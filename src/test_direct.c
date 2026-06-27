void _start() {
    const char* msg = "Direct syscall test!\n";
    asm volatile (
        "int $0x80"
        : 
        : "a"(19), "b"(2), "c"((unsigned int)msg), "d"(21)
        : "memory"
    );
    while(1);
}
