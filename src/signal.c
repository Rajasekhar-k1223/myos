#include "signal.h"
#include "task.h"
#include "kernel.h"
#include <stdint.h>

/* Per-task signal state. Indexed by task slot (not PID). */
static uint32_t    pending[MAX_TASKS];               /* bitmask of pending signals */
static sighandler_t handlers[MAX_TASKS][32];         /* per-signal handler per task */

void signal_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        pending[i] = 0;
        for (int s = 0; s < 32; s++)
            handlers[i][s] = SIG_DFL;
    }
}

/* Find the task slot whose .id == pid. Returns -1 if not found. */
static int find_slot_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_DEAD && tasks[i].id == pid)
            return i;
    }
    return -1;
}

void signal_send(uint32_t pid, int signum) {
    if (signum <= 0 || signum >= 32) return;
    int slot = find_slot_by_pid(pid);
    if (slot < 0) return;
    pending[slot] |= (1u << signum);
}

void signal_set_handler(uint32_t pid, int signum, sighandler_t handler) {
    if (signum <= 0 || signum >= 32) return;
    int slot = find_slot_by_pid(pid);
    if (slot < 0) return;
    handlers[slot][signum] = handler;
}

/*
 * Called from task_tick() each PIT tick.
 * Delivers any pending signals to the currently-running task.
 * SIGKILL / SIGTERM with SIG_DFL → task_exit().
 * SIGCHLD with SIG_DFL → ignore.
 * User-installed handler → call it.
 */
void signal_dispatch(void) {
    task_t* t = task_current();
    if (!t || t->state == TASK_DEAD) return;

    /* Find this task's slot index */
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (&tasks[i] == t) { slot = i; break; }
    }
    if (slot < 0) return;

    uint32_t mask = pending[slot];
    if (!mask) return;

    for (int sig = 1; sig < 32; sig++) {
        if (!(mask & (1u << sig))) continue;

        /* Clear the pending bit */
        pending[slot] &= ~(1u << sig);

        sighandler_t h = handlers[slot][sig];

        if (h == SIG_IGN) {
            /* explicitly ignored */
            continue;
        }

        if (h != SIG_DFL) {
            /* Deliver via userspace trampoline: queue into task_t so task_check_signals()
             * injects the proper stack frame on next return-to-user. */
            t->pending_signals |= (1u << sig);
            if (sig < 32) t->sig_handlers[sig] = (uint32_t)(uintptr_t)h;
            continue;
        }

        /* Default actions */
        switch (sig) {
            case SIGKILL:
            case SIGTERM:
                terminal_printf("[signal] pid %u killed by signal %d\n", t->id, sig);
                task_exit();
                /* never reached */
                break;
            case SIGCHLD:
                /* Default: ignore */
                break;
            case SIGHUP:
                terminal_printf("[signal] pid %u hangup (signal %d)\n", t->id, sig);
                task_exit();
                break;
            default:
                /* Default for unknown signals: ignore */
                break;
        }
    }
}
