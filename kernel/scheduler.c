// scheduler.c
// Task Scheduler and Context Switching
//
// Manages multiple kernel tasks (lightweight threads) with round-robin scheduling.
// Features:
// - Task creation and management
// - Context switching (saving/restoring CPU state)
// - Round-robin scheduling (each task gets a turn)
// - Task lifecycle (READY, RUNNING, DEAD states)
//
// Context switching is expensive: we save ~10 registers per switch.
// To minimize overhead, we only switch when tasks explicitly yield().

// Cooperative round-robin scheduler with context switching
#include <stdint.h>
#include "scheduler.h"

#define MAX_TASKS 10
#define STACK_SIZE 4096

// ===== PORT I/O HELPERS =====
static inline void outb(uint16_t port, uint8_t value) {
    // Output a byte to an I/O port
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    // Read a byte from an I/O port
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// ===== SERIAL OUTPUT FOR DEBUGGING =====
static void serial_putc(char c) {
    // Wait for transmit buffer to be empty, then output character
    while ((inb(0x3F8 + 5) & 0x20) == 0);  // Poll TX buffer ready bit
    outb(0x3F8, (uint8_t)c);
}

static void serial_print(const char* s) {
    // Print null-terminated string to serial port
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');  // CR before LF for line endings
        }
        serial_putc(*s);
        s++;
    }
}

// ===== SCHEDULER STATE =====
static task_t tasks[MAX_TASKS];                    // Array holding all task structures
static uint8_t task_stacks[MAX_TASKS][STACK_SIZE]; // Per-task kernel stacks
static void (*task_entries[MAX_TASKS])(void);     // Function pointers for each task
static uint32_t task_count = 0;                    // Number of tasks registered
static uint32_t current_index = 0;                 // Index of current running task
static uint8_t started = 0;                        // Flag: has scheduler started?

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
	// Initialize the scheduler: zero all task state.
	// Called once at kernel startup before any tasks are added.
	
	task_count = 0;
	current_index = 0;
	started = 0;
	
	// Initialize all task slots to DEAD so they're safe
	for (uint32_t i = 0; i < MAX_TASKS; i++) {
		tasks[i].id = i;
		tasks[i].state = TASK_DEAD;
		tasks[i].stack_base = (uint32_t)&task_stacks[i][0];
		tasks[i].stack_size = STACK_SIZE;
		// Clear task name
		for (int j = 0; j < 16; j++) {
			tasks[i].name[j] = '\0';
		}
		task_entries[i] = 0;
	}
}

void scheduler_add_task(void (*entry)(void), const char* name) {
	// Register a new task with the scheduler.
	// This sets up the task's CPU context so it can be switched to.
	//
	// Parameters:
	//   entry: function pointer (void*)(void) to call when task starts
	//   name: human-readable task name (for debugging)
	
	if (task_count >= MAX_TASKS) return;  // Fail silently if task table full
	
	uint32_t idx = task_count++;  // Get next slot and increment count
	task_entries[idx] = entry;    // Remember task's entry function

	// Initialize context for new task
	// The task will start running task_trampoline(), which will call entry()
	uint32_t stack_top = tasks[idx].stack_base + tasks[idx].stack_size;

	// Align stack to 16 bytes for ABI compliance (callee must align to 16)
	stack_top &= ~((uint32_t)0xF);

	// Zero all general purpose registers (clean state)
	tasks[idx].context.eax = 0;
	tasks[idx].context.ebx = 0;
	tasks[idx].context.ecx = 0;
	tasks[idx].context.edx = 0;
	tasks[idx].context.esi = 0;
	tasks[idx].context.edi = 0;
	tasks[idx].context.ebp = 0;
	
	// Set stack pointer to top of allocated stack
	tasks[idx].context.esp = stack_top;
	
	// Set instruction pointer to task_trampoline()
	// This is the first code that will run when we switch to this task
	tasks[idx].context.eip = (uint32_t)task_trampoline;
	
	// Set CPU flags: 0x202 means Interrupt Flag (IF) is set (interrupts enabled)
	tasks[idx].context.eflags = 0x202;

	// Mark task as ready to run
	tasks[idx].state = TASK_READY;

	// Copy task name for debugging (up to 15 chars + null terminator)
	for (int i = 0; i < 15 && name && name[i] != '\0'; i++) {
		tasks[idx].name[i] = name[i];
		tasks[idx].name[i + 1] = '\0';
	}
}

task_t* scheduler_current_task(void) {
	// Get the task_t structure of the currently running task
	if (task_count == 0) return 0;
	return &tasks[current_index % task_count];
}

