// kernel.c
#include <stdint.h>
#include "idt.h"
#include "scheduler.h"

static inline void sti(void) {
	asm volatile("sti");
}

static inline void hlt(void) {
	asm volatile("hlt");
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
	// Basic screen init
	vga_clear();
	vga_puts_at(0, 0, "Custom OS Kernel");
	vga_puts_at(2, 0, "Initializing IDT, PIC, PIT...");

	// Phase 2 wiring (interrupts and timer)
	idt_init();
	pit_init(1); // 1 Hz to make ticks visible

	vga_puts_at(0, 40, "[OK]");
	vga_puts_at(2, 40, "[OK]");
	vga_puts_at(3, 0, "All systems initialized");

	// Phase 3: cooperative scheduler/tasks
	scheduler_init();
	// add demo tasks
	extern void tasks_init_demo(void);
	tasks_init_demo();

	// Enable interrupts so PIT starts ticking
	sti();

	// Start the scheduler: jump into first task
	scheduler_start();

	// If control ever returns, idle
	for (;;) { hlt(); }
}

