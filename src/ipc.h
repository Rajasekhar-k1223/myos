#pragma once
#include <stdint.h>

#define IPC_MAX_QUEUES 8
#define IPC_MAX_MSG    16
#define IPC_MSG_SIZE   256

typedef struct {
    long   mtype;
    char   mtext[IPC_MSG_SIZE];
} ipc_msg_t;

typedef struct {
    int       key;
    int       used;
    ipc_msg_t msgs[IPC_MAX_MSG];
    int       head, tail, count;
} ipc_queue_t;

/* IPC control commands */
#define IPC_RMID  0

void ipc_init(void);
int  ipc_msgget(int key, int flags);
int  ipc_msgsnd(int qid, const ipc_msg_t* msg, uint32_t size, int flags);
int  ipc_msgrcv(int qid, ipc_msg_t* msg, uint32_t size, long mtype, int flags);
int  ipc_msgctl(int qid, int cmd, void* buf);

/* Shared memory */
#define SHM_MAX_SEGS 8
#define SHM_SEG_SIZE 4096

int   shm_get(int key, uint32_t size, int flags);
void* shm_at(int shmid, int flags);
int   shm_dt(void* addr);
int   shm_ctl(int shmid, int cmd);

/* Listing helper for ipcs command */
void ipc_print_all(void);
