/* Mark stack as non-executable to silence linker warning */
.section .note.GNU-stack,"",@progbits

.section .text
/*
 * ISR and IRQ stub trampolines.
 * Each stub pushes a dummy error code (if the CPU doesn't) and the interrupt
 * number, then jumps to a common handler that saves full context and calls C.
 */

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    push $0
    push $\num
    jmp  isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    push $\num
    jmp  isr_common_stub
.endm

.macro IRQ num, vec
.global irq\num
irq\num:
    push $0
    push $\vec
    jmp  irq_common_stub
.endm

/* CPU exceptions 0-31 (error code marked per Intel manual) */
ISR_NOERRCODE 0   /* #DE Division Error              */
ISR_NOERRCODE 1   /* #DB Debug                       */
ISR_NOERRCODE 2   /*     NMI                         */
ISR_NOERRCODE 3   /* #BP Breakpoint                  */
ISR_NOERRCODE 4   /* #OF Overflow                    */
ISR_NOERRCODE 5   /* #BR Bound Range Exceeded        */
ISR_NOERRCODE 6   /* #UD Invalid Opcode              */
ISR_NOERRCODE 7   /* #NM Device Not Available        */
ISR_ERRCODE   8   /* #DF Double Fault                */
ISR_NOERRCODE 9   /*     Coprocessor Segment Overrun */
ISR_ERRCODE   10  /* #TS Invalid TSS                 */
ISR_ERRCODE   11  /* #NP Segment Not Present         */
ISR_ERRCODE   12  /* #SS Stack-Segment Fault         */
ISR_ERRCODE   13  /* #GP General Protection Fault    */
ISR_ERRCODE   14  /* #PF Page Fault                  */
ISR_NOERRCODE 15  /*     Reserved                    */
ISR_NOERRCODE 16  /* #MF x87 FP Exception            */
ISR_ERRCODE   17  /* #AC Alignment Check             */
ISR_NOERRCODE 18  /* #MC Machine Check               */
ISR_NOERRCODE 19  /* #XM SIMD FP Exception           */
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30  /* #SX Security Exception          */
ISR_NOERRCODE 31

/* Hardware IRQs remapped to vectors 32-47 */
IRQ  0, 32   /* PIT timer    */
IRQ  1, 33   /* PS/2 keyboard */
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/*
 * Common stub for CPU exceptions.
 *
 * Stack on entry (low → high address):
 *   [int_no] [err_code] [eip] [cs] [eflags]
 *
 * After pusha + push ds:
 *   [ds] [edi] [esi] [ebp] [esp] [ebx] [edx] [ecx] [eax]
 *   [int_no] [err_code] [eip] [cs] [eflags]
 *
 * This matches struct registers in idt.h exactly.
 * cdecl: the struct starts at [esp+4] after the call — correct for by-value arg.
 */
.extern isr_handler
isr_common_stub:
    pusha
    mov %ds, %ax
    push %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    call isr_handler
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa
    add $8, %esp   /* discard int_no + err_code */
    iret

.extern irq_handler
irq_common_stub:
    pusha
    mov %ds, %ax
    push %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    call irq_handler
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa
    add $8, %esp
    iret
