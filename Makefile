CC = gcc
AS = nasm
LD = ld

CFLAGS = -ffreestanding -m32 -c
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

all: os.iso

boot.o:
	$(AS) -f elf32 boot/boot.asm -o boot.o

kernel.o:
	$(CC) $(CFLAGS) kernel/kernel.c -o kernel.o

kernel.elf: boot.o kernel.o
	$(LD) $(LDFLAGS) boot.o kernel.o -o kernel.elf

os.iso: kernel.elf
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o os.iso iso

run: os.iso
	qemu-system-x86_64 -cdrom os.iso

clean:
	rm -rf *.o *.bin *.elf *.iso iso
