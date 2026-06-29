sed -i 's/int close/int close(int fd) { return syscall1(10, fd); } \nint close_dummy/' src/libc/libc.c
