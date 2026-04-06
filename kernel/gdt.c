#include "gdt.h"

static gdt_entry_t gdt[3];
static gdt_ptr_t gdt_ptr;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].limit_high_and_gran = ((limit >> 16) & 0x0F) | (gran & 0xF0);

    gdt[num].flags = access;
}

static void _load_gdt(void) {
    asm volatile ("lgdt (%0)" : : "r"(&gdt_ptr));
}

void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 3) - 1;
    gdt_ptr.base = (uint32_t)&gdt[0];

    // Null descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // Code descriptor (offset 0x08)
    // Base=0, Limit=0xFFFFFFFF, flags=present+ring0+code, gran=4KB granularity+32-bit
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // Data descriptor (offset 0x10)
    // Base=0, Limit=0xFFFFFFFF, flags=present+ring0+data, gran=4KB granularity+32-bit
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    _load_gdt();

    // Update segment registers
    asm volatile ("mov $0x10, %%ax; mov %%ax, %%ds; mov %%ax, %%es; mov %%ax, %%fs; mov %%ax, %%gs; mov %%ax, %%ss" : : : "%ax");

    // Far jump to update cs
    asm volatile ("ljmp $0x08, $continue; continue:" : : : "memory");
}
