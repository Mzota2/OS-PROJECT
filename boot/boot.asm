; boot.asm
; Minimal Multiboot2 header and entry point
; This file is the first code executed by GRUB. It sets up the multiboot header
; (so GRUB recognizes us), initializes the stack, zeros the .bss section,
; and jumps to kernel_main().

section .multiboot
align 8
mb_header_start:
    dd 0xE85250D6              ; Multiboot2 magic number (required by GRUB)
    dd 0                       ; Architecture (0 = i386 32-bit)
    dd mb_header_end - mb_header_start ; Total header length
    dd -(0xE85250D6 + 0 + (mb_header_end - mb_header_start)) ; Checksum (magic + arch + length = -checksum)
    
    ; End tag (required by Multiboot2 spec)
    dw 0                       ; Type: end (terminates header)
    dw 0                       ; Flags
    dd 8                       ; Size of end tag structure
mb_header_end:

section .text
global _start
extern kernel_main          ; Defined in kernel.c
extern __bss_start          ; Linker-provided BSS section start
extern __bss_end            ; Linker-provided BSS section end

section .bss
align 16                    ; Align stack to 16-byte boundary for ABI compliance
stack_bottom:
    resb 16384              ; Reserve 16KB kernel stack
stack_top:

section .text

_start:
    ; Entry point called by GRUB in 32-bit protected mode
    cli                         ; Disable interrupts for setup (will be enabled later)
    
    ; Set a known-good kernel stack before entering C code.
    ; This is critical: C code uses ESP for local variables and function calls.
    mov esp, stack_top

    ; Zero .bss section so static globals start in a deterministic state.
    ; The .bss section holds uninitialized static/global variables, which should be zero.
    mov edi, __bss_start        ; EDI = start of .bss
    mov ecx, __bss_end          ; ECX = end of .bss
    sub ecx, edi                ; ECX = size of .bss in bytes
    xor eax, eax                ; EAX = 0 (value to fill with)
    shr ecx, 2                  ; ECX = size in 32-bit words (divide by 4)
    rep stosd                   ; Fill .bss with zeros (repeat store DWORD)

    ; Jump to kernel_main() in kernel.c to continue initialization
    call kernel_main
    
.hang:
    ; If kernel_main() ever returns, enter infinite loop
    hlt                         ; Halt CPU (saves power)
    jmp .hang                   ; Jump back to halt (safety loop)
