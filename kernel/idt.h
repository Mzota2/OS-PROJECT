#ifndef OS_IDT_H
#define OS_IDT_H

#include <stdint.h>

// Each IDT entry is a 32-bit interrupt gate descriptor in protected mode.
// It maps an interrupt vector to a handler function.

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

// Pointer structure used by lidt instruction.

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

// Initialize the IDT and PIC remap (IRQ base 0x20).
void idt_init(void);

// Initialize PIT with the requested frequency (Hz).
void pit_init(uint32_t frequency);

#endif // OS_IDT_H
