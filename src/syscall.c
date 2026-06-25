#include "syscall.h"
#include "idt.h"
#include "kernel.h"

// System call handler
static void syscall_handler(struct registers* regs) {
    // Syscall number is passed in EAX
    // Arguments are passed in EBX, ECX, EDX, ESI, EDI
    uint32_t syscall_no = regs->eax;

    switch (syscall_no) {
        case 0: { // sys_print
            const char* msg = (const char*)regs->ebx;
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
            terminal_writestring("[Syscall] ");
            terminal_writestring(msg);
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            break;
        }
        default:
            terminal_writestring("Unknown syscall: ");
            terminal_writedec(syscall_no);
            terminal_writestring("\n");
            break;
    }
}

void syscall_init(void) {
    register_interrupt_handler(128, syscall_handler);
}
