global irq0_handler_asm
global irq1_handler_asm
global syscall_handler_asm
global isr_stub_noerr
global isr_stub_err

extern pit_timer_service
extern keyboard_irq_service
extern syscall_irq_service
extern scheduler_on_timer_isr

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
    pusha              ; save general purpose registers
    call pit_timer_service
    mov eax, esp       ; current task interrupt-frame stack pointer
    push eax
    call scheduler_on_timer_isr
    add esp, 4
    mov esp, eax       ; switch to selected task's interrupt frame
    popa               ; restore registers from selected task
    iret               ; return from interrupt

; IRQ1 interrupt handler wrapper (keyboard)
irq1_handler_asm:
    pusha
    call keyboard_irq_service
    popa
    iret

; Syscall interrupt handler wrapper (int 0x80)
syscall_handler_asm:
    pusha
    call syscall_irq_service
    popa
    iret
