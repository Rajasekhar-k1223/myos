#include "pipe.h"
#include "task.h"

int pipe_create(pipe_t* p) {
    if (!p) return -1;
    p->head = 0;
    p->tail = 0;
    p->read_open = 1;
    p->write_open = 1;
    return 0;
}

static uint32_t pipe_available_read(pipe_t* p) {
    return p->head - p->tail;
}

static uint32_t pipe_available_write(pipe_t* p) {
    return PIPE_BUF_SIZE - (p->head - p->tail);
}

int pipe_read(pipe_t* p, void* buf, size_t count) {
    if (!p || !buf) return -1;
    uint8_t* b = (uint8_t*)buf;
    size_t read_bytes = 0;

    while (read_bytes < count) {
        if (pipe_available_read(p) == 0) {
            if (!p->write_open) break; // EOF
            // Block and wait for data (yield CPU)
            task_sleep(10); // simple yield
            continue;
        }
        b[read_bytes++] = p->buffer[p->tail % PIPE_BUF_SIZE];
        p->tail++;
    }

    return read_bytes;
}

int pipe_write(pipe_t* p, const void* buf, size_t count) {
    if (!p || !buf) return -1;
    const uint8_t* b = (const uint8_t*)buf;
    size_t written_bytes = 0;

    while (written_bytes < count) {
        if (!p->read_open) return -1; // SIGPIPE equivalent

        if (pipe_available_write(p) == 0) {
            // Block and wait for space (yield CPU)
            task_sleep(10);
            continue;
        }
        p->buffer[p->head % PIPE_BUF_SIZE] = b[written_bytes++];
        p->head++;
    }

    return written_bytes;
}

void pipe_close_read(pipe_t* p) {
    if (p) p->read_open = 0;
}

void pipe_close_write(pipe_t* p) {
    if (p) p->write_open = 0;
}
