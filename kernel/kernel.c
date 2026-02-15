// kernel.c

#include <stdint.h>

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;

void print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_buffer[i] = (0x0F << 8) | str[i];
    }
}

void kernel_main() {
    print("Hello from my OS!");
}

