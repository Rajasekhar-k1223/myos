.section .note.GNU-stack,"",@progbits

/* Declare constants for the multiboot header. */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set VIDEO,    1<<2             /* request video mode */
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */

/* 
Declare a multiboot header that marks the program as a kernel.
*/
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0 /* header_addr */
.long 0 /* load_addr */
.long 0 /* load_end_addr */
.long 0 /* bss_end_addr */
.long 0 /* entry_addr */
.long 0 /* mode_type: linear graphics */
.long 1024 /* width */
.long 768  /* height */
.long 32   /* depth */

/*
The multiboot standard does not define the value of the stack pointer register
(esp) and it is up to the kernel to provide a stack.
*/
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

/*
The linker script specifies _start as the entry point to the kernel and the
bootloader will jump to this position once the kernel has been loaded.
*/
.section .text
.global _start
.type _start, @function
_start:
	/* Set up the stack. */
	mov $stack_top, %esp

	/* Push multiboot info struct pointer and magic number */
	push %ebx
	push %eax

	/* Call the global constructors. */
	/* call _init */

	/* Transfer control to the main kernel. */
	call kernel_main

	/*
	If the system has nothing more to do, put the computer into an
	infinite loop.
	*/
	cli
1:	hlt
	jmp 1b

.size _start, . - _start
