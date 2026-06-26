#include <stdint.h>
// Standalone ELF Application for ElseaOS
// This code runs entirely separate from the kernel.
// Because it runs in kernel space (for now), we can directly call kernel functions!
// Wait! If it's a completely standalone ELF, how does it know the address of kernel functions?
// It doesn't! We must either pass a pointer to a syscall table, or use INT 0x80.
// Let's use INT 0x80 (syscall).

void syscall_print(const char* msg) {
    __asm__ volatile ("int $0x80" : : "a"(0), "b"(msg));
}

int syscall_fork(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(2));
    return ret;
}

int syscall_exec(const char* path) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(3), "b"(path));
    return ret;
}

void _start() {
    syscall_print("\n");
    syscall_print("\033[36m========================================\033[0m\n");
    syscall_print("  \033[32mHELLO FROM MULTI-THREADED USER SPACE!\033[0m \n");
    syscall_print("\033[36m========================================\033[0m\n");

    syscall_print("Calling fork()...\n");
    int pid = syscall_fork();
    
    if (pid == 0) {
        syscall_print("  -> I am the CHILD process!\n");
        syscall_print("  -> Exec-ing calc.elf...\n");
        syscall_exec("calc.elf");
        syscall_print("  -> Exec failed!\n"); // Should not be reached
    } else {
        syscall_print("  -> I am the PARENT process! Child PID is: ");
        // Quick hex print because we don't have printf in user space
        char num[2] = { pid + '0', 0 };
        syscall_print(num);
        syscall_print("\n");
    }
    
    // Tell task scheduler to exit this thread
    __asm__ volatile ("int $0x80" : : "a"(1));
    
    while (1) {}
}
