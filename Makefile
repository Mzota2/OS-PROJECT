CC = gcc
AS = nasm
LD = ld

CFLAGS = -ffreestanding -m32 -c -Ikernel
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

all: os.iso

boot.o:
	$(AS) -f elf32 boot/boot.asm -o boot.o

kernel.o:
	$(CC) $(CFLAGS) kernel/kernel.c -o kernel.o

interrupts.o:
	$(CC) $(CFLAGS) kernel/interrupts.c -o interrupts.o

irq0.o:
	$(AS) -f elf32 kernel/irq0.asm -o irq0.o

scheduler.o:
	$(CC) $(CFLAGS) kernel/scheduler.c -o scheduler.o

context_switch.o:
	$(AS) -f elf32 kernel/context_switch.asm -o context_switch.o

tasks.o:
	$(CC) $(CFLAGS) kernel/tasks.c -o tasks.o

kernel.elf: boot.o kernel.o interrupts.o irq0.o scheduler.o context_switch.o tasks.o
	$(LD) $(LDFLAGS) boot.o kernel.o interrupts.o irq0.o scheduler.o context_switch.o tasks.o -o kernel.elf

os.iso: kernel.elf
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o os.iso iso

run: os.iso
	qemu-system-i386 -cdrom os.iso -no-reboot -no-shutdown

run-debug: os.iso
	qemu-system-i386 -cdrom os.iso -no-reboot -no-shutdown -d int,cpu_reset -D qemu.log

clean:
	rm -rf *.o *.bin *.elf *.iso iso
