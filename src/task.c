#include "task.h"
#include "pit.h"
#include "string.h"
#include "kernel.h"
#include "paging.h"
#include <stdint.h>

#include "apic.h"
#include "acpi.h"

/* ── Globals ─────────────────────────────────────────────────────────────── */
task_t tasks[MAX_TASKS]; /* non-static: shell ps command reads it directly */
static int     current_tasks[256];
// Note: initialized to -1 in tasking_init(), NOT via static init
// (BSS zero-init would give 0 = task[0], causing APs to corrupt BSP task)
static int     task_count_n = 0;
static uint32_t scheduler_lock = 0;

static inline void lock_sched(void) {
    asm volatile("cli");
    while (__sync_lock_test_and_set(&scheduler_lock, 1)) {
        while (scheduler_lock) { asm volatile("pause"); }
    }
}

static inline void unlock_sched(void) {
    __sync_lock_release(&scheduler_lock);
    asm volatile("sti");
}

extern void context_switch(cpu_context_t* old_ctx, cpu_context_t* new_ctx);
extern uint32_t* current_page_directory;
extern void paging_switch_directory(uint32_t* dir);
extern void tss_set_stack(uint32_t core_idx, uint32_t ss0, uint32_t esp0);
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
    return &tasks[current_tasks[apic_get_id()]];
}

uint32_t task_getpid(void) {
    return tasks[current_tasks[apic_get_id()]].id;
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
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_DEAD;
        tasks[i].cpu = -1;
        tasks[i].affinity = -1;
    }
    for (int i = 0; i < 256; i++) {
        current_tasks[i] = -1;
    }

    /* Adopt the current execution context as task 0 */
    tasks[0].id    = next_id++;
    tasks[0].state = TASK_RUNNING;
    tasks[0].cpu   = bsp_apic_id;
    tasks[0].affinity = bsp_apic_id; // Kernel task MUST run on BSP for now
    strncpy(tasks[0].name, "kernel", TASK_NAME_LEN - 1);
    tasks[0].page_directory = current_page_directory;
    tasks[0].kernel_stack = (uint32_t)(tasks[0].stack + TASK_STACK_SIZE);
    task_count_n = 1;
    
    current_tasks[bsp_apic_id]  = 0;
}

/* Called by each AP core before enabling interrupts.
 * Marks the AP as having no current task so the first scheduler tick
 * correctly picks a READY task instead of inheriting task[0]. */
void tasking_init_ap(void) {
    uint8_t id = apic_get_id();
    current_tasks[id] = -1;
}

static void idle_thread(void) {
    while (1) {
        asm volatile("sti; hlt");
    }
}

void task_create_idle(uint8_t core_id) {
    int tid = task_create("idle", idle_thread);
    if (tid >= 0) {
        tasks[tid].affinity = core_id; // bind to this core
    }
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
    t->cpu   = -1;
    strncpy(t->name, name, TASK_NAME_LEN - 1);

    /*
     * context_switch.S restore path:
     *   1. Restores edi/esi/ebp/ebx/edx from ctx struct fields (NOT from stack)
     *   2. Restores eflags from ctx.eflags
     *   3. mov ctx.esp -> %esp      (restores stack pointer)
     *   4. ret                      (pops EIP from [esp])
     *
     * For a new task, ctx.esp must point to the entry address on the stack,
     * so that `ret` pops entry and jumps there.
     * task_exit sits above entry so that if entry() does `ret`, it lands there.
     */
    uint32_t* sp = (uint32_t*)(t->stack + TASK_STACK_SIZE);
    *(--sp) = (uint32_t)task_exit;   /* if entry() returns — popped by entry's ret */
    *(--sp) = (uint32_t)entry;       /* popped by context_switch's final ret       */

    t->page_directory = current_page_directory;
    t->kernel_stack   = (uint32_t)(t->stack + TASK_STACK_SIZE);

    t->ctx.esp    = (uint32_t)sp;     /* points to entry — ret pops it as EIP      */
    t->ctx.eip    = (uint32_t)entry;  /* informational; not used by context_switch  */
    t->ctx.eflags = 0x202;            /* IF=1, reserved bit 1 always set            */
    /* edi/esi/ebp/ebx/edx/eax = 0 from memset — restored from ctx by context_switch */

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
    t->cpu   = -1;
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
    
    // Note: context_switch uses 'ret' to jump to the new task,
    // so we MUST push the entry point (jump_usermode) onto the stack!
    extern void jump_usermode(void);
    *(--sp) = (uint32_t)jump_usermode;

    t->ctx.esp    = (uint32_t)sp;
    t->ctx.eip    = (uint32_t)jump_usermode; // Not strictly needed anymore, but keep for debug
    t->ctx.eflags = 0x202;

    t->affinity  = -1; /* allow scheduler to run on any AP core */
    task_count_n++;
    return (int)t->id;
}

