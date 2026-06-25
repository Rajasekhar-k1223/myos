#include "user.h"
#include <stdint.h>

// This is the actual system call wrapper that runs in Ring 3
static void syscall_print(const char* msg) {
    __asm__ volatile ("int $0x80" : : "a"(0), "b"(msg));
}

// This function runs in Ring 3 (User Mode)
void user_app_main(void) {
    syscall_print("Hello from User Space! Syscall INT 0x80 successful!\n");
    
    // We cannot return from here, and we can't halt (privileged).
    // So we just spin in an infinite loop.
    while(1);
}

// We jump to Ring 3 using IRET
void enter_user_mode(void) {
    __asm__ volatile(
        "mov $0x23, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        
        "mov %esp, %eax\n"
        "pushl $0x23\n"
        "pushl %eax\n"
        "pushf\n"
        "pushl $0x1B\n"
        "push $user_app_main\n"
        "iret\n"
    );
}
