#pragma once
#include <stdint.h>

/* Signal numbers */
#define SIGHUP    1
#define SIGKILL   9
#define SIGTERM  15
#define SIGCHLD  17

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

void     signal_init(void);
void     signal_send(uint32_t pid, int signum);
void     signal_set_handler(uint32_t pid, int signum, sighandler_t handler);
void     signal_dispatch(void);   /* called from task_tick() in task.c */
