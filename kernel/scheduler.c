// Cooperative round-robin scheduler with context switching
#include <stdint.h>
#include "scheduler.h"

#define MAX_TASKS 10
#define STACK_SIZE 4096

static task_t tasks[MAX_TASKS];
static uint8_t task_stacks[MAX_TASKS][STACK_SIZE];
static void (*task_entries[MAX_TASKS])(void);
static uint32_t task_count = 0;
static uint32_t current_index = 0;
static uint8_t started = 0;

extern void context_switch_asm(task_t* prev_task, task_t* next_task);
extern void display_task_status(const char* name, char status);

static void task_trampoline(void);

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

	// Initialize context to start at task_trampoline with a fresh stack
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
	tasks[idx].context.eflags = 0x202; // IF=1, reserved bit set

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
	// With cooperative switching, just update any visual state here if desired.
	// Preemptive switching could request a yield flag in the future.
}

void scheduler_yield(void) {
	if (task_count <= 1) return;
	uint32_t prev = current_index;
	uint32_t next = find_next_ready(prev);
	if (next == prev) return;

	task_t* prev_task = &tasks[prev];
	task_t* next_task = &tasks[next];

	prev_task->state = TASK_READY;
	next_task->state = TASK_RUNNING;

	display_task_status((const char*)next_task->name, '*');
	current_index = next;

	// Switch contexts: does not return to the same point; execution continues in next task.
	context_switch_asm(prev_task, next_task);
}

void scheduler_start(void) {
	if (task_count == 0 || started) return;
	started = 1;

	current_index = 0;
	tasks[0].state = TASK_RUNNING;
	display_task_status((const char*)tasks[0].name, '*');

	// Jump from kernel context into the first task
	// Use a dummy previous context on stack (ignored on first switch)
	task_t dummy_prev;
	context_switch_asm(&dummy_prev, &tasks[0]);
}

// Trampoline that calls the registered entry for the current task id
static void task_trampoline(void) {
	// Find our task slot
	uint32_t id = current_index;
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
