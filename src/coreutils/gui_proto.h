/* gui_proto.h — Shared IPC protocol between ElseaOS compositor and apps.
 * Both compositor and apps include this header. */
#pragma once

#define GUI_QUEUE_KEY   1000  /* compositor's message queue key */

/* Message types compositor ← app */
#define GUI_MSG_CONNECT  1   /* app → comp: I want a window */
#define GUI_MSG_FLUSH    2   /* app → comp: please repaint my window */
#define GUI_MSG_CLOSE    3   /* app → comp: closing my window */

/* Message types compositor → app */
#define GUI_MSG_READY   10   /* comp → app: here is your canvas shmid */
#define GUI_MSG_KEY     20   /* comp → app: key event */
#define GUI_MSG_CLICK   30   /* comp → app: mouse click inside your window */

#define GUI_MAX_TITLE   32
#define GUI_MAX_WINDOWS 8

struct gui_msg {
    long    mtype;          /* IPC message type (= MSG_* above) */
    int     win_id;         /* window ID */
    int     app_qid;        /* app's own queue ID (for reverse messages) */
    int     x, y, w, h;    /* window geometry */
    char    title[GUI_MAX_TITLE];
    int     shmid;          /* shared canvas shmid */
    int     key;            /* keycode (for GUI_MSG_KEY) */
    int     mouse_x, mouse_y, mouse_btn;
};

/* Inline syscall helpers — usable from both compositor and apps */
static inline int gui_msgget(int key, int flags) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(26), "b"(key), "c"(flags));
    return ret;
}

static inline int gui_msgsnd(int qid, const struct gui_msg* m) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
        : "a"(42), "b"(qid), "c"(m), "d"(sizeof(*m) - sizeof(long)));
    return ret;
}

/* Non-blocking receive. Returns -1 if no message of that type. */
static inline int gui_msgrcv(int qid, struct gui_msg* m, long mtype) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
        : "a"(43), "b"(qid), "c"(m), "d"(sizeof(*m) - sizeof(long)), "S"(mtype));
    return ret;
}

static inline int gui_shmget(int key, unsigned int size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(29), "b"(key), "c"(size), "d"(0666));
    return ret;
}

static inline void* gui_shmat(int shmid) {
    void* ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(30), "b"(shmid), "c"(0));
    return ret;
}

static inline int gui_spawn(const char* path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(40), "b"(path), "c"(0));
    return ret;
}

static inline void gui_yield(void) {
    __asm__ volatile("int $0x80" : : "a"(14), "b"(0));
}
