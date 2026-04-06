global irq0_handler_asm
global irq1_handler_asm
global syscall_handler_asm
global isr_stub_noerr
global isr_stub_err

extern pit_timer_service
extern keyboard_irq_service
extern syscall_irq_service

section .text

; Generic unhandled exception stubs
; Two variants: without and with CPU-pushed error code.
; These halt the CPU to avoid triple-fault reboot loops.
isr_stub_noerr:
    cli
.halt_noerr:
    hlt
    jmp .halt_noerr

isr_stub_err:
    ; Stack on entry (top -> bottom):
    ; error_code, EIP, CS, EFLAGS
    ; Drop the error code so EIP is on top (in case someone iret's here),
    ; but we will halt anyway for safety.
    add esp, 4
    cli
.halt_err:
    hlt
    jmp .halt_err

; IRQ0 interrupt handler wrapper (PIT timer)
; Runs in protected mode, then returns with IRET.
irq0_handler_asm:
    push ds
    push es
    push fs
    push gs
    pusha              ; save general purpose registers
    ; set data segments to kernel data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    call pit_timer_service
    popa               ; restore registers
    pop gs
    pop fs
    pop es
    pop ds
    iret               ; return from interrupt

; IRQ1 interrupt handler wrapper (keyboard)
irq1_handler_asm:
    push ds
    push es
    push fs
    push gs
    pusha
    ; set data segments to kernel data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    call keyboard_irq_service
    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret

; Syscall interrupt handler wrapper (int 0x80)
syscall_handler_asm:
    pusha
    call syscall_irq_service
    popa
    iret
