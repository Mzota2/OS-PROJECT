// Cooperative round-robin scheduler with context switching
#include <stdint.h>
#include "scheduler.h"

#define MAX_TASKS 10
#define STACK_SIZE 4096

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

static task_t tasks[MAX_TASKS];
static uint8_t task_stacks[MAX_TASKS][STACK_SIZE];
static void (*task_entries[MAX_TASKS])(void);
static uint32_t task_count = 0;
static uint32_t current_index = 0;
static uint8_t started = 0;

extern void context_switch_asm(task_t* prev_task, task_t* next_task);
extern void display_task_status(const char* name, char status);
extern void display_current_task(const char* name);

static void task_trampoline(void);

static void debug_trampoline(void) {
	volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
	vga[23 * 80 + 0] = (0x0F << 8) | 'T';
	vga[23 * 80 + 1] = (0x0F << 8) | 'R';
	vga[23 * 80 + 2] = (0x0F << 8) | 'M';
	vga[23 * 80 + 3] = (0x0F << 8) | 'P';
}

void scheduler_init(void) {
	task_count = 0;
	current_index = 0;
	started = 0;
	for (uint32_t i = 0; i < MAX_TASKS; i++) {
		tasks[i].id = i;
		tasks[i].state = TASK_DEAD;
		tasks[i].stack_base = (uint32_t)&task_stacks[i][0];
		tasks[i].stack_size = STACK_SIZE;
		for (int j = 0; j < 16; j++) {
			tasks[i].name[j] = '\0';
		}
		task_entries[i] = 0;
	}
}

void scheduler_add_task(void (*entry)(void), const char* name) {
	if (task_count >= MAX_TASKS) return;
	uint32_t idx = task_count++;
	task_entries[idx] = entry;

	// Initialize context to start at task_trampoline with a fresh stack.
	uint32_t stack_top = tasks[idx].stack_base + tasks[idx].stack_size;

	// Align stack to 16 bytes for good measure
	stack_top &= ~((uint32_t)0xF);

	tasks[idx].context.eax = 0;
	tasks[idx].context.ebx = 0;
	tasks[idx].context.ecx = 0;
	tasks[idx].context.edx = 0;
	tasks[idx].context.esi = 0;
	tasks[idx].context.edi = 0;
	tasks[idx].context.ebp = 0;
	tasks[idx].context.esp = stack_top;
	tasks[idx].context.eip = (uint32_t)task_trampoline;
	tasks[idx].context.eflags = 0x202;

	tasks[idx].state = TASK_READY;

	// Copy name (up to 15 chars + null)
	for (int i = 0; i < 15 && name && name[i] != '\0'; i++) {
		tasks[idx].name[i] = name[i];
		tasks[idx].name[i + 1] = '\0';
	}
}

task_t* scheduler_current_task(void) {
	if (task_count == 0) return 0;
	return &tasks[current_index % task_count];
}

static uint32_t find_next_ready(uint32_t from) {
	if (task_count == 0) return 0;
	for (uint32_t i = 1; i <= task_count; i++) {
		uint32_t idx = (from + i) % task_count;
		if (tasks[idx].state == TASK_READY || tasks[idx].state == TASK_RUNNING) {
			return idx;
		}
	}
	return from;
}

void scheduler_tick(void) {
	// Keep heartbeat independent; cooperative switches occur via explicit yield.
}

void scheduler_yield(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[24 * 80 + 0] = (0x0F << 8) | 'Y';
    vga[24 * 80 + 1] = (0x0F << 8) | 'I';
    vga[24 * 80 + 2] = (0x0F << 8) | 'E';
    vga[24 * 80 + 3] = (0x0F << 8) | 'L';
    vga[24 * 80 + 4] = (0x0F << 8) | 'D';
    serial_print("[SCHED] yield prev=");
    char pch = '0' + (char)current_index;
    serial_putc(pch);
    serial_print("\n");
    if (task_count <= 1) return;
    uint32_t prev = current_index;
    uint32_t next = find_next_ready(prev);
    if (next == prev) {
        serial_print("[SCHED] yield no-next\n");
        return;
    }

    task_t* prev_task = &tasks[prev];
    task_t* next_task = &tasks[next];

    prev_task->state = TASK_READY;
    next_task->state = TASK_RUNNING;

    // Display task switch
    display_task_status((const char*)next_task->name, '*');
    
    current_index = next;
    display_current_task((const char*)next_task->name);
    serial_print("[SCHED] switch to ");
    char nch = '0' + (char)next;
    serial_putc(nch);
    serial_print("\n");

    // Switch contexts: does not return to the same point; execution continues in next task.
    context_switch_asm(prev_task, next_task);

    serial_print("[SCHED] resumed from context_switch_asm\n");
}

void scheduler_start(void) {
	if (task_count == 0 || started) return;
	started = 1;

	// Debug: write to screen to prove we got here
	volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
	const char* msg = "SCHED_START";
	for (int i = 0; msg[i] != '\0'; i++) {
		vga[6 * 80 + 30 + i] = (0x0F << 8) | msg[i];
	}
	vga[22 * 80 + 0] = (0x0F << 8) | 'S';
	vga[22 * 80 + 1] = (0x0F << 8) | 'T';
	vga[22 * 80 + 2] = (0x0F << 8) | 'A';
	vga[22 * 80 + 3] = (0x0F << 8) | 'R';
	vga[22 * 80 + 4] = (0x0F << 8) | 'T';

	current_index = 0;
	tasks[0].state = TASK_RUNNING;
	display_task_status((const char*)tasks[0].name, '*');
	display_current_task((const char*)tasks[0].name);

	// Create a dummy kernel task context (we won't save it, just load the first task)
	task_t dummy_kernel;
	dummy_kernel.context.eax = 0;
	dummy_kernel.context.ebx = 0;
	dummy_kernel.context.ecx = 0;
	dummy_kernel.context.edx = 0;
	dummy_kernel.context.esi = 0;
	dummy_kernel.context.edi = 0;
	dummy_kernel.context.ebp = 0;
	dummy_kernel.context.esp = 0;
	dummy_kernel.context.eip = 0;
	dummy_kernel.context.eflags = 0x202;

	serial_print("[TEST] direct switch to first task\n");
	context_switch_asm(&dummy_kernel, &tasks[0]);
	serial_print("[TEST] ERROR returned from direct switch\n");
}

uint32_t scheduler_on_timer_isr(uint32_t current_esp) {
	// Placeholder for future IRQ-driven preemption.
	return current_esp;
}

// Trampoline that calls the registered entry for the current task id
static void task_trampoline(void) {
    debug_trampoline();
    // Find our task slot
    uint32_t id = current_index;
    serial_print("[SCHED] task_trampoline id = ");
    char num = '0' + (char)id;
    serial_putc(num);
    serial_print("\n");
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[21 * 80 + 0] = (0x0F << 8) | num;
    void (*entry)(void) = task_entries[id];
    if (entry) {
        entry();
    }
    // If a task ever returns, mark it dead and yield
    tasks[id].state = TASK_DEAD;
    while (1) {
        scheduler_yield();
    }
}