static uint32_t find_next_ready(uint32_t from) {
	// Find the next task that is ready to run (after 'from' in round-robin order).
	// Cycles through all tasks looking for any in TASK_READY or TASK_RUNNING state.
	// If none found, returns 'from' (current task).
	
	if (task_count == 0) return 0;
	
	// Start checking from next task and wrap around
	for (uint32_t i = 1; i <= task_count; i++) {
		uint32_t idx = (from + i) % task_count;  // Wrap around using modulo
		if (tasks[idx].state == TASK_READY || tasks[idx].state == TASK_RUNNING) {
			return idx;  // Found a ready task
		}
	}
	
	return from;  // No other task is ready, return current
}

void scheduler_tick(void) {
	// Called by timer ISR on each PIT tick.
	// Currently a no-op (we use cooperative scheduling only).
	// In future, could implement preemptive scheduling here.
}

void scheduler_yield(void) {
    // Voluntarily relinquish CPU to next ready task.
    // Called by tasks: scheduler_yield();
    // Note: this function doesn't return immediately;
    // it returns when this task is switched back to.
    
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    
    // Debug visualization: show yield on screen
    vga[24 * 80 + 0] = (0x0F << 8) | 'Y';
    vga[24 * 80 + 1] = (0x0F << 8) | 'I';
    vga[24 * 80 + 2] = (0x0F << 8) | 'E';
    vga[24 * 80 + 3] = (0x0F << 8) | 'L';
    vga[24 * 80 + 4] = (0x0F << 8) | 'D';
    
    // Log yield to serial
    serial_print("[SCHED] yield prev=");
    char pch = '0' + (char)current_index;
    serial_putc(pch);
    serial_print("\n");
    
    // If only one task exists, can't switch to another
    if (task_count <= 1) return;
    
    uint32_t prev = current_index;
    uint32_t next = find_next_ready(prev);  // Find next ready task
    
    if (next == prev) {
        // No other ready task, stay with current
        serial_print("[SCHED] yield no-next\n");
        return;
    }

    task_t* prev_task = &tasks[prev];
    task_t* next_task = &tasks[next];

    // Update task states for scheduler bookkeeping
    prev_task->state = TASK_READY;     // Current becomes ready (for future switching)
    next_task->state = TASK_RUNNING;   // Next becomes running

    // Update display
    display_task_status((const char*)next_task->name, '*');
    
    // Update global current task index
    current_index = next;
    display_current_task((const char*)next_task->name);
    
    // Log the switch
    serial_print("[SCHED] switch to ");
    char nch = '0' + (char)next;
    serial_putc(nch);
    serial_print("\n");

    // ===== CRITICAL: CONTEXT SWITCH =====
    // This assembly function saves prev's CPU state and loads next's state.
    // After loading next's state, execution JUMPS to next's instruction pointer.
    // This function "returns" when this task is switched back to from another context_switch_asm.
    context_switch_asm(prev_task, next_task);

    // When we reach here, we've been switched back to by another task
    serial_print("[SCHED] resumed from context_switch_asm\n");
}

void scheduler_start(void) {
	// Start the scheduler: switch to the first task and never return.
	// Called once at kernel init to begin multitasking.
	
	if (task_count == 0 || started) return;  // Fail if no tasks or already started
	started = 1;

	// Debug: display message on screen
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

	// Initialize first task as current and running
	current_index = 0;
	tasks[0].state = TASK_RUNNING;
	display_task_status((const char*)tasks[0].name, '*');
	display_current_task((const char*)tasks[0].name);

	// Create dummy kernel context (we won't save it, just load the first task from it).
	// This synthetic context makes the first task switch "special" since no kernel code saved.
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
	// Switch from dummy kernel context to first task.
	// This never returns (we jump to first task's code).
	context_switch_asm(&dummy_kernel, &tasks[0]);
	serial_print("[TEST] ERROR returned from direct switch\n");
}

uint32_t scheduler_on_timer_isr(uint32_t current_esp) {
	// Called by timer ISR for future preemptive scheduling.
	// Currently unused (we do cooperative scheduling only).
	// Future: could implement forced task switching here.
	return current_esp;
}

// task_trampoline: Initial entry point for every new task.
// When we context_switch to a new task, EIP points here first.
// This function calls the task's actual entry function, then handles exit.
static void task_trampoline(void) {
    debug_trampoline();  // Debug visualization
    
    // Get the ID of the current task (we're running it now)
    uint32_t id = current_index;
    serial_print("[SCHED] task_trampoline id = ");
    char num = '0' + (char)id;
    serial_putc(num);
    serial_print("\n");
    
    // Debug: show task starting on screen
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[21 * 80 + 0] = (0x0F << 8) | num;
    
    // Get the task's entry function (what it should actually run)
    void (*entry)(void) = task_entries[id];
    
    // Call the task's main function
    if (entry) {
        entry();  // Run the task's code
    }
    
    // If we reach here, the task returned (finished execution).
    // Mark task as dead so scheduler won't switch to it again.
    tasks[id].state = TASK_DEAD;
    
    // Infinite loop yielding to keep task dead but present in scheduler.
    // Yields to other tasks, which will never switch back to us.
    while (1) {
        scheduler_yield();  // Give other tasks a chance
    }
}