extern void clone_context(cpu_context_t* ctx);

int task_fork(struct registers* regs) {
    lock_sched();
    task_t* parent = task_current();
    task_t* child = alloc_slot();
    if (!child) { unlock_sched(); return -1; }
    /* Ensure child starts with clean signal state */
    child->pending_signals    = 0;
    child->in_signal_handler  = 0;

    memcpy(child, parent, sizeof(task_t));

    child->id = next_id++;
    child->parent_id = parent->id;
    child->state = TASK_READY;
    child->cpu   = -1;
    child->affinity = parent->affinity;
    child->page_directory = paging_clone_directory();

    uint32_t offset = (uint32_t)child->stack - (uint32_t)parent->stack;
    child->kernel_stack += offset;

    clone_context(&child->ctx);

    if (task_current() == parent) {
        child->ctx.esp += offset;
        child->ctx.ebp += offset; // Assuming EBP is on the stack
        
        struct registers* child_regs = (struct registers*)((uint32_t)regs + offset);
        child_regs->eax = 0; // Return 0 in child
        
        task_count_n++;
        unlock_sched();
        return child->id;
    } else {
        // We are the child!
        return 0;
    }
}

int task_waitpid(int pid) {
    // Check if the child exists and is not dead
    int found = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].id == (uint32_t)pid && tasks[i].state != TASK_DEAD) {
            found = 1;
            break;
        }
    }
    if (!found) return -1; // No such task or already dead

    lock_sched();
    tasks[current_tasks[apic_get_id()]].state = TASK_SLEEPING;
    tasks[current_tasks[apic_get_id()]].wait_pid = pid;
    unlock_sched();
    
    schedule();
    
    return 0;
}

/* Called by a task when it wants to stop. */
void task_exit(void) {
    lock_sched();
    uint8_t core_id = apic_get_id();
    uint32_t my_id = tasks[current_tasks[core_id]].id;
    tasks[current_tasks[core_id]].state = TASK_DEAD;
    task_count_n--;
    
    // Wake up anyone waiting for us
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && tasks[i].wait_pid == my_id) {
            tasks[i].state = TASK_READY;
            tasks[i].wait_pid = 0;
        }
    }
    
    unlock_sched();
    schedule(); /* yield — never returns to this task */
    for (;;) __asm__ volatile("hlt");
}

/* Sleep for approximately ms milliseconds (rounded to 10 ms ticks). */
void task_sleep(uint32_t ms) {
    uint32_t ticks = (ms * 100) / 1000; /* convert ms to 100 Hz ticks */
    if (!ticks) ticks = 1;
    lock_sched();
    tasks[current_tasks[apic_get_id()]].state       = TASK_SLEEPING;
    tasks[current_tasks[apic_get_id()]].sleep_ticks = ticks;
    unlock_sched();
    schedule();
}

/*
 * Called from the PIT IRQ0 handler every tick (100 Hz).
 * Decrements sleep counters and wakes tasks whose time has come.
 */
void task_tick(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING) {
            if (tasks[i].sleep_ticks > 0) {
                // simple atomic decrement (not strictly safe without lock, but OK for tick)
                tasks[i].sleep_ticks--;
            }
            if (tasks[i].sleep_ticks == 0)
                tasks[i].state = TASK_READY;
        }
    }
    extern void uhci_poll(void);
    uhci_poll();
}

/*
 * Round-robin scheduler.
 * Picks the next READY task and switches to it.
 */
