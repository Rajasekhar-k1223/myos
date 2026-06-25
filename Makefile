CC     = gcc -m32
AS     = gcc -m32
CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc -fno-pie -fno-pic
LDFLAGS = -m32 -T src/linker.ld -nostdlib -no-pie -Wl,--build-id=none

SRCS_C = src/kernel.c src/gdt.c src/idt.c src/keyboard.c \
         src/string.c src/sprintf.c src/pmm.c src/paging.c \
         src/kheap.c src/pit.c src/rtc.c src/shell.c src/tar.c \
         src/task.c src/tss.c src/syscall.c src/user.c src/vesa.c src/bmp.c src/mouse.c src/wm.c src/snake.c src/calc.c src/clock.c src/wallpaper.c src/paint.c src/ata.c src/fs.c
SRCS_S = src/boot.S src/gdt_flush.S src/isr.S src/context_switch.S
OBJS   = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: myos.iso

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.S
	$(AS) -c $< -o $@

myos.bin: $(OBJS) src/linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -lgcc

disk.img:
	dd if=/dev/zero of=disk.img bs=512 count=10000

myos.iso: myos.bin disk.img
	mkdir -p isodir/boot/grub
	cp myos.bin isodir/boot/myos.bin
	cp grub/grub.cfg isodir/boot/grub/grub.cfg
	cd initrd && tar -cvf ../isodir/boot/initrd.tar .
	grub-mkrescue -o myos.iso isodir

run: myos.iso disk.img
	qemu-system-i386 -cdrom myos.iso -drive file=disk.img,format=raw,index=0,media=disk -boot d

debug: myos.iso disk.img
	qemu-system-i386 -cdrom myos.iso -drive file=disk.img,format=raw,index=0,media=disk -boot d -s -S -no-reboot -no-shutdown \
	    -serial stdio 2>&1 | tee qemu_debug.log &
	gdb myos.bin -ex "target remote :1234"

clean:
	rm -f src/*.o myos.bin myos.iso
	rm -rf isodir
