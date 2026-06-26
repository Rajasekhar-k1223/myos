.section .note.GNU-stack,"",@progbits

.section .text

/*
 * void context_switch(cpu_context_t* old, cpu_context_t* new);
 *
 * Save current CPU registers into *old, then restore from *new.
 * cpu_context_t layout (must match task.h):
 *   +0  edi
 *   +4  esi
 *   +8  ebp
 *   +12 esp  (saved ESP value, not used on restore — restored via stack)
 *   +16 ebx
 *   +20 edx
 *   +24 ecx
 *   +28 eax
 *   +32 eip
 *   +36 eflags
 */
.global context_switch
context_switch:
    /* Arguments: [esp+4] = old ctx ptr, [esp+8] = new ctx ptr */
    mov  4(%esp),  %eax      /* eax = old */
    mov  8(%esp),  %ecx      /* ecx = new */

    /* ── Save old task ── */
    mov  %edi,  0(%eax)
    mov  %esi,  4(%eax)
    mov  %ebp,  8(%eax)
    mov  %esp, 12(%eax)
    mov  %ebx, 16(%eax)
    mov  %edx, 20(%eax)
    /* skip ecx (used as new-ptr), save caller's ecx via the stack */
    mov  %eax, 28(%eax)      /* save eax (will be overwritten on restore) */
    /* save return address as EIP */
    mov  (%esp), %edx
    mov  %edx,  32(%eax)
    /* save EFLAGS */
    pushf
    pop  %edx
    mov  %edx,  36(%eax)

    /* ── Restore new task ── */
    mov   0(%ecx), %edi
    mov   4(%ecx), %esi
    mov   8(%ecx), %ebp
    mov  16(%ecx), %ebx
    mov  20(%ecx), %edx
    /* restore EFLAGS */
    push 36(%ecx)
    popf
    /* restore stack pointer */
    mov  12(%ecx), %esp
    /* jump to saved EIP (return into the new task's caller) */
    jmp *32(%ecx)

.global jump_usermode
jump_usermode:
    /* context_switch restored esp to our kernel stack, and jumped here */
    /* We placed an IRET frame on the stack: SS, ESP, EFLAGS, CS, EIP */
    /* To cleanly jump to Ring 3, we just execute iret */
    iret
