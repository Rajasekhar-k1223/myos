// libc.c - Minimal C runtime for ElseaOS user-mode programs
// Provides syscall wrappers, stdio, stdlib, string functions
// All output goes through syscall 7 (sys_write_buf) - a length-aware print

typedef unsigned int  size_t;
typedef unsigned int  uint32_t;

// ─── Syscall stubs ───────────────────────────────────────────────────────────
static inline unsigned int syscall1(unsigned int num, unsigned int a1) {
    unsigned int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1));
    return ret;
}
static inline unsigned int syscall2(unsigned int num, unsigned int a1, unsigned int a2) {
    unsigned int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2));
    return ret;
}
static inline unsigned int syscall3(unsigned int num, unsigned int a1, unsigned int a2, unsigned int a3) {
    unsigned int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3));
    return ret;
}
static inline unsigned int syscall0(unsigned int num) {
    unsigned int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num));
    return ret;
}

// ─── string.h ────────────────────────────────────────────────────────────────
void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest; const char* s = (const char*)src;
    while (n--) *d++ = *s++;
    return dest;
}
void* memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    while (n--) *p++ = (char)c;
    return s;
}
int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    while (n--) { if (*a != *b) return *a - *b; a++; b++; }
    return 0;
}
size_t strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}
char* strcpy(char* d, const char* s) {
    char* r = d; while ((*d++ = *s++)); return r;
}
char* strncpy(char* d, const char* s, size_t n) {
    char* r = d;
    while (n-- && (*d++ = *s++));
    while (n-- > 0) *d++ = 0;
    return r;
}
char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    if (c == 0) return (char*)s;
    return 0;
}
char* strstr(const char* haystack, const char* needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return (char*)haystack;
    while (*haystack) {
        if (!memcmp(haystack, needle, nlen)) return (char*)haystack;
        haystack++;
    }
    return 0;
}
int strtol(const char* str, char** endptr, int base) {
    int res = 0; int sign = 1;
    while (*str == ' ') str++;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    while (*str) {
        int v = -1;
        if (*str >= '0' && *str <= '9') v = *str - '0';
        else if (*str >= 'a' && *str <= 'z') v = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') v = *str - 'A' + 10;
        if (v < 0 || v >= base) break;
        res = res * base + v;
        str++;
    }
    if (endptr) *endptr = (char*)str;
    return res * sign;
}

// ─── stdlib.h (Memory Allocation) ─────────────────────────────────────────────
void exit(int status) {
    syscall1(1, (unsigned int)status);
    while (1);
}

typedef struct block_meta {
    size_t size;
    int free;
    struct block_meta *next;
} block_meta_t;

#define META_SIZE sizeof(block_meta_t)
static block_meta_t *global_base = 0;

static block_meta_t *request_space(block_meta_t* last, size_t size) {
    // We use sys_sbrk (syscall 13) to expand heap
    void *request = (void*)syscall1(13, size + META_SIZE);
    if ((int)request == -1 || request == 0) return 0;
    
    block_meta_t *block = (block_meta_t*)request;
    block->size = size;
    block->free = 0;
    block->next = 0;
    if (last) last->next = block;
    return block;
}

static block_meta_t *find_free_block(block_meta_t **last, size_t size) {
    block_meta_t *current = global_base;
    while (current && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }
    return current;
}

