#include <stddef.h>

extern int printf(const char* fmt, ...);
extern int open(const char* path, int flags);
extern int read(int fd, void* buf, size_t count);
extern int close(int fd);

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("cat: %s: No such file or directory\n", argv[i]);
            continue;
        }
        
        char buf[256];
        int n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            // Write to stdout (fd 1) via sys_write_buf (printf for now, but handle \0)
            // Wait, we can use a custom write or just loop
            for (int j=0; j<n; j++) {
                printf("%c", buf[j]);
            }
        }
        close(fd);
    }
    return 0;
}