void schedule(void) {
    lock_sched();
    int core_id = apic_get_id();
    int old = current_tasks[core_id];

    /* Mark current as ready (unless it's sleeping or dead) */
    if (old != -1 && tasks[old].state == TASK_RUNNING) {
        tasks[old].state = TASK_READY;
        tasks[old].cpu = -1;
    }

    /* Find next READY task (round-robin) */
    int next = -1;
    int start = (old == -1) ? 0 : old;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int candidate = (start + i) % MAX_TASKS;
        // We can pick it if it's READY and (unassigned to any running cpu) and (has no affinity OR matches our core)
        if (tasks[candidate].state == TASK_READY && tasks[candidate].cpu == -1 &&
            (tasks[candidate].affinity == -1 || tasks[candidate].affinity == core_id)) {
            next = candidate;
            break;
        }
    }

    // If we're already running the chosen task, do nothing
    if (next == old) {
        unlock_sched();
        return;
    }

    /* No other ready task */
    if (next == -1) {
        if (old != -1 && tasks[old].state == TASK_READY) {
            tasks[old].state = TASK_RUNNING;
            tasks[old].cpu = core_id;
            unlock_sched();
            return; /* no switch needed */
        }
        
        // If we reach here, we MUST switch to an idle task bound to us.
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_READY && tasks[i].cpu == -1 && tasks[i].affinity == core_id) {
                next = i;
                break;
            }
        }
        
        if (next == -1) {
            // No idle task found? This is a fatal error or we are booting.
            if (old == -1) {
                unlock_sched();
                return; // Nothing to run yet. Return to ap_main's idle loop.
            }
            unlock_sched();
            terminal_printf("\n[FATAL] Core %d has no ready tasks and no idle task!\n", core_id);
            for (;;) asm("cli; hlt");
        }
    }

    current_tasks[core_id] = next;
    tasks[next].cpu = core_id;
    tasks[next].state = TASK_RUNNING;

    // Switch Address Space
    if (tasks[next].page_directory && tasks[next].page_directory != current_page_directory) {
        if (tasks[next].id == 2 || tasks[next].id == 5) {
            extern void com1_print(const char*);
            com1_print("Switching to test task!\n");
        }
        
        paging_switch_directory(tasks[next].page_directory);
    }
    
    tss_set_stack(core_id, 0x10, tasks[next].kernel_stack);

    unlock_sched();
    
    static cpu_context_t dummy_ctx[MAX_CORES];
    cpu_context_t* old_ctx = (old != -1) ? &tasks[old].ctx : &dummy_ctx[core_id];
    
    context_switch(old_ctx, &tasks[next].ctx);
}

void task_check_signals(struct registers* regs) {
    if ((regs->cs & 0x3) != 3) return; // Only deliver signals when returning to Userspace
    
    task_t* cur = task_current();
    if (!cur || cur->state == TASK_DEAD) return;
    
    if (cur->pending_signals && !cur->in_signal_handler) {
        int sig = -1;
        for (int i=0; i<32; i++) {
            if (cur->pending_signals & (1<<i)) { sig = i; break; }
        }
        if (sig == -1) return;
        
        cur->pending_signals &= ~(1<<sig);
        
        uint32_t handler = cur->sig_handlers[sig];
        if (handler == 0) {
            // Default action (kill)
            task_exit();
        } else {
            // Inject trampoline
            cur->in_signal_handler = 1;
            
            // Build the signal frame on the user stack
            uint32_t* ustack = (uint32_t*)regs->useresp;
            
            // Push values (downwards)
            ustack--; *ustack = regs->useresp; // old ESP
            ustack--; *ustack = regs->eflags;  // old EFLAGS
            ustack--; *ustack = regs->eip;     // old EIP
            
            // Push a fake return address that points to a syscall (sys_sigreturn)
            // But wait, how does the user return? 
            // In Linux, the kernel provides a restorer on the stack or vDSO.
            // We can just push an invalid address or let the user call sigreturn explicitly?
            // Actually, we can push the syscall instruction itself on the stack!
            // int 0x80 is 0x80CD. Let's push a small machine code snippet.
            // ustack is uint32_t, so we can write 0x000080CD (int 0x80; ud2)
            // But NX bit might prevent execution on stack. 
            // The simplest way in ElseaOS: require the user handler to call `sys_sigreturn()`.
            // We will just pass `sig` as the first argument.
            ustack--; *ustack = sig; // Argument 1
            ustack--; *ustack = 0xDEADC0DE; // Fake return address
            
            regs->useresp = (uint32_t)ustack;
            regs->eip = handler;
        }
    }
}
