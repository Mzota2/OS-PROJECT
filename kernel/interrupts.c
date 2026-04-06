// interrupts.c
// Interrupt and Exception Handling
//
// This module sets up the Interrupt Descriptor Table (IDT) to handle:
// - Hardware interrupts (IRQ0 timer, IRQ1 keyboard)
// - Software interrupts (syscalls via int 0x80)
// - CPU exceptions (divide by zero, page fault, etc.)
//
// Also manages:
// - PIC (Programmable Interrupt Controller) remapping
// - PIT (Programmable Interval Timer) initialization
// - Keyboard input buffering
// - Syscall dispatch

#include <stdbool.h>
#include <stdint.h>

#include "idt.h"
#include "scheduler.h"

extern void task_a(void);
extern void task_b(void);
extern void task_c(void);

// ===== SYSCALL MECHANISM =====
// Tasks call syscall(num, arg) which:
// 1. Sets these global variables with syscall number and argument
// 2. Executes "int 0x80" software interrupt
// 3. ISR handler reads these globals and services the request
volatile uint32_t syscall_num;   // Syscall request number
volatile uint32_t syscall_arg;   // Syscall argument

// Wrapper to invoke syscall via software interrupt
void syscall(uint32_t num, uint32_t arg) {
    syscall_num = num;      // Set syscall number
    syscall_arg = arg;      // Set argument
    asm volatile("int $0x80");  // Trigger software interrupt
}

// ===== PIC (8259 INTERRUPT CONTROLLER) PORT ADDRESSES =====
// The PIC manages hardware interrupts from devices.
// Master PIC controls IRQ0-7, Slave PIC controls IRQ8-15.
#define PIC1_COMMAND 0x20   // Master PIC command port
#define PIC1_DATA    0x21   // Master PIC data/mask port
#define PIC2_COMMAND 0xA0   // Slave PIC command port
#define PIC2_DATA    0xA1   // Slave PIC data/mask port

#define PIC_EOI 0x20        // End-of-Interrupt code
#define KBD_DATA_PORT 0x60  // Keyboard data port (scancode)

// ===== IDT AND INTERRUPT STATE =====
static idt_entry_t idt[256];    // Interrupt Descriptor Table (256 possible interrupts)
static idt_ptr_t idt_ptr;       // IDTR register contents (base + limit)

// Timer tracking for scheduler
static volatile uint32_t tick_count = 0;         // Count of timer ticks
static volatile uint32_t pit_raw_ticks = 0;     // Raw PIT interrupt count
static volatile uint32_t pit_effective_hz = 0;  // Actual frequency achieved
static volatile uint32_t scheduler_quantum_ticks = 1;
static volatile uint32_t scheduler_tick_divider = 0;

// ===== KEYBOARD INPUT BUFFERING =====
// When keyboard interrupt fires, scancode is added to ring buffer.
// Tasks can poll this buffer via input_getchar_nb() in tasks.c
#define INPUT_BUFFER_SIZE 256
static volatile char input_buffer[INPUT_BUFFER_SIZE];
static volatile uint32_t input_head = 0;  // Where to write next character
static volatile uint32_t input_tail = 0;  // Where to read next character

// Display output state
static volatile uint32_t display_row_input = 6;  // VGA row for keyboard display
static volatile uint32_t display_row_task = 3;   // VGA row for task display

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
    // The PIC starts with IRQ0-7 mapped to CPU vectors 0-7 (these conflict with CPU exceptions).
    // We need to remap them to vectors 0x20-0x27 (after exceptions).
    //
    // Initialization Command Word (ICW) sequence:
    // ICW1: Signal start of initialization, cascade mode
    // ICW2: Interrupt vector offset (base address)
    // ICW3: Slave PIC configuration
    // ICW4: Mode selection
    
    uint8_t a1 = inb(PIC1_DATA);  // Read current mask
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);  // ICW1: Start init, cascade mode
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);     // ICW2: Master IRQ0-7 map to vectors 32-39 (0x20-0x27)
    outb(PIC2_DATA, 0x28);     // ICW2: Slave IRQ8-15 map to vectors 40-47 (0x28-0x2F)

    outb(PIC1_DATA, 0x04);     // ICW3: Master has slave at IRQ2
    outb(PIC2_DATA, 0x02);     // ICW3: Slave is connected to Master IRQ2

    outb(PIC1_DATA, 0x01);     // ICW4: 8086 mode
    outb(PIC2_DATA, 0x01);

    // Interrupt Mask Register (IMR): Select which IRQs are enabled.
    // Bit=0 means IRQ enabled, Bit=1 means IRQ disabled.
    // 0xFC = 11111100 binary
    //   Bits 0-1 = 0 (IRQ0 timer, IRQ1 keyboard enabled)
    //   Bits 2-7 = 1 (IRQ2-7 disabled)
    outb(PIC1_DATA, 0xFC);     // Master: enable IRQ0, IRQ1; disable others
    outb(PIC2_DATA, 0xFF);     // Slave: disable all IRQs
}

static inline void pic_send_eoi(uint8_t irq) {
    // Send End-of-Interrupt signal to PIC.
    // For IRQs on the slave (8+), we must send EOI to both master and slave.
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);  // Tell Slave we're done
    }
    outb(PIC1_COMMAND, PIC_EOI);      // Tell Master we're done
}

static void set_idt_entry(uint8_t vector, void (*handler)(void), uint16_t selector, uint8_t flags) {
    // Populate one IDT entry with interrupt handler address.
    // IDT entry is 8 bytes (two 16-bit offset parts + selector + flags).
    //
    // Parameters:
    //   vector: interrupt vector number (0-255)
    //   handler: address of interrupt handler function
    //   selector: code segment selector (0x08 for kernel code)
    //   flags: type and attributes (0x8E for interrupt gate)
    
    uint32_t base = (uint32_t)handler;
    idt[vector].offset_low = base & 0xFFFF;          // Low 16 bits of handler address
    idt[vector].selector = selector;                 // Code segment selector
    idt[vector].zero = 0;                            // Reserved field
    idt[vector].type_attr = flags;                   // Type and attributes
    idt[vector].offset_high = (base >> 16) & 0xFFFF; // High 16 bits of handler address
}

static inline void lidt(void) {
    // Load IDT Register (IDTR) with IDT base address and size.
    // The IDTR is a special CPU register that points to our IDT.
    
    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * 256 - 1);  // Size of IDT (256 entries)
    idt_ptr.base = (uint32_t)&idt;                              // Address of IDT
    asm volatile ("lidt (%0)" : : "r"(&idt_ptr));            // Load IDTR
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
