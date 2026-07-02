#include "gui_proto.h"
#include <stddef.h>

/* Simple string length */
static int strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Copy string */
static void strcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static void print(const char* s) {
    unsigned int len = strlen(s);
    unsigned int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(7), "b"(s), "c"(len));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    print("[TERMINAL] Starting GUI Terminal...\n");
    
    /* Connect to Compositor */
    struct gui_msg m;
    m.mtype = GUI_MSG_CONNECT;
    m.win_id = -1; /* Request new */
    
    /* App creates its own queue to receive messages from compositor */
    int my_qid = gui_msgget(2000, 0666 | 01000); /* arbitrary key for now */
    m.app_qid = my_qid;
    m.x = 100;
    m.y = 80;
    m.w = 400;
    m.h = 300;
    strcpy(m.title, "Terminal");
    
    int comp_qid = gui_msgget(GUI_QUEUE_KEY, 0666);
    if (gui_msgsnd(comp_qid, &m) < 0) {
        print("[TERMINAL] Failed to send connect message to compositor.\n");
        return 1;
    }
    
    print("[TERMINAL] Waiting for compositor ready...\n");
    if (gui_msgrcv(my_qid, &m, GUI_MSG_READY) < 0) {
        print("[TERMINAL] Failed to receive ready message.\n");
        return 1;
    }
    
    print("[TERMINAL] Connected! Canvas ShmID received.\n");
    unsigned int* canvas = gui_shmat(m.shmid);
    if ((int)canvas == -1) {
        print("[TERMINAL] Failed to attach canvas.\n");
        return 1;
    }
    
    /* Fill canvas with black background */
    for (int y = 0; y < m.h; y++) {
        for (int x = 0; x < m.w; x++) {
            canvas[y * m.w + x] = 0xFF000000;
        }
    }
    
    /* Tell compositor to flush/redraw */
    m.mtype = GUI_MSG_FLUSH;
    gui_msgsnd(comp_qid, &m);
    
    /* Event loop */
    while (1) {
        struct gui_msg event;
        if (gui_msgrcv(my_qid, &event, 0) > 0) {
            if (event.mtype == GUI_MSG_KEY) {
                print("[TERMINAL] Key pressed!\n");
            } else if (event.mtype == GUI_MSG_CLICK) {
                print("[TERMINAL] Mouse clicked in window!\n");
            }
        } else {
            gui_yield();
        }
    }
    
    return 0;
}
