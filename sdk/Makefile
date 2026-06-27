CC = gcc
CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -I../src
LDFLAGS = -m32 -nostdlib -Ttext 0x40000000

all: test_app.elf

test_app.elf: test_app.o
$(CC) $(LDFLAGS) -o $@ $^

test_app.o: test_app.c
$(CC) $(CFLAGS) -c $< -o $@

clean:
rm -f *.o *.elf
