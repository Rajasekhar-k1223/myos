.section .note.GNU-stack,"",@progbits

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax      /* 0x10 = kernel data selector (GDT entry 2 * 8) */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.Lflush /* 0x08 = kernel code selector (GDT entry 1 * 8) */
.Lflush:
    ret
