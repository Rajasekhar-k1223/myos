typedef unsigned int size_t;
int pipe(int pipefd[2]);
int dup2(int oldfd, int newfd);
int fork(void);
void exit(int status);
int read(int fd, void* buf, size_t count);
int write(int fd, const void* buf, size_t count);
int close(int fd);
int printf(const char* format, ...);
int waitpid(int pid);

void _start() {
    write(2, "test_pipe starting\n", 19);
    int p[2];
    if (pipe(p) < 0) {
        printf("Failed to create pipe\n");
        return -1;
    }
    
    int pid = fork();
    if (pid == 0) {
        // Child
        // close(p[0]); // close read end
        dup2(p[1], 1); // stdout -> pipe write
        write(2, "child writing\n", 14);
        const char* msg = "Hello from child pipe!\n";
        write(1, msg, 23);
        write(2, "child exiting\n", 14);
        exit(0);
    } else {
        write(2, "parent waiting\n", 15);
        // Parent
        // close(p[1]); // close write end
        char buf[64];
        int n = read(p[0], buf, sizeof(buf) - 1);
        if (n >= 0) {
            buf[n] = '\0';
            printf("Parent read: %s", buf);
        } else {
            printf("Parent read failed\n");
        }
        waitpid(pid);
    }
    exit(0);
}
