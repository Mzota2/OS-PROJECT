// tasks.c
#include <stdint.h>
#include "scheduler.h"

extern void display_string(uint32_t row, const char* str);
extern void display_clear_row(uint32_t row);
extern void display_task_status(const char* name, char status);
extern void syscall_request(uint32_t num, uint32_t arg);

static void busy_delay(volatile uint32_t cycles) {
	while (cycles--) {
		__asm__ __volatile__("" : : : "memory");
	}
}

static void task_a(void) {
	display_string(3, "Task A running");
	for (;;) {
		syscall_request(1, 'A'); // print via syscall
		busy_delay(3000000);
		scheduler_yield();       // cooperative yield in task context
	}
}

static void task_b(void) {
	display_string(4, "Task B running");
	for (;;) {
		syscall_request(1, 'B');
		busy_delay(3000000);
		scheduler_yield();
	}
}

static void task_c(void) {
	display_string(5, "Task C running");
	for (;;) {
		syscall_request(1, 'C');
		busy_delay(3000000);
		scheduler_yield();
	}
}

void tasks_init_demo(void) {
	display_clear_row(3);
	display_clear_row(4);
	display_clear_row(5);
	scheduler_add_task(task_a, "Task A");
	scheduler_add_task(task_b, "Task B");
	scheduler_add_task(task_c, "Task C");
}
