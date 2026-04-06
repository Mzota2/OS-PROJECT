// tasks.c
#include <stdint.h>
#include "scheduler.h"
#include "memory.h"

extern void syscall(uint32_t num, uint32_t arg);

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_putc(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, (uint8_t)c);
}

static void serial_print(const char* s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s);
        s++;
    }
}

extern void display_string(uint32_t row, const char* str);
extern void display_clear_row(uint32_t row);
extern void display_task_status(const char* name, char status);
extern void syscall_request(uint32_t num, uint32_t arg);
extern uint32_t input_getchar_nb(char* c);
extern void serial_putchar(char c);

static void busy_delay(volatile uint32_t cycles) {
	while (cycles--) {
		__asm__ __volatile__("" : : : "memory");
	}
}

void task_a(void) {
	serial_print("[TASK] A start\n");
	display_string(3, "Task A running");
	for (;;) {
		syscall_request(1, 'A'); // print via syscall
		busy_delay(3000000);
		scheduler_yield();
	}
}

void task_b(void) {
	serial_print("[TASK] B start\n");
	display_string(4, "Task B running");
	for (;;) {
		syscall_request(1, 'B');
		busy_delay(3000000);
		scheduler_yield();
	}
}

void task_c(void) {
	serial_print("[TASK] C start\n");
	display_string(5, "Task C running");
	for (;;) {
		syscall_request(1, 'C');
		busy_delay(3000000);
		scheduler_yield();
	}
}

static void task_shell(void) {
	serial_print("[TASK] Shell start\n");
	display_string(6, "Shell ready");
	display_clear_row(7);
	display_clear_row(8);
	display_clear_row(9);
	for (;;) {
		char c;
		if (input_getchar_nb(&c)) {
			serial_print("[SHELL] Received: ");
			serial_putchar(c);
			serial_print("\n");
			if (c == 'h' || c == 'H') {
				display_string(7, "Commands: h=help, a=info A, b=info B, c=info C, w=write, q=spawn A, r=spawn B, t=spawn C, x=exit, m=memory test, c=clear");
			} else if (c == 'a' || c == 'A') {
				display_string(8, "Task A: prints 'A' via syscall");
			} else if (c == 'b' || c == 'B') {
				display_string(9, "Task B: prints 'B' via syscall");
			} else if (c == 'c' || c == 'C') {
				display_string(10, "Task C: prints 'C' via syscall");
			} else if (c == 'w' || c == 'W') {
				syscall(1, 'A');
				display_string(11, "Write syscall called");
			} else if (c == 'q' || c == 'Q') {
				syscall(2, 0);
				display_string(11, "Spawned task A");
			} else if (c == 'r' || c == 'R') {
				syscall(2, 1);
				display_string(11, "Spawned task B");
			} else if (c == 't' || c == 'T') {
				syscall(2, 2);
				display_string(11, "Spawned task C");
			} else if (c == 'x' || c == 'X') {
				display_string(11, "Exiting shell...");
				syscall(3, 0);
			} else if (c == 'm' || c == 'M') {
				void* page = alloc_page();
				if (page) {
					*(char*)page = 'T';
					if (*(char*)page == 'T') {
						display_string(11, "Memory test: PASS");
					} else {
						display_string(11, "Memory test: FAIL");
					}
				} else {
					display_string(11, "Memory test: OOM");
				}
			} else if (c == 'c' || c == 'C') {
				display_clear_row(7);
				display_clear_row(8);
				display_clear_row(9);
				display_clear_row(10);
				display_clear_row(11);
				display_string(7, "Screen cleared");
			} else {
				display_string(11, "Unknown command");
			}
		}
		scheduler_yield();
	}
}

void tasks_init_demo(void) {
	display_clear_row(3);
	display_clear_row(4);
	display_clear_row(5);
	display_clear_row(6);
	display_clear_row(7);
	display_clear_row(8);
	display_clear_row(9);
	display_clear_row(10);
	display_clear_row(11);
	scheduler_add_task(task_a, "Task A");
	scheduler_add_task(task_b, "Task B");
	scheduler_add_task(task_c, "Task C");
	scheduler_add_task(task_shell, "Shell");
}
