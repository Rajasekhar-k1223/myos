#include "nvme.h"
#include "pci.h"
#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "kheap.h"

/* NVMe Admin & I/O Queue state */
#define NVME_QUEUE_DEPTH 4

typedef struct {
    uint32_t cdw[16]; /* 64-byte command */
} nvme_cmd_t;

typedef struct {
    uint32_t dw[4]; /* 16-byte completion entry */
} nvme_cq_entry_t;

static nvme_bar0_t* nvme_bar   = NULL;
static int          nvme_ready = 0;

/* Admin queues — statically allocated (page-aligned via kmalloc) */
static nvme_cmd_t*    asq_base  = NULL; /* Admin Submission Queue  */
static nvme_cq_entry_t* acq_base = NULL; /* Admin Completion Queue  */
static uint32_t       asq_tail  = 0;
static uint32_t       acq_head  = 0;
static uint32_t       acq_phase = 1; /* expected phase bit */

/* I/O queues (single pair, depth NVME_QUEUE_DEPTH) */
static nvme_cmd_t*    iosq_base = NULL;
static nvme_cq_entry_t* iocq_base = NULL;
static uint32_t       iosq_tail = 0;
static uint32_t       iocq_head = 0;
static uint32_t       iocq_phase = 1;

static uint16_t       cmd_id    = 1;

/* Doorbell stride (in bytes) = 4 << CAP.DSTRD */
static uint32_t db_stride = 4;

static inline uint32_t nvme_db_offset(int queue, int is_cq) {
    return 0x1000 + ((2 * queue + is_cq) * db_stride);
}

static void nvme_ring_sq_doorbell(int queue, uint32_t tail) {
    volatile uint32_t* db = (volatile uint32_t*)((uint8_t*)nvme_bar + nvme_db_offset(queue, 0));
    *db = tail;
}

static int nvme_wait_cq(nvme_cq_entry_t* cq, uint32_t* head, uint32_t* phase, uint32_t depth) {
    int timeout = 2000000;
    while (timeout-- > 0) {
        nvme_cq_entry_t* e = &cq[*head];
        uint32_t p = (e->dw[3] >> 16) & 1;
        if (p == *phase) {
            uint16_t status = (e->dw[3] >> 17) & 0x7FF;
            if (++(*head) >= depth) { *head = 0; *phase ^= 1; }
            return status == 0 ? 1 : 0;
        }
    }
    terminal_printf("[NVMe] CQ timeout.\n");
    return 0;
}

/* Submit a command to the admin submission queue */
static int nvme_submit_admin(nvme_cmd_t* cmd) {
    memcpy(&asq_base[asq_tail], cmd, sizeof(nvme_cmd_t));
    asq_tail = (asq_tail + 1) % NVME_QUEUE_DEPTH;
    nvme_ring_sq_doorbell(0, asq_tail);
    return nvme_wait_cq(acq_base, &acq_head, &acq_phase, NVME_QUEUE_DEPTH);
}

