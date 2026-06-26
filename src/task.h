#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

/* ── Task states ─────────────────────────────────────────────────────────── */
typedef enum {
    TASK_RUNNING  = 0,
    TASK_READY    = 1,
    TASK_SLEEPING = 2,
    TASK_DEAD     = 3,
} task_state_t;

/* ── Saved CPU context (matches context_switch.S push order) ────────────── */
typedef struct {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip;
    uint32_t eflags;
} cpu_context_t;

/* ── Task Control Block ──────────────────────────────────────────────────── */
#define TASK_STACK_SIZE  8192   /* 8 KB per task */
#define TASK_NAME_LEN      16
#define MAX_TASKS           8

typedef struct task {
    cpu_context_t  ctx;
    uint32_t       id;
    task_state_t   state;
    uint32_t       sleep_ticks;  /* non-zero while sleeping */
    char           name[TASK_NAME_LEN];
    uint8_t        stack[TASK_STACK_SIZE];
    uint32_t*      page_directory;
    uint32_t       kernel_stack;
} task_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
void     tasking_init(void);
int      task_create(const char* name, void (*entry)(void));
int      task_create_user(const char* name, uint32_t entry, uint32_t user_stack_top, uint32_t* page_directory);
void     task_exit(void);
void     task_sleep(uint32_t ms);
void     task_tick(void);           /* called from PIT IRQ0 */
void     schedule(void);            /* explicit yield */
task_t*  task_current(void);
uint32_t task_count(void);

/* Non-static array so shell ps can inspect it directly */
extern task_t tasks[MAX_TASKS];

#endif
