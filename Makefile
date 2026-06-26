CC      = gcc -m32
AS      = gcc -m32
CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc -fno-pie -fno-pic
LDFLAGS = -m32 -T src/linker.ld -nostdlib -no-pie -Wl,--build-id=none

SRCS_C = src/kernel.c src/gdt.c src/idt.c src/keyboard.c \
         src/string.c src/sprintf.c src/pmm.c src/paging.c \
         src/kheap.c src/pit.c src/rtc.c src/shell.c src/tar.c \
         src/task.c src/tss.c src/syscall.c src/user.c \
         src/vesa.c src/bmp.c src/mouse.c src/wm.c \
         src/snake.c src/calc.c src/clock.c src/wallpaper.c \
         src/paint.c src/ata.c src/fs.c src/fat16.c src/explorer.c src/speaker.c \
         src/minesweeper.c src/settings.c src/elf.c src/pci.c src/rtl8139.c \
         src/ethernet.c src/arp.c src/ipv4.c src/icmp.c src/sb16.c src/uhci.c \
         src/ttf.c src/vector.c src/music.c src/udp.c src/tcp.c src/browser.c
SRCS_S = src/boot.S src/gdt_flush.S src/isr.S src/context_switch.S

OBJS = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: elsea.iso

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.S
	$(AS) -c $< -o $@

elsea.bin: $(OBJS) src/linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -lgcc

disk.img:
	dd if=/dev/zero of=disk.img bs=512 count=65536
	mkfs.fat -F 16 -n "ELSEAOS" disk.img

# Dual BIOS + UEFI bootable ISO.
# grub-mkrescue includes i386-pc (BIOS) + x86_64-efi (UEFI) when both
# grub-efi-amd64-bin and grub-pc-bin packages are installed.

hello.elf: hello.c
	$(CC) -m32 -ffreestanding -fno-pie -fno-pic -nostdlib -Wl,-Ttext=0x08048000 hello.c -o hello.elf
	cp hello.elf initrd/hello.elf

elsea.iso: elsea.bin disk.img hello.elf
	mkdir -p isodir/boot/grub
	cp elsea.bin isodir/boot/elsea.bin
	cp grub/grub.cfg isodir/boot/grub/grub.cfg
	cd initrd && tar -cvf ../isodir/boot/initrd.tar .
	grub-mkrescue -o elsea.iso isodir \
	    --modules="normal part_gpt part_msdos fat iso9660 multiboot2 search" 2>&1

run: elsea.iso disk.img
	qemu-system-i386 -cdrom elsea.iso \
	    -drive file=disk.img,format=raw,index=0,media=disk \
	    -netdev user,id=n0 -device rtl8139,netdev=n0 \
	    -audiodev pa,id=snd0 -device sb16,audiodev=snd0 \
	    -boot d

run-uefi: elsea.iso disk.img
	qemu-system-i386 -cdrom elsea.iso \
	    -drive file=disk.img,format=raw,index=0,media=disk \
	    -netdev user,id=n0 -device rtl8139,netdev=n0 \
	    -audiodev pa,id=snd0 -device sb16,audiodev=snd0 \
	    -bios /usr/share/ovmf/OVMF.fd -boot d 2>/dev/null || \
	qemu-system-i386 -cdrom elsea.iso \
	    -drive file=disk.img,format=raw,index=0,media=disk \
	    -netdev user,id=n0 -device rtl8139,netdev=n0 \
	    -audiodev pa,id=snd0 -device sb16,audiodev=snd0 \
	    -boot d

debug: elsea.iso disk.img
	qemu-system-i386 -cdrom elsea.iso \
	    -drive file=disk.img,format=raw,index=0,media=disk \
	    -boot d -s -S -no-reboot -no-shutdown \
	    -serial stdio 2>&1 | tee qemu_debug.log &
	gdb elsea.bin -ex "target remote :1234"

clean:
	rm -f src/*.o elsea.bin elsea.iso
	rm -rf isodir
