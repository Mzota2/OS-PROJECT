// kernel.c
#include <stdint.h>
#include "idt.h"
#include "scheduler.h"
#include "gdt.h"

static inline void sti(void) {
	asm volatile("sti");
}

static inline void hlt(void) {
	asm volatile("hlt");
}

static inline void outb(uint16_t port, uint8_t value) {
	asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
	uint8_t value;
	asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

// Serial port output for debugging
#define SERIAL_PORT 0x3F8

static void serial_init(void) {
	outb(SERIAL_PORT + 1, 0x00);    // Disable all interrupts
	outb(SERIAL_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
	outb(SERIAL_PORT + 0, 0x03);    // Set divisor to 3 (38400 baud)
	outb(SERIAL_PORT + 1, 0x00);
	outb(SERIAL_PORT + 3, 0x03);    // 8 bits, no parity, 1 stop bit
	outb(SERIAL_PORT + 2, 0xC7);    // Enable FIFO
	outb(SERIAL_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static void serial_putchar(char c) {
	// Wait for transmit holding register to be empty
	while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
	outb(SERIAL_PORT, (uint8_t)c);
}

static void serial_print(const char* str) {
	while (*str) {
		if (*str == '\n') {
			serial_putchar('\r');
		}
		serial_putchar(*str);
		str++;
	}
}

static volatile uint16_t* const VGA = (uint16_t*)0xB8000;

static void vga_clear(void) {
	for (int r = 0; r < 25; r++) {
		for (int c = 0; c < 80; c++) {
			VGA[r * 80 + c] = (0x0F << 8) | ' ';
		}
	}
}

static void vga_puts_at(int row, int col, const char* s) {
	if (row < 0 || row >= 25) return;
	int i = 0;
	while (s[i] != '\0' && (col + i) < 80) {
		VGA[row * 80 + col + i] = (0x0F << 8) | (unsigned char)s[i];
		i++;
	}
}

void kernel_main(void) {
	// Initialize serial port early for debugging
	serial_init();
	serial_print("[KERNEL] Serial debug initialized\n");
	
	// Basic screen init
	vga_clear();
	vga_puts_at(0, 0, "Custom OS Kernel");
	serial_print("[KERNEL] VGA cleared and title printed\n");
	
	vga_puts_at(1, 0, "Initializing IDT, PIC, PIT...");
	serial_print("[INIT] Starting hardware initialization\n");

	// Initialize GDT first for proper segment descriptors
	serial_print("[INIT] Calling gdt_init()...\n");
	gdt_init();
	serial_print("[INIT] gdt_init() completed\n");

	// Phase 2 wiring (interrupts and timer)
	vga_puts_at(2, 0, "Calling idt_init...");
	serial_print("[INIT] Calling idt_init()...\n");
	idt_init();
	serial_print("[INIT] idt_init() completed\n");
	
	vga_puts_at(2, 20, "[OK]  pit_init...");
	serial_print("[INIT] Calling pit_init(18)...\n");
	pit_init(18);  // 18 Hz is the fastest reliable rate with max divisor
	serial_print("[INIT] pit_init() completed\n");

	vga_puts_at(2, 50, "[OK]");
	
	// Phase 3: cooperative scheduler/tasks
	vga_puts_at(3, 0, "Initializing scheduler...");
	serial_print("[INIT] Calling scheduler_init()...\n");
	scheduler_init();
	serial_print("[INIT] scheduler_init() completed\n");
	
	vga_puts_at(4, 0, "Adding demo tasks...");
	serial_print("[INIT] Calling tasks_init_demo()...\n");
	extern void tasks_init_demo(void);
	tasks_init_demo();
	serial_print("[INIT] tasks_init_demo() completed\n");

	vga_puts_at(5, 0, "Enabling interrupts...");
	serial_print("[INIT] Calling sti() to enable interrupts...\n");
	
	// Enable interrupts so PIT starts ticking
	sti();
	serial_print("[KERNEL] Interrupts enabled - entering polling loop\n");
	
	vga_puts_at(5, 20, "[INT OK]");
	vga_puts_at(6, 0, "Starting scheduler...");

	extern void scheduler_start(void);
	serial_print("[KERNEL] Starting scheduler...\n");
	scheduler_start();
	
	// scheduler_start() never returns - execution continues in tasks
	serial_print("[KERNEL] ERROR: scheduler_start() returned!\n");
}

