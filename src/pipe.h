#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>
#include <stddef.h>

#define PIPE_BUF_SIZE 4096

typedef struct {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    int read_open;
    int write_open;
} pipe_t;

// Returns 0 on success, -1 on error
// Initializes the pipe structure
int pipe_create(pipe_t* p);

// Reads up to count bytes from pipe to buf
int pipe_read(pipe_t* p, void* buf, size_t count);

// Writes up to count bytes from buf to pipe
int pipe_write(pipe_t* p, const void* buf, size_t count);

void pipe_close_read(pipe_t* p);
void pipe_close_write(pipe_t* p);

#endif // PIPE_H
