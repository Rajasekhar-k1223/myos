#include "task.h"
#include "pit.h"
#include "string.h"
#include "kernel.h"
#include <stdint.h>

/* ── Globals ─────────────────────────────────────────────────────────────── */
task_t tasks[MAX_TASKS]; /* non-static: shell ps command reads it directly */
static int     current_tid  = 0;
static int     task_count_n = 0;

extern void context_switch(cpu_context_t* old_ctx, cpu_context_t* new_ctx);
extern uint32_t* current_page_directory;
extern void paging_switch_directory(uint32_t* dir);
extern void tss_set_stack(uint32_t ss0, uint32_t esp0);
void jump_usermode(void);

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static uint32_t next_id = 0;

static task_t* alloc_slot(void) {
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TASK_DEAD) return &tasks[i];
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

task_t* task_current(void) {
    return &tasks[current_tid];
}

uint32_t task_count(void) {
    uint32_t n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_DEAD) n++;
    return n;
}

/*
 * Initialise the tasking subsystem.
 * The currently-running code becomes task 0 ("kernel").
 */
void tasking_init(void) {
    memset(tasks, 0, sizeof(tasks));
    for (int i = 0; i < MAX_TASKS; i++)
        tasks[i].state = TASK_DEAD;

    /* Adopt the current execution context as task 0 */
    tasks[0].id    = next_id++;
    tasks[0].state = TASK_RUNNING;
    strncpy(tasks[0].name, "kernel", TASK_NAME_LEN - 1);
    tasks[0].page_directory = current_page_directory;
    tasks[0].kernel_stack = (uint32_t)(tasks[0].stack + TASK_STACK_SIZE);
    task_count_n = 1;
    current_tid  = 0;
}

/*
 * Create a new kernel thread starting at entry().
 * Sets up a minimal stack frame so the first context_switch lands in entry().
 */
int task_create(const char* name, void (*entry)(void)) {
    task_t* t = alloc_slot();
    if (!t) return -1;

    memset(t, 0, sizeof(task_t));
    t->id    = next_id++;
    t->state = TASK_READY;
    strncpy(t->name, name, TASK_NAME_LEN - 1);

    /*
     * Set up the new task's stack so that context_switch() will jump into
     * entry() on the first switch.
     *
     * Stack layout (growing downward):
     *   [top of stack - 4]  = address of task_exit  (return address if entry returns)
     *   ctx.esp             = points here
     *   ctx.eip             = entry
     *   ctx.eflags          = IF enabled
     */
    uint32_t* sp = (uint32_t*)(t->stack + TASK_STACK_SIZE);
    *(--sp) = (uint32_t)task_exit;   /* return address: if entry() returns */

    t->page_directory = current_page_directory;
    t->kernel_stack = (uint32_t)sp + 4; // Top of stack before task_exit was pushed

    t->ctx.esp    = (uint32_t)sp;
    t->ctx.eip    = (uint32_t)entry;
    t->ctx.eflags = 0x202; /* IF=1, bit 1 always set */

    task_count_n++;
    return (int)t->id;
}

/*
 * Create a new Ring 3 User Mode task.
 */
int task_create_user(const char* name, uint32_t entry, uint32_t user_stack_top, uint32_t* page_directory) {
    task_t* t = alloc_slot();
    if (!t) return -1;

    memset(t, 0, sizeof(task_t));
    t->id    = next_id++;
    t->state = TASK_READY;
    strncpy(t->name, name, TASK_NAME_LEN - 1);

    t->page_directory = page_directory;
    uint32_t* sp = (uint32_t*)(t->stack + TASK_STACK_SIZE);
    t->kernel_stack = (uint32_t)sp;

    // Set up an IRET frame for User Mode
    *(--sp) = 0x23;             // SS (User Data Segment)
    *(--sp) = user_stack_top;   // User ESP
    *(--sp) = 0x202;            // EFLAGS (IF=1)
    *(--sp) = 0x1B;             // CS (User Code Segment)
    *(--sp) = entry;            // EIP (User entry point)
    
    // We also need a return address for the context_switch return,
    // which will point to a tiny assembly stub that just does `iret`.
    *(--sp) = (uint32_t)jump_usermode; 

    t->ctx.esp    = (uint32_t)sp;
    t->ctx.eip    = (uint32_t)jump_usermode;
    t->ctx.eflags = 0x202;

    task_count_n++;
    return (int)t->id;
}

/* Called by a task when it wants to stop. */
void task_exit(void) {
    __asm__ volatile("cli");
    tasks[current_tid].state = TASK_DEAD;
    task_count_n--;
    __asm__ volatile("sti");
    schedule(); /* yield — never returns to this task */
    for (;;) __asm__ volatile("hlt");
}

/* Sleep for approximately ms milliseconds (rounded to 10 ms ticks). */
void task_sleep(uint32_t ms) {
    uint32_t ticks = (ms * 100) / 1000; /* convert ms to 100 Hz ticks */
    if (!ticks) ticks = 1;
    tasks[current_tid].state       = TASK_SLEEPING;
    tasks[current_tid].sleep_ticks = ticks;
    schedule();
}

/*
 * Called from the PIT IRQ0 handler every tick (100 Hz).
 * Decrements sleep counters and wakes tasks whose time has come.
 */
void task_tick(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING) {
            if (tasks[i].sleep_ticks > 0)
                tasks[i].sleep_ticks--;
            if (tasks[i].sleep_ticks == 0)
                tasks[i].state = TASK_READY;
        }
    }
}

/*
 * Round-robin scheduler.
 * Picks the next READY task and switches to it.
 */
void schedule(void) {
    int old = current_tid;

    /* Mark current as ready (unless it's sleeping or dead) */
    if (tasks[old].state == TASK_RUNNING)
        tasks[old].state = TASK_READY;

    /* Find next READY task (round-robin) */
    int next = old;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int candidate = (old + i) % MAX_TASKS;
        if (tasks[candidate].state == TASK_READY) {
            next = candidate;
            break;
        }
    }

    /* No other ready task — stay on current if possible */
    if (next == old) {
        if (tasks[old].state == TASK_READY) {
            tasks[old].state = TASK_RUNNING;
            return; /* no switch needed */
        }
        /* All tasks blocked — spin in idle until someone wakes */
        __asm__ volatile("sti");
        while (1) {
            int found = 0;
            for (int i = 0; i < MAX_TASKS; i++)
                if (tasks[i].state == TASK_READY) { found = 1; break; }
            if (found) break;
            __asm__ volatile("hlt");
        }
        /* Re-enter scheduler after someone woke up */
        for (int i = 1; i <= MAX_TASKS; i++) {
            int c = (old + i) % MAX_TASKS;
            if (tasks[c].state == TASK_READY) { next = c; break; }
        }
    }

    tasks[next].state = TASK_RUNNING;
    current_tid       = next;

    // Switch Address Space
    if (tasks[next].page_directory && tasks[next].page_directory != current_page_directory) {
        paging_switch_directory(tasks[next].page_directory);
    }
    
    // Update TSS so interrupts from User Mode switch to the correct kernel stack!
    tss_set_stack(0x10, tasks[next].kernel_stack);

    context_switch(&tasks[old].ctx, &tasks[next].ctx);
}
