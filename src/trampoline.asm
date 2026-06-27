[BITS 16]
[ORG 0x8000]

trampoline_entry:
    cli
    
    ; Load data segments with 0 (since we are at 0x0000:0x8000)
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    
    ; Load the temporary GDT
    lgdt [gdt_desc]
    
    ; Switch to protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to clear prefetch queue and set CS to 32-bit segment
    jmp 0x08:protected_mode

[BITS 32]
protected_mode:
    ; We are now in 32-bit protected mode!
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; The BSP will write the AP's stack pointer into the `ap_stack` variable
    mov esp, [ap_stack]
    
    ; The BSP will write the address of ap_main into `ap_main_ptr`
    mov eax, [ap_main_ptr]
    call eax
    
halt_loop:
    cli
    hlt
    jmp halt_loop

align 16
gdt_start:
    dq 0x0 ; Null
    dq 0x00cf9a000000ffff ; Code 32-bit
    dq 0x00cf92000000ffff ; Data 32-bit
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

ap_stack:
    dd 0

ap_main_ptr:
    dd 0
