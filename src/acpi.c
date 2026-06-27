#include "acpi.h"
#include "kernel.h"
#include "string.h"
#include "paging.h"
#include "io.h"

uint32_t local_apic_base = 0;
uint8_t  apic_ids[MAX_CORES];
int      num_cores   = 0;
uint8_t  bsp_apic_id = 0;

/* ── ACPI PM port state (from FADT) ─────────────────────────────────────── */
static uint32_t pm1a_cnt = 0;   /* PM1a_CNT_BLK  */
static uint32_t pm1b_cnt = 0;   /* PM1b_CNT_BLK (0 if not present) */
static uint8_t  s3_slp_typ_a = 0;
static uint8_t  s3_slp_typ_b = 0;
static uint8_t  s5_slp_typ_a = 5;
static uint8_t  s5_slp_typ_b = 5;
static int      acpi_s3_ready = 0;

/* ── Table header ────────────────────────────────────────────────────────── */
struct rsdp_descriptor {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

/* ── FADT (Fixed ACPI Description Table) ─────────────────────────────────── */
struct fadt {
    struct sdt_header hdr;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;  /* <-- PM1a_CNT_BLK */
    uint32_t pm1b_cnt_blk;  /* <-- PM1b_CNT_BLK */
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    /* … more fields we don't need … */
} __attribute__((packed));

/* ── DSDT SLP_TYP parser ─────────────────────────────────────────────────── */
/* Searches for \_S3_ or \_S5_ AML package in the DSDT raw bytes */
static int find_slp_typ(uint8_t* dsdt_data, uint32_t len,
                        const char* name4, uint8_t* typ_a, uint8_t* typ_b) {
    for (uint32_t i = 0; i + 8 < len; i++) {
        /* Look for the 4-char name as a sequence of bytes with 0x5C 0x2F … or direct */
        if (dsdt_data[i]   == (uint8_t)name4[0] &&
            dsdt_data[i+1] == (uint8_t)name4[1] &&
            dsdt_data[i+2] == (uint8_t)name4[2] &&
            dsdt_data[i+3] == (uint8_t)name4[3]) {
            /* The package typically follows: 0x12 0x06 0x04 0x0A <val_a> 0x0A <val_b> */
            if (i + 8 < len && dsdt_data[i+4] == 0x12) {
                *typ_a = dsdt_data[i+6];
                *typ_b = dsdt_data[i+8];
                return 1;
            }
            /* Some firmwares use slightly different layout */
            if (i + 7 < len && dsdt_data[i+5] == 0x0A) {
                *typ_a = dsdt_data[i+6];
                *typ_b = (i + 8 < len) ? dsdt_data[i+8] : 0;
                return 1;
            }
        }
    }
    return 0;
}

/* ── RSDP finder ─────────────────────────────────────────────────────────── */
static void* find_rsdp(void) {
    uint8_t* mem = (uint8_t*)0x000E0000;
    while ((uint32_t)mem < 0x000FFFFF) {
        if (strncmp((char*)mem, "RSD PTR ", 8) == 0) {
            uint8_t sum = 0;
            for (int i = 0; i < 20; i++) sum += mem[i];
            if (sum == 0) return mem;
        }
        mem += 16;
    }
    /* Also check EBDA */
    uint16_t ebda_seg = *(uint16_t*)0x40E;
    uint8_t* ebda = (uint8_t*)((uint32_t)ebda_seg << 4);
    for (int i = 0; i < 1024; i += 16) {
        if (strncmp((char*)(ebda + i), "RSD PTR ", 8) == 0) {
            uint8_t sum = 0;
            for (int j = 0; j < 20; j++) sum += ebda[i+j];
            if (sum == 0) return ebda + i;
        }
    }
    return NULL;
}

/* ── acpi_init ───────────────────────────────────────────────────────────── */
void acpi_init(void) {
    struct rsdp_descriptor* rsdp = (struct rsdp_descriptor*)find_rsdp();
    if (!rsdp) { terminal_printf("[ACPI] RSDP not found.\n"); return; }

    struct sdt_header* rsdt = (struct sdt_header*)rsdp->rsdt_address;
    if (!rsdt) { terminal_printf("[ACPI] RSDT null.\n"); return; }

    int entries = (rsdt->length - sizeof(struct sdt_header)) / 4;
    uint32_t* ptrs = (uint32_t*)((uint8_t*)rsdt + sizeof(struct sdt_header));

    struct sdt_header* madt = NULL;
    struct fadt*       fadt = NULL;

    for (int i = 0; i < entries; i++) {
        struct sdt_header* hdr = (struct sdt_header*)ptrs[i];
        if (!hdr) continue;
        if (strncmp(hdr->signature, "APIC", 4) == 0) madt = hdr;
        if (strncmp(hdr->signature, "FACP", 4) == 0) fadt = (struct fadt*)hdr;
    }

    /* ── Parse MADT for LAPIC base + CPU APIC IDs ── */
    if (madt) {
        uint8_t* p = (uint8_t*)madt + sizeof(struct sdt_header);
        local_apic_base = *(uint32_t*)p;
        p += 8;
        while (p < (uint8_t*)madt + madt->length) {
            uint8_t type = p[0], len = p[1];
            if (type == 0) { /* Processor Local APIC */
                uint8_t  apic_id = p[3];
                uint32_t flags   = *(uint32_t*)(p + 4);
                if ((flags & 1) && num_cores < MAX_CORES)
                    apic_ids[num_cores++] = apic_id;
            }
            p += len;
        }
        terminal_printf("[ACPI] %d core(s). LAPIC @ 0x%x\n", num_cores, local_apic_base);
    } else {
        terminal_printf("[ACPI] MADT not found.\n");
    }

    /* ── Parse FADT for PM1 control ports + DSDT SLP_TYP values ── */
    if (fadt) {
        pm1a_cnt = fadt->pm1a_cnt_blk;
        pm1b_cnt = fadt->pm1b_cnt_blk;

        /* Parse DSDT to find _S3_ and _S5_ sleep type values */
        struct sdt_header* dsdt = (struct sdt_header*)fadt->dsdt;
        if (dsdt) {
            uint8_t* aml  = (uint8_t*)dsdt + sizeof(struct sdt_header);
            uint32_t alen = dsdt->length   - sizeof(struct sdt_header);

            if (find_slp_typ(aml, alen, "_S3_", &s3_slp_typ_a, &s3_slp_typ_b)) {
                acpi_s3_ready = 1;
                terminal_printf("[ACPI] S3 SLP_TYP: a=%u b=%u\n", s3_slp_typ_a, s3_slp_typ_b);
            }
            if (find_slp_typ(aml, alen, "_S5_", &s5_slp_typ_a, &s5_slp_typ_b)) {
                terminal_printf("[ACPI] S5 SLP_TYP: a=%u b=%u\n", s5_slp_typ_a, s5_slp_typ_b);
            }
        }
        terminal_printf("[ACPI] PM1a_CNT=0x%x  PM1b_CNT=0x%x\n", pm1a_cnt, pm1b_cnt);
    }
}

/* ── acpi_shutdown (S5 — power off) ─────────────────────────────────────── */
void acpi_shutdown(void) {
    terminal_printf("[ACPI] Entering S5 (power off)...\n");
    asm volatile("cli");

    if (pm1a_cnt) {
        uint16_t val = (uint16_t)((s5_slp_typ_a << 10) | (1 << 13)); /* SLP_TYP | SLP_EN */
        outw(pm1a_cnt, val);
        if (pm1b_cnt) outw(pm1b_cnt, (uint16_t)((s5_slp_typ_b << 10) | (1 << 13)));
    }
    /* QEMU/Bochs fallbacks */
    outw(0x604,  0x2000);
    outw(0xB004, 0x2000);
    for (;;) asm volatile("cli; hlt");
}

/* ── acpi_sleep_s3 (S3 — suspend-to-RAM) ────────────────────────────────── */
void acpi_sleep_s3(void) {
    if (!acpi_s3_ready || !pm1a_cnt) {
        terminal_printf("[ACPI] S3 not available — FADT/DSDT didn't expose _S3_\n");
        return;
    }
    terminal_printf("[ACPI] Entering S3 (suspend to RAM)...\n");

    /* Save processor state (simplified — real resume needs a wakeup vector) */
    asm volatile("cli");
    uint16_t val_a = (uint16_t)((s3_slp_typ_a << 10) | (1 << 13));
    uint16_t val_b = (uint16_t)((s3_slp_typ_b << 10) | (1 << 13));
    outw(pm1a_cnt, val_a);
    if (pm1b_cnt) outw(pm1b_cnt, val_b);

    /* If we wake up from S3, re-enable interrupts */
    asm volatile("sti");
    terminal_printf("[ACPI] Resumed from S3.\n");
}

/* ── acpi_reboot ─────────────────────────────────────────────────────────── */
void acpi_reboot(void) {
    terminal_printf("[ACPI] Rebooting...\n");
    /* Keyboard controller reset line */
    uint8_t v;
    do { asm volatile("inb %1,%0" : "=a"(v) : "Nd"((uint16_t)0x64)); } while (v & 2);
    asm volatile("outb %0, %1" :: "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    /* ACPI reset register fallback */
    outb(0xCF9, 0x06);
    for (;;) asm volatile("hlt");
}