void nvme_init(void) {
    nvme_ready = 0;
    pci_device_t dev;
    if (!pci_get_device_by_class(0x01, 0x08, 0x02, &dev)) {
        terminal_printf("[NVMe] No NVMe controller found.\n");
        return;
    }

    uint32_t bar0_raw = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x10);
    nvme_bar = (nvme_bar0_t*)(bar0_raw & ~0xFu);

    /* Map BAR0 pages */
    uint32_t phys = (uint32_t)nvme_bar & ~0xFFFu;
    for (int p = 0; p < 4; p++)
        paging_map_page(phys + p * 0x1000, phys + p * 0x1000, 0x1B);
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");

    pci_enable_bus_mastering(&dev);

    db_stride = 4u << ((uint32_t)(nvme_bar->cap >> 32) & 0xF);

    /* Disable controller */
    nvme_bar->cc &= ~1u;
    int t = 500000; while ((nvme_bar->csts & 1) && t-- > 0);

    /* Allocate admin queues */
    asq_base = (nvme_cmd_t*)kmalloc(NVME_QUEUE_DEPTH * 64);
    acq_base = (nvme_cq_entry_t*)kmalloc(NVME_QUEUE_DEPTH * 16);
    if (!asq_base || !acq_base) return;
    memset(asq_base, 0, NVME_QUEUE_DEPTH * 64);
    memset(acq_base, 0, NVME_QUEUE_DEPTH * 16);

    nvme_bar->aqa = ((NVME_QUEUE_DEPTH - 1) << 16) | (NVME_QUEUE_DEPTH - 1);
    nvme_bar->asq = (uint64_t)(uint32_t)asq_base;
    nvme_bar->acq = (uint64_t)(uint32_t)acq_base;

    /* Configure and enable controller (4KB pages, NVM command set) */
    nvme_bar->cc = (0 << 20) | (0 << 16) | (6 << 7) | (4 << 4) | 1;
    t = 2000000; while (!(nvme_bar->csts & 1) && t-- > 0);
    if (!(nvme_bar->csts & 1)) {
        terminal_printf("[NVMe] Controller did not become ready.\n");
        return;
    }

    /* Create I/O queues via admin commands */
    iosq_base = (nvme_cmd_t*)kmalloc(NVME_QUEUE_DEPTH * 64);
    iocq_base = (nvme_cq_entry_t*)kmalloc(NVME_QUEUE_DEPTH * 16);
    if (!iosq_base || !iocq_base) return;
    memset(iosq_base, 0, NVME_QUEUE_DEPTH * 64);
    memset(iocq_base, 0, NVME_QUEUE_DEPTH * 16);

    /* Create I/O Completion Queue (opcode 0x05) */
    nvme_cmd_t cmd; memset(&cmd, 0, sizeof(cmd));
    cmd.cdw[0] = 0x05 | ((uint32_t)cmd_id++ << 16);
    cmd.cdw[6] = (uint32_t)iocq_base;
    cmd.cdw[10] = ((NVME_QUEUE_DEPTH - 1) << 16) | 1; /* QID=1 */
    cmd.cdw[11] = 1;                                    /* PC=1 (phys contiguous) */
    if (!nvme_submit_admin(&cmd)) { terminal_printf("[NVMe] Create IOCQ failed.\n"); return; }

    /* Create I/O Submission Queue (opcode 0x01) */
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw[0] = 0x01 | ((uint32_t)cmd_id++ << 16);
    cmd.cdw[6] = (uint32_t)iosq_base;
    cmd.cdw[10] = ((NVME_QUEUE_DEPTH - 1) << 16) | 1; /* QID=1 */
    cmd.cdw[11] = (1 << 16) | 1;                       /* CQID=1, PC=1 */
    if (!nvme_submit_admin(&cmd)) { terminal_printf("[NVMe] Create IOSQ failed.\n"); return; }

    nvme_ready = 1;
    terminal_printf("[NVMe] Ready — I/O queues established (depth=%d).\n", NVME_QUEUE_DEPTH);
}

/* Read one 512-byte sector from LBA into `buf` (blocking, polling) */
int nvme_read_sector(uint64_t lba, void* buf) {
    if (!nvme_ready) return 0;

    nvme_cmd_t cmd; memset(&cmd, 0, sizeof(cmd));
    cmd.cdw[0]  = 0x02 | ((uint32_t)cmd_id++ << 16); /* NVM Read */
    cmd.cdw[6]  = (uint32_t)buf;                       /* PRP1 low */
    cmd.cdw[7]  = 0;                                   /* PRP1 high */
    cmd.cdw[10] = (uint32_t)(lba & 0xFFFFFFFF);        /* SLBA low  */
    cmd.cdw[11] = (uint32_t)(lba >> 32);               /* SLBA high */
    cmd.cdw[12] = 0;                                   /* NLB = 1 sector (0-based) */

    memcpy(&iosq_base[iosq_tail], &cmd, sizeof(cmd));
    iosq_tail = (iosq_tail + 1) % NVME_QUEUE_DEPTH;
    nvme_ring_sq_doorbell(1, iosq_tail);
    return nvme_wait_cq(iocq_base, &iocq_head, &iocq_phase, NVME_QUEUE_DEPTH);
}

/* Write one 512-byte sector at LBA from `buf` (blocking, polling) */
int nvme_write_sector(uint64_t lba, const void* buf) {
    if (!nvme_ready) return 0;

    nvme_cmd_t cmd; memset(&cmd, 0, sizeof(cmd));
    cmd.cdw[0]  = 0x01 | ((uint32_t)cmd_id++ << 16); /* NVM Write */
    cmd.cdw[6]  = (uint32_t)buf;                       /* PRP1 low */
    cmd.cdw[7]  = 0;                                   /* PRP1 high */
    cmd.cdw[10] = (uint32_t)(lba & 0xFFFFFFFF);        /* SLBA low  */
    cmd.cdw[11] = (uint32_t)(lba >> 32);               /* SLBA high */
    cmd.cdw[12] = 0;                                   /* NLB = 1 sector (0-based) */

    memcpy(&iosq_base[iosq_tail], &cmd, sizeof(cmd));
    iosq_tail = (iosq_tail + 1) % NVME_QUEUE_DEPTH;
    nvme_ring_sq_doorbell(1, iosq_tail);
    return nvme_wait_cq(iocq_base, &iocq_head, &iocq_phase, NVME_QUEUE_DEPTH);
}
