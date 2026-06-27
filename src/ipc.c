#include "ipc.h"
#include "kernel.h"
#include "string.h"

/* ── Message queue table ─────────────────────────────────────────────────── */
static ipc_queue_t queues[IPC_MAX_QUEUES];

/* ── Shared memory table ─────────────────────────────────────────────────── */
typedef struct {
    int      key;
    int      used;
    uint32_t size;
} shm_seg_t;

static shm_seg_t   shm_segs[SHM_MAX_SEGS];
static uint8_t     shm_pages[SHM_MAX_SEGS][SHM_SEG_SIZE];

/* ── ipc_init ────────────────────────────────────────────────────────────── */
void ipc_init(void) {
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        queues[i].used  = 0;
        queues[i].key   = 0;
        queues[i].head  = 0;
        queues[i].tail  = 0;
        queues[i].count = 0;
    }
    for (int i = 0; i < SHM_MAX_SEGS; i++) {
        shm_segs[i].used = 0;
        shm_segs[i].key  = 0;
        shm_segs[i].size = 0;
        memset(shm_pages[i], 0, SHM_SEG_SIZE);
    }
}

/* ── Message queue API ───────────────────────────────────────────────────── */

int ipc_msgget(int key, int flags) {
    (void)flags;
    /* Search for existing queue with this key */
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (queues[i].used && queues[i].key == key)
            return i;
    }
    /* Create new queue */
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (!queues[i].used) {
            queues[i].used  = 1;
            queues[i].key   = key;
            queues[i].head  = 0;
            queues[i].tail  = 0;
            queues[i].count = 0;
            return i;
        }
    }
    return -1; /* no free slot */
}

int ipc_msgsnd(int qid, const ipc_msg_t* msg, uint32_t size, int flags) {
    (void)flags;
    if (qid < 0 || qid >= IPC_MAX_QUEUES) return -1;
    ipc_queue_t* q = &queues[qid];
    if (!q->used) return -1;
    if (q->count >= IPC_MAX_MSG) return -1; /* queue full */

    ipc_msg_t* dst = &q->msgs[q->tail];
    dst->mtype = msg->mtype;
    uint32_t copy_len = size < IPC_MSG_SIZE ? size : IPC_MSG_SIZE;
    memcpy(dst->mtext, msg->mtext, copy_len);

    q->tail = (q->tail + 1) % IPC_MAX_MSG;
    q->count++;
    return 0;
}

int ipc_msgrcv(int qid, ipc_msg_t* msg, uint32_t size, long mtype, int flags) {
    (void)flags;
    if (qid < 0 || qid >= IPC_MAX_QUEUES) return -1;
    ipc_queue_t* q = &queues[qid];
    if (!q->used) return -1;
    if (q->count == 0) return -1; /* queue empty */

    if (mtype == 0) {
        /* Dequeue from head */
        ipc_msg_t* src = &q->msgs[q->head];
        msg->mtype = src->mtype;
        uint32_t copy_len = size < IPC_MSG_SIZE ? size : IPC_MSG_SIZE;
        memcpy(msg->mtext, src->mtext, copy_len);
        q->head = (q->head + 1) % IPC_MAX_MSG;
        q->count--;
        return (int)copy_len;
    } else {
        /* Search for a message with matching mtype (linear scan, circular) */
        int scan = q->head;
        for (int i = 0; i < q->count; i++) {
            ipc_msg_t* src = &q->msgs[scan];
            if (src->mtype == mtype) {
                msg->mtype = src->mtype;
                uint32_t copy_len = size < IPC_MSG_SIZE ? size : IPC_MSG_SIZE;
                memcpy(msg->mtext, src->mtext, copy_len);
                /* Compact the circular buffer: shift remaining entries */
                int cur = scan;
                for (int j = i; j < q->count - 1; j++) {
                    int nxt = (cur + 1) % IPC_MAX_MSG;
                    q->msgs[cur] = q->msgs[nxt];
                    cur = nxt;
                }
                q->tail = (q->tail - 1 + IPC_MAX_MSG) % IPC_MAX_MSG;
                q->count--;
                return (int)copy_len;
            }
            scan = (scan + 1) % IPC_MAX_MSG;
        }
        return -1; /* not found */
    }
}

int ipc_msgctl(int qid, int cmd, void* buf) {
    (void)buf;
    if (qid < 0 || qid >= IPC_MAX_QUEUES) return -1;
    ipc_queue_t* q = &queues[qid];
    if (!q->used) return -1;
    if (cmd == IPC_RMID) {
        q->used  = 0;
        q->key   = 0;
        q->head  = 0;
        q->tail  = 0;
        q->count = 0;
        return 0;
    }
    return -1;
}

/* ── Shared memory API ───────────────────────────────────────────────────── */

int shm_get(int key, uint32_t size, int flags) {
    (void)flags;
    /* Search for existing segment with this key */
    for (int i = 0; i < SHM_MAX_SEGS; i++) {
        if (shm_segs[i].used && shm_segs[i].key == key)
            return i;
    }
    /* Allocate new segment */
    if (size > SHM_SEG_SIZE) return -1;
    for (int i = 0; i < SHM_MAX_SEGS; i++) {
        if (!shm_segs[i].used) {
            shm_segs[i].used = 1;
            shm_segs[i].key  = key;
            shm_segs[i].size = size ? size : SHM_SEG_SIZE;
            memset(shm_pages[i], 0, SHM_SEG_SIZE);
            return i;
        }
    }
    return -1;
}

void* shm_at(int shmid, int flags) {
    (void)flags;
    if (shmid < 0 || shmid >= SHM_MAX_SEGS) return (void*)0;
    if (!shm_segs[shmid].used) return (void*)0;
    return (void*)shm_pages[shmid];
}

int shm_dt(void* addr) {
    /* In this simple implementation, pages are static — nothing to do */
    (void)addr;
    return 0;
}

int shm_ctl(int shmid, int cmd) {
    if (shmid < 0 || shmid >= SHM_MAX_SEGS) return -1;
    if (!shm_segs[shmid].used) return -1;
    if (cmd == IPC_RMID) {
        shm_segs[shmid].used = 0;
        shm_segs[shmid].key  = 0;
        shm_segs[shmid].size = 0;
        memset(shm_pages[shmid], 0, SHM_SEG_SIZE);
        return 0;
    }
    return -1;
}

/* ── ipc_print_all ───────────────────────────────────────────────────────── */
void ipc_print_all(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  IPC Message Queues\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ──────────────────────────────────────────\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    int any = 0;
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (!queues[i].used) continue;
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_printf("  qid=%-2d", i);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_printf("  key=%-8d  msgs=%d/%d\n",
                        queues[i].key, queues[i].count, IPC_MAX_MSG);
        any++;
    }
    if (!any) terminal_writestring("  (no message queues)\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  Shared Memory Segments\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ──────────────────────────────────────────\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    any = 0;
    for (int i = 0; i < SHM_MAX_SEGS; i++) {
        if (!shm_segs[i].used) continue;
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_printf("  shmid=%-2d", i);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_printf("  key=%-8d  size=%u bytes  addr=0x%08x\n",
                        shm_segs[i].key, shm_segs[i].size,
                        (uint32_t)(uintptr_t)shm_pages[i]);
        any++;
    }
    if (!any) terminal_writestring("  (no shared memory segments)\n");
    terminal_putchar('\n');
}
