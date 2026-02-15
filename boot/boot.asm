; boot.asm
; Minimal Multiboot2 header and entry point

section .multiboot
align 8
mb_header_start:
    dd 0xE85250D6              ; Multiboot2 magic
    dd 0                       ; Architecture (0 = i386)
    dd mb_header_end - mb_header_start ; Total header length
    dd -(0xE85250D6 + 0 + (mb_header_end - mb_header_start)) ; Checksum
    
    ; End tag (required)
    dw 0                       ; Type: end
    dw 0                       ; Flags
    dd 8                       ; Size of end tag
mb_header_end:

section .text
global _start
extern kernel_main

_start:
    cli
    call kernel_main
.hang:
    hlt
    jmp .hang
