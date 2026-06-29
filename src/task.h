#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"

/* ── Task states ─────────────────────────────────────────────────────────── */
typedef enum {
    TASK_RUNNING  = 0,
    TASK_READY    = 1,
    TASK_SLEEPING = 2,
    TASK_DEAD     = 3,
} task_state_t;

/* ── Saved CPU context (matches context_switch.S push order) ────────────── */
// Must match context_switch.S exactly!
typedef struct {
    uint32_t edi;    // 0
    uint32_t esi;    // 4
    uint32_t ebp;    // 8
    uint32_t esp;    // 12
    uint32_t ebx;    // 16
    uint32_t edx;    // 20
    uint32_t ecx;    // 24
    uint32_t eax;    // 28
    uint32_t eip;    // 32
    uint32_t eflags; // 36
} cpu_context_t;

/* ── Task Control Block ──────────────────────────────────────────────────── */
#define TASK_STACK_SIZE  8192   /* 8 KB per task */
#define TASK_NAME_LEN      16
#define MAX_TASKS           8

typedef struct task {
    cpu_context_t  ctx;
    uint32_t       id;
    uint32_t       parent_id;
    task_state_t   state;
    uint32_t       sleep_ticks;  /* non-zero while sleeping */
    uint32_t       wait_pid;     /* waiting for this pid to exit */
    char           name[TASK_NAME_LEN];
    uint8_t        stack[TASK_STACK_SIZE];
    uint32_t*      page_directory;
    uint32_t       kernel_stack;
    int            cpu;          /* CPU core ID this task is running on, or -1 */
    int            affinity;     /* CPU core ID this task MUST run on, or -1 for any */
    uint32_t       pending_signals;
    uint32_t       sig_handlers[32];
    uint32_t       in_signal_handler;
    uint32_t       tls_base;
    uint32_t       mmap_next; /* next virtual address for sys_mmap */
    uint32_t       brk;       /* current program break for sys_sbrk */
} task_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
void     tasking_init(void);
void     tasking_init_ap(void);
void     task_create_idle(uint8_t core_id);
int      task_create(const char* name, void (*entry)(void));
int      task_create_user(const char* name, uint32_t entry, uint32_t user_stack_top, uint32_t* page_directory);
int      task_fork(struct registers* regs);
void     task_exit(void);
void     task_sleep(uint32_t ms);
void     task_tick(void);           /* called from PIT IRQ0 */
void     schedule(void);            /* explicit yield */
task_t*  task_current(void);
uint32_t task_count(void);
int      task_waitpid(int pid);
uint32_t task_getpid(void);

/* Non-static array so shell ps can inspect it directly */
extern task_t tasks[MAX_TASKS];

#endif
