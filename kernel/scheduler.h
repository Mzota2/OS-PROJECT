#ifndef OS_SCHEDULER_H
#define OS_SCHEDULER_H

#include <stdint.h>

typedef enum {
    TASK_DEAD = 0,
    TASK_READY = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3
} task_state_t;

typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
} __attribute__((packed)) cpu_context_t;

// Order matters! Must match context_switch.asm expectations.
typedef struct {
    uint32_t id;
    task_state_t state;
    cpu_context_t context;
    uint32_t stack_base;
    uint32_t stack_size;
    char name[16];
} task_t;

// Scheduler API
void scheduler_init(void);
void scheduler_add_task(void (*entry)(void), const char* name);
task_t* scheduler_current_task(void);
void scheduler_tick(void);
extern void context_switch_asm(task_t* prev_task, task_t* next_task);
void scheduler_yield(void);
void scheduler_start(void);

#endif // OS_SCHEDULER_H
