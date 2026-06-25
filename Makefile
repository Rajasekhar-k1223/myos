CC = gcc -m32
AS = gcc -m32
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -T src/linker.ld -nostdlib

all: myos.iso

src/boot.o: src/boot.S
	$(AS) -c src/boot.S -o src/boot.o

src/kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c src/kernel.c -o src/kernel.o

myos.bin: src/boot.o src/kernel.o
	$(CC) $(LDFLAGS) src/boot.o src/kernel.o -o myos.bin -lgcc

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
