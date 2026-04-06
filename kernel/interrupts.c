#include <stdbool.h>
#include <stdint.h>

#include "idt.h"
#include "scheduler.h"

extern void task_a(void);
extern void task_b(void);
extern void task_c(void);

volatile uint32_t syscall_num;
volatile uint32_t syscall_arg;

void syscall(uint32_t num, uint32_t arg) {
    syscall_num = num;
    syscall_arg = arg;
    asm volatile("int $0x80");
}

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

// Serial functions (declared in kernel.c)
extern void serial_print(const char* s);
extern void serial_putchar(char c);

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
    // 0xFC = 11111100 (unmask IRQ0 and IRQ1).
    outb(PIC1_DATA, 0xFC); // 11111100 - IRQ0 and IRQ1 enabled
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

    // PIT channel 0 divisor is 16-bit. Limit to achievable frequencies.
    uint32_t divisor = 1193180 / frequency;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 65535) {
        // Can't achieve requested frequency; use maximum (slowest) we can
        divisor = 65535;
    }

    pit_effective_hz = 1193180 / divisor;
    if (pit_effective_hz == 0) {
        pit_effective_hz = 18;
    }

    // Display tick on every interrupt, scheduler quantum based on actual Hz
    scheduler_quantum_ticks = 1;  // Always display every tick
    scheduler_tick_divider = 0;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void pit_tick_handler(void) {
    tick_count++;

    volatile uint16_t* vga = (uint16_t*)0xB8000;
    
    // Row 1 (index 80): Display tick count
    const char* text = "Tick: ";
    int base = 80;
    
    for (int i = 0; text[i] != '\0'; i++) {
        vga[base + i] = (0x0F << 8) | text[i];
    }

    // Simple decimal display (up to 5 digits)
    uint32_t n = tick_count;
    char buf[6];
    int pos = 5;
    buf[pos] = '\0';
    
    if (n == 0) {
        buf[0] = '0';
        pos = 1;
    } else {
        while (n > 0 && pos > 0) {
            buf[--pos] = '0' + (n % 10);
            n /= 10;
        }
    }

    int col = base + 6;
    for (int i = pos; i < 6 && col < base + 10; i++) {
        vga[col++] = (0x0F << 8) | buf[i];
    }
    
    // Clear rest of display area
    while (col < base + 15) {
        vga[col++] = (0x0F << 8) | ' ';
    }

    // Toggle indicator at row 1, col 40 for visible heartbeat
    static int toggle = 0;
    toggle = !toggle;
    vga[1 * 80 + 40] = (0x0F << 8) | (toggle ? 'X' : 'O');
}

// Called from assembly ISR wrapper
void pit_timer_service(void) {
    // Acknowledge the timer interrupt on the PIC first
    pic_send_eoi(0);

    // Count every hardware PIT interrupt
    pit_raw_ticks++;
    tick_count++;  // Increment tick count directly (polling loop will display)
    
    // Debug: print every 10 ticks to serial
    if (tick_count % 10 == 0) {
        // Simple serial output for ISR debug
        static const char* msg = "[ISR] Tick: ";
        const char* p = msg;
        while (*p) {
            while ((inb(0x3F8 + 5) & 0x20) == 0);
            outb(0x3F8, *p);
            p++;
        }
        // Print tick count
        uint32_t n = tick_count;
        if (n == 0) {
            while ((inb(0x3F8 + 5) & 0x20) == 0);
            outb(0x3F8, '0');
        } else {
            char buf[16];
            int len = 0;
            while (n > 0) {
                buf[len++] = '0' + (n % 10);
                n /= 10;
            }
            for (int i = len - 1; i >= 0; i--) {
                while ((inb(0x3F8 + 5) & 0x20) == 0);
                outb(0x3F8, buf[i]);
            }
        }
        while ((inb(0x3F8 + 5) & 0x20) == 0);
        outb(0x3F8, '\n');
    }
    
    // Don't call scheduler_tick() yet - test if ISR works at all
    scheduler_tick();
}

// Expose tick count for polling from main loop
uint32_t timer_get_ticks(void) {
    return tick_count;
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
                // Debug: print to serial on key press
                serial_print("[KBD] Pressed: ");
                serial_putchar(c);
                serial_print("\n");
            }
        }
    }

    // Display the input buffer on row 2
    volatile uint16_t* input_row = (volatile uint16_t*)0xB8000 + 160;
    const char* input_label = "Keyboard: ";
    int col = 0;
    for (int i = 0; input_label[i] != '\0'; i++) {
        input_row[col++] = (0x0F << 8) | input_label[i];
    }
    
    // Show current input buffer contents
    uint32_t temp_tail = input_tail;
    while (temp_tail != input_head && col < 80) {
        input_row[col++] = (0x0F << 8) | input_buffer[temp_tail];
        temp_tail = (temp_tail + 1) % INPUT_BUFFER_SIZE;
    }
    
    // Clear rest of row
    while (col < 80) {
        input_row[col++] = (0x0F << 8) | ' ';
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
    } else if (syscall_num == 2) { // spawn (create_thread)
        // For demo: arg 0 = spawn task_a, arg 1 = spawn task_b, etc.
        extern void task_a(void);
        extern void task_b(void);
        extern void task_c(void);
        if (syscall_arg == 0) {
            scheduler_add_task(task_a, "Syscall A");
        } else if (syscall_arg == 1) {
            scheduler_add_task(task_b, "Syscall B");
        } else if (syscall_arg == 2) {
            scheduler_add_task(task_c, "Syscall C");
        }
    } else if (syscall_num == 3) { // exit
        task_t* current = scheduler_current_task();
        if (current) {
            current->state = TASK_DEAD;
            scheduler_yield();
        }
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

// Display current running task on row 4
void display_current_task(const char* name) {
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    const char* label = "RUN: ";
    int row_offset = 4 * 80;
    
    int col = 0;
    for (int i = 0; label[i] != '\0'; i++) {
        vga[row_offset + col++] = (0x0F << 8) | label[i];
    }
    
    for (int i = 0; name && name[i] != '\0' && col < 80; i++) {
        vga[row_offset + col++] = (0x0F << 8) | name[i];
    }
    
    // Clear rest of row
    while (col < 80) {
        vga[row_offset + col++] = (0x0F << 8) | ' ';
    }
}
