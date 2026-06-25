CC     = gcc -m32
AS     = gcc -m32
CFLAGS  = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc -no-pie
LDFLAGS = -T src/linker.ld -nostdlib -no-pie

SRCS_C = src/kernel.c src/gdt.c src/idt.c src/keyboard.c src/string.c src/pmm.c src/paging.c src/kheap.c
SRCS_S = src/boot.S src/gdt_flush.S src/isr.S
OBJS   = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: myos.iso

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.S
	$(AS) -c $< -o $@

myos.bin: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o myos.bin -lgcc

myos.iso: myos.bin
	mkdir -p isodir/boot/grub
	cp myos.bin isodir/boot/myos.bin
	cp grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

run: myos.iso
	qemu-system-i386 -cdrom myos.iso

clean:
	rm -f src/*.o myos.bin myos.iso
	rm -rf isodir
