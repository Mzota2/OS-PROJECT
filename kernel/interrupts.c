#include <stdbool.h>
#include <stdint.h>

#include "idt.h"
#include "scheduler.h"

#define PIC1_COMMAND 0x20

#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI 0x20
#define KBD_DATA_PORT 0x60

// Global state shared across modules
static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;
static volatile uint32_t tick_count = 0;
static volatile uint32_t pit_raw_ticks = 0;
static volatile uint32_t pit_effective_hz = 0;
static volatile uint32_t scheduler_quantum_ticks = 1;
static volatile uint32_t scheduler_tick_divider = 0;

// Input buffer for keyboard data
#define INPUT_BUFFER_SIZE 256
static volatile char input_buffer[INPUT_BUFFER_SIZE];
static volatile uint32_t input_head = 0;
static volatile uint32_t input_tail = 0;

// Display state
static volatile uint32_t display_row_input = 6;
static volatile uint32_t display_row_task = 3;

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void pic_remap(void) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20); // Master vector offset 0x20
    outb(PIC2_DATA, 0x28); // Slave vector offset 0x28

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // Enable IRQ0 (timer) and IRQ1 (keyboard) on master PIC.
    // 0xFC = 11111100 (unmask IRQ0 and IRQ1). Slave remains fully masked.
    outb(PIC1_DATA, 0xFC); // 11111100
    outb(PIC2_DATA, 0xFF); // 11111111
}


static inline void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

static void set_idt_entry(uint8_t vector, void (*handler)(void), uint16_t selector, uint8_t flags) {
    uint32_t base = (uint32_t)handler;
    idt[vector].offset_low = base & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].zero = 0;
    idt[vector].type_attr = flags;
    idt[vector].offset_high = (base >> 16) & 0xFFFF;
}

static inline void lidt(void) {
    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * 256 - 1);
    idt_ptr.base = (uint32_t)&idt;
    asm volatile ("lidt (%0)" : : "r"(&idt_ptr));
}

extern void irq0_handler_asm(void);
extern void irq1_handler_asm(void);
extern void syscall_handler_asm(void);
extern void isr_stub_noerr(void);
extern void isr_stub_err(void);

static void pit_tick_handler(void);

void idt_init(void) {
    // Use kernel code segment selector (GDT entry 1: 0x08)
    const uint16_t cs = 0x08;

    for (int i = 0; i < 256; i++) {
        set_idt_entry(i, isr_stub_noerr, cs, 0x8E);
    }

    // Exceptions that push an error code
    set_idt_entry(8,  isr_stub_err,   cs, 0x8E); // Double Fault
    set_idt_entry(10, isr_stub_err,   cs, 0x8E); // Invalid TSS
    set_idt_entry(11, isr_stub_err,   cs, 0x8E); // Segment Not Present
    set_idt_entry(12, isr_stub_err,   cs, 0x8E); // Stack-Segment Fault
    set_idt_entry(13, isr_stub_err,   cs, 0x8E); // General Protection Fault
    set_idt_entry(14, isr_stub_err,   cs, 0x8E); // Page Fault
    set_idt_entry(17, isr_stub_err,   cs, 0x8E); // Alignment Check

    pic_remap();

    // Timer IRQ (IRQ0) mapped to vector 32
    set_idt_entry(32, irq0_handler_asm, cs, 0x8E);
    // Keyboard IRQ (IRQ1) mapped to vector 33
    set_idt_entry(33, irq1_handler_asm, cs, 0x8E);
    // Syscall trap (int 0x80)
    set_idt_entry(0x80, syscall_handler_asm, cs, 0x8E);

    lidt();
}