void *malloc(size_t size) {
    if (size <= 0) return 0;
    
    // Align to 8 bytes
    size = (size + 7) & ~7u;
    
    block_meta_t *block;
    if (!global_base) {
        block = request_space(0, size);
        if (!block) return 0;
        global_base = block;
    } else {
        block_meta_t *last = global_base;
        block = find_free_block(&last, size);
        if (!block) {
            block = request_space(last, size);
            if (!block) return 0;
        } else {
            block->free = 0;
        }
    }
    return (void*)(block + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_meta_t *block = ((block_meta_t*)ptr) - 1;
    block->free = 1;
}

// ─── I/O — syscall 7 = sys_write_buf(buf, len) ──────────────────────────────
// We use a dedicated length-aware write syscall so we can pass stack buffers
// without null-termination risk.
static void write_buf(const char* buf, size_t len) {
    if (!buf || len == 0) return;
    syscall2(7, (unsigned int)buf, (unsigned int)len);
}

static void write_str(const char* s) {
    if (!s) return;
    write_buf(s, strlen(s));
}

// ─── File descriptors ─────────────────────────────────────────────────────────

int open(const char* path, int flags) {
    return syscall2(8, (unsigned int)path, (unsigned int)flags);
}

int read(int fd, void* buf, size_t count) {
    return (int)syscall3(9, (unsigned int)fd, (unsigned int)buf, (unsigned int)count);
}

int write(int fd, const void* buf, size_t count) {
    return (int)syscall3(19, (unsigned int)fd, (unsigned int)buf, (unsigned int)count);
}

int close(int fd) { return syscall1(10, fd); } 
int close_dummy(int fd) {
    return 0;
}

// ─── stdio.h file streams ───────────────────────────────────────────────────
#define MAX_FDS 16
typedef struct { int fd; } FILE;
static FILE _files[MAX_FDS];

FILE* fopen(const char* filename, const char* mode) {
    int flags = 0; // Simple dummy flag
    if (mode[0] == 'w') flags = 1;
    int fd = open(filename, flags);
    if (fd < 0) return 0;
    _files[fd].fd = fd;
    return &_files[fd];
}

size_t fread(void* ptr, size_t size, size_t count, FILE* stream) {
    if (!stream) return 0;
    int bytes = read(stream->fd, ptr, size * count);
    if (bytes < 0) return 0;
    return bytes / size;
}

int fclose(FILE* stream) {
    if (!stream) return -1;
    return close(stream->fd);
}

int fseek(FILE* stream, long offset, int whence) {
    // Dummy fseek for now, myos doesn't support seek syscall yet
    (void)stream; (void)offset; (void)whence;
    return 0;
}

long ftell(FILE* stream) {
    (void)stream;
    return 0;
}

int fputc(int ch, FILE* stream) {
    if (!stream) return -1;
    unsigned char c = (unsigned char)ch;
    if (write(stream->fd, &c, 1) != 1) return -1;
    return ch;
}

int fgetc(FILE* stream) {
    if (!stream) return -1;
    unsigned char c;
    if (read(stream->fd, &c, 1) != 1) return -1;
    return c;
}

// ─── stdio.h — printf ────────────────────────────────────────────────────────
static void fmt_uint(unsigned int n, int base, char* out, int* olen) {
    static const char hex[] = "0123456789abcdef";
    char tmp[16]; int i = 0;
    if (n == 0) { tmp[i++] = '0'; }
    while (n) { tmp[i++] = hex[n % base]; n /= base; }
    // reverse
    for (int j = i - 1; j >= 0; j--) out[(*olen)++] = tmp[j];
}

int printf(const char* fmt, ...) {
    // Build output into a stack buffer for efficiency
    char out[1024]; int olen = 0;

    // va_list manually for 32-bit x86 cdecl
    unsigned int* ap = (unsigned int*)(&fmt) + 1;

    while (*fmt) {
        if (*fmt != '%') {
            if (olen < 1023) out[olen++] = *fmt;
            fmt++;
            continue;
        }
        fmt++; // skip '%'

        // Check for %.*s
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                fmt++;
                if (*fmt == 's') {
                    int len = (int)*ap++;
                    const char* s = (const char*)*ap++;
                    for (int i = 0; i < len && olen < 1023; i++) out[olen++] = s[i];
                    fmt++;
                    continue;
                }
            }
            // unknown, just skip
            fmt++;
            continue;
        }

        switch (*fmt) {
            case 'd': {
                int v = (int)*ap++;
                if (v < 0) { if (olen<1023) out[olen++]='-'; v=-v; }
                fmt_uint((unsigned int)v, 10, out, &olen);
                break;
            }
            case 'u': {
                unsigned int v = *ap++;
                fmt_uint(v, 10, out, &olen);
                break;
            }
            case 'x': {
                unsigned int v = *ap++;
                fmt_uint(v, 16, out, &olen);
                break;
            }
            case 's': {
                const char* s = (const char*)*ap++;
                if (!s) s = "(null)";
                while (*s && olen < 1023) out[olen++] = *s++;
                break;
            }
            case 'c': {
                char c = (char)*ap++;
                if (olen < 1023) out[olen++] = c;
                break;
            }
            case '%': {
                if (olen < 1023) out[olen++] = '%';
                break;
            }
            default: {
                if (olen < 1022) { out[olen++] = '%'; out[olen++] = *fmt; }
                break;
            }
        }
        fmt++;
    }

    if (olen > 0) write_buf(out, olen);
    return olen;
}

int fprintf(FILE* stream, const char* fmt, ...) {
    if (!stream) return -1;
    char out[1024]; int olen = 0;
    unsigned int* ap = (unsigned int*)(&fmt) + 1;
    while (*fmt) {
        if (*fmt != '%') {
            if (olen < 1023) out[olen++] = *fmt;
            fmt++;
            continue;
        }
        fmt++;
        switch (*fmt) {
            case 'd': {
                int v = (int)*ap++;
                if (v < 0) { if (olen<1023) out[olen++]='-'; v=-v; }
                fmt_uint((unsigned int)v, 10, out, &olen);
                break;
            }
            case 's': {
                const char* s = (const char*)*ap++;
                if (!s) s = "(null)";
                while (*s && olen < 1023) out[olen++] = *s++;
                break;
            }
            default: {
                if (olen < 1022) { out[olen++] = '%'; out[olen++] = *fmt; }
                break;
            }
        }
        fmt++;
    }
    if (olen > 0) write(stream->fd, out, olen);
    return olen;
}

// ─── Syscall Wrappers ────────────────────────────────────────────────────────
int pipe(int pipefd[2]) {
    return syscall1(17, (unsigned int)pipefd);
}
int dup2(int oldfd, int newfd) {
    return syscall2(18, (unsigned int)oldfd, (unsigned int)newfd);
}
void sleep(unsigned int ms) {
    syscall1(14, ms);
}
int fork(void) {
    return syscall0(2);
}
int waitpid(int pid) {
    return syscall1(12, (unsigned int)pid);
}

struct timeval {
    long tv_sec;
    long tv_usec;
};
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    unsigned int ms = syscall0(15);
    if (tv) {
        tv->tv_sec = ms / 1000;
        tv->tv_usec = (ms % 1000) * 1000;
    }
    return 0;
}

struct stat {
    unsigned int st_size;
};
int stat(const char *pathname, struct stat *statbuf) {
    if (!statbuf) return -1;
    unsigned int buf[4];
    int ret = syscall2(16, (unsigned int)pathname, (unsigned int)buf);
    if (ret == 0) statbuf->st_size = buf[0];
    return ret;
}

