// Standalone ELF Application for ElseaOS
// This code runs entirely separate from the kernel.
// Because it runs in kernel space (for now), we can directly call kernel functions!
// Wait! If it's a completely standalone ELF, how does it know the address of kernel functions?
// It doesn't! We must either pass a pointer to a syscall table, or use INT 0x80.
// Let's use INT 0x80 (syscall).

void syscall_print(const char* msg) {
    __asm__ volatile ("int $0x80" : : "a"(0), "b"(msg));
}

void _start() {
    syscall_print("\n");
    syscall_print("\033[36m========================================\033[0m\n");
    syscall_print("  \033[32mHELLO FROM DYNAMICALLY LOADED ELF!\033[0m    \n");
    syscall_print("\033[36m========================================\033[0m\n");
    
    // Tell task scheduler to exit this thread
    __asm__ volatile ("int $0x80" : : "a"(1));
    
    while (1) {}
}
