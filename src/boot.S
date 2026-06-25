.section .note.GNU-stack,"",@progbits

/* ── Multiboot2 Header ──────────────────────────────────────────────────────
 * Magic:        0xE85250D6
 * Architecture: 0 = i386 32-bit protected mode
 * The header must appear within the first 32 KB of the kernel image.
 * ───────────────────────────────────────────────────────────────────────── */
.section .multiboot2
.align 8

mb2_header_start:
    .long  0xE85250D6                                           /* magic */
    .long  0                                                    /* arch: i386 */
    .long  (mb2_header_end - mb2_header_start)                  /* header length */
    .long  -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start)) /* checksum */

    /* ── Tag: Framebuffer request (type 5) ── */
    .align 8
    .word  5          /* type */
    .word  1          /* flags: 1 = optional (GRUB will provide best available) */
    .long  20         /* size */
    .long  1024       /* preferred width  (GOP/VBE will try to match) */
    .long  768        /* preferred height */
    .long  32         /* preferred bits-per-pixel */

    /* ── Tag: End (type 0) ── */
    .align 8
    .word  0          /* type */
    .word  0          /* flags */
    .long  8          /* size */
mb2_header_end:

/* ── Stack ─────────────────────────────────────────────────────────────────── */
.section .bss
.align 16
stack_bottom:
.skip 16384           /* 16 KiB kernel stack */
stack_top:

/* ── Kernel entry point ─────────────────────────────────────────────────────
 * GRUB Multiboot2 ABI:
 *   EAX = 0x36d76289  (Multiboot2 bootloader magic)
 *   EBX = physical address of Multiboot2 info structure
 * ───────────────────────────────────────────────────────────────────────── */
.section .text
.global _start
.type   _start, @function
_start:
    mov  $stack_top, %esp   /* set up kernel stack */

    push %ebx               /* arg2: Multiboot2 info address */
    push %eax               /* arg1: magic */

    call kernel_main

    cli
.Lhalt:
    hlt
    jmp  .Lhalt

.size _start, . - _start