void pit_init(uint32_t frequency) {
    if (frequency == 0) {
        frequency = 1;
    }

    // PIT channel 0 divisor is 16-bit. The lowest hardware rate is ~18.2Hz.
    uint32_t divisor = 1193180 / frequency;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 65535) {
        divisor = 65535;
    }

    pit_effective_hz = 1193180 / divisor;
    if (pit_effective_hz == 0) {
        pit_effective_hz = 18;
    }

    // Run scheduler at requested logical frequency even when PIT clamps low Hz.
    scheduler_quantum_ticks = pit_effective_hz / frequency;
    if (scheduler_quantum_ticks == 0) {
        scheduler_quantum_ticks = 1;
    }
    scheduler_tick_divider = 0;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void pit_tick_handler(void) {
    tick_count++;

    volatile uint16_t* vga = (uint16_t*)0xB8000;
    
    // Row 1: Display tick count in fixed width
    const char* text = "Tick: ";
    int base = 80; // second line
    
    for (int i = 0; text[i] != '\0'; i++) {
        vga[base + i] = (0x0F << 8) | text[i];
    }

    uint32_t n = tick_count;
    int cur_col = base + 6;
    int width = 3; // display at most 999 ticks with leading zeros

    // write fixed width, right aligned
    for (int i = 0; i < width; i++) {
        int digit = n % 10;
        vga[cur_col + width - 1 - i] = (0x0F << 8) | ('0' + digit);
        n /= 10;
    }

    // clear any higher digits beyond width
    for (int i = width; i < 10; i++) {
        vga[cur_col + i] = (0x0F << 8) | ' ';
    }

    // Row 2: Status line showing we're still in kernel
    const char* status = "Running normally";
    volatile uint16_t* status_row = vga + 160; // row 2
    for (int i = 0; status[i] != '\0'; i++) {
        status_row[i] = (0x0F << 8) | status[i];
    }
}

// Called from assembly ISR wrapper
void pit_timer_service(void) {
    // Acknowledge the timer interrupt on the PIC
    pic_send_eoi(0);

    // Count every hardware PIT interrupt.
    pit_raw_ticks++;

    // Run visible tick + scheduler at requested logical tick rate.
    scheduler_tick_divider++;
    if (scheduler_tick_divider >= scheduler_quantum_ticks) {
        scheduler_tick_divider = 0;
        pit_tick_handler();
        scheduler_tick();
    }
}

void keyboard_service(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);

    bool key_release = scancode & 0x80;
    if (key_release) return; // ignore key releases

    static const char kbd_map[128] = {
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
        'd','f','g','h','j','k','l',';','\'', '`', 0,'\\','z','x','c','v',
        'b','n','m',',','.','/',' ',0,0,0,0,0,0,0,0,0,0,
    };

    if (scancode < 128) {
        char c = kbd_map[scancode];
        if (c != 0) {
            uint32_t next_head = (input_head + 1) % INPUT_BUFFER_SIZE;
            if (next_head != input_tail) {
                input_buffer[input_head] = c;
                input_head = next_head;
            }
        }
    }
}

// Safe character write to VGA buffer at a specific location
static void vga_putchar(uint32_t row, uint32_t col, char c) {
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    if (row < 25 && col < 80) {
        vga[row * 80 + col] = (0x0F << 8) | (unsigned char)c;
    }
}

// Safe string write to VGA buffer
static void vga_putstring(uint32_t row, uint32_t col, const char* str) {
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    if (row < 25) {
        uint32_t i = 0;
        while (str[i] != '\0' && col + i < 80) {
            vga[row * 80 + col + i] = (0x0F << 8) | (unsigned char)str[i];
            i++;
        }
    }
}

// Clear a row on VGA buffer
static void vga_clear_row(uint32_t row) {
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    if (row < 25) {
        for (int i = 0; i < 80; i++) {
            vga[row * 80 + i] = (0x0F << 8) | ' ';
        }
    }
}

// Keyboard interrupt handler
// Simple system call framework (Phase 4)
volatile uint32_t syscall_num = 0;
volatile uint32_t syscall_arg = 0;

void syscall_service(void) {
    if (syscall_num == 1) {
        char c = (char)syscall_arg;
        volatile uint16_t* vga = (uint16_t*)0xB8000;
        static uint32_t call_pos = 0;
        int base = 80 * 8;
        if (call_pos >= 80) call_pos = 0;
        vga[base + call_pos++] = (0x0F << 8) | c;
    }
}

void syscall_request(uint32_t num, uint32_t arg) {
    syscall_num = num;
    syscall_arg = arg;
    asm volatile("int $0x80");
}

void keyboard_irq_service(void) {
    pic_send_eoi(1);
    keyboard_service();
}

void syscall_irq_service(void) {
    syscall_service();
}

// Public API to read one character from input buffer (non-blocking)
uint32_t input_getchar_nb(char* c) {
    if (input_head != input_tail) {
        *c = input_buffer[input_tail];
        input_tail = (input_tail + 1) % INPUT_BUFFER_SIZE;
        return 1;
    }
    return 0;
}

// Public API to display a string at a specific row
void display_string(uint32_t row, const char* str) {
    vga_putstring(row, 0, str);
}

// Public API to clear a row
void display_clear_row(uint32_t row) {
    vga_clear_row(row);
}

// Update task display - used by tasks
void display_task_status(const char* name, char status) {
    vga_putstring(display_row_task, 0, name);
    vga_putchar(display_row_task, 40, status);
}
