#pragma once
#include <stdint.h>

/* Stack frame pushed by the CPU + our ISR stubs. */
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha order */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;            /* pushed by CPU */
};

typedef void (*isr_t)(struct registers*);

void idt_init(void);
void register_interrupt_handler(uint8_t n, isr_t handler);
