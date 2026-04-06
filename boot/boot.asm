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
extern __bss_start
extern __bss_end

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text

_start:
    cli
    ; Set a known-good kernel stack before entering C code.
    mov esp, stack_top

    ; Zero .bss so static globals start in a deterministic state.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    shr ecx, 2
    rep stosd

    call kernel_main
.hang:
    hlt
    jmp .hang
