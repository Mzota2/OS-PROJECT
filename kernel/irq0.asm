; irq0.asm
; Assembly interrupt and exception handlers.
; These wrappers prepare the CPU state before calling C ISR service functions.
; Wrappers preserve registers, set segment registers, then call C code.

global irq0_handler_asm       ; PIT timer interrupt handler
global irq1_handler_asm       ; Keyboard interrupt handler
global syscall_handler_asm    ; Syscall (int 0x80) handler
global isr_stub_noerr        ; Generic exception handler (no error code)
global isr_stub_err          ; Generic exception handler (with error code)

extern pit_timer_service     ; C function to handle timer IRQ (interrupts.c)
extern keyboard_irq_service  ; C function to handle keyboard IRQ (interrupts.c)
extern syscall_irq_service   ; C function to handle syscalls (interrupts.c)

section .text

; =========================================================================
; EXCEPTION HANDLERS FOR UNHANDLED ERRORS
; =========================================================================
; When CPU encounters an unhandled exception, we halt to prevent reboots.
; Two variants: some exceptions push error codes, some don't.

; isr_stub_noerr: for exceptions without error codes
; Examples: divide by zero, invalid opcode, page fault during setup
isr_stub_noerr:
    cli                         ; Disable interrupts immediately
.halt_noerr:
    hlt                         ; Halt CPU (stop execution)
    jmp .halt_noerr             ; Safety: loop in case something wakes us

; isr_stub_err: for exceptions that pushed an error code
; Examples: double fault, invalid TSS, stack segment fault
isr_stub_err:
    ; Stack layout when this is called:
    ;   ESP+0: Error code (pushed by CPU)
    ;   ESP+4: EIP (pushed by CPU)
    ;   ESP+8: CS (pushed by CPU)
    ;   ESP+12: EFLAGS (pushed by CPU)
    ; We skip the error code to clean the stack before halting
    add esp, 4                  ; Skip error code (pop it without using it)
    cli                         ; Disable interrupts
.halt_err:
    hlt                         ; Halt CPU
    jmp .halt_err               ; Safety: loop

; =========================================================================
; IRQ0 HANDLER (PIT TIMER INTERRUPT)
; =========================================================================
; Called 18 times per second (configured frequency).
; Acknowledges the interrupt and calls C code to handle it.
irq0_handler_asm:
    ; Preserve segment registers (DS, ES, FS, GS)
    push ds                     ; Save old DS
    push es                     ; Save old ES
    push fs                     ; Save old FS
    push gs                     ; Save old GS
    
    pusha                       ; Save all general purpose registers (EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI)
    
    ; Set data segments to kernel data selector (GDT entry 2, 0x10)
    ; This ensures we're using kernel-valid memory segments
    mov ax, 0x10                ; Kernel data selector
    mov ds, ax
    mov es, ax
    
    ; Call C function to handle timer interrupt
    call pit_timer_service      ; Defined in interrupts.c
    
    ; Restore all registers (reverse order of saves)
    popa                        ; Restore general purpose registers
    pop gs                      ; Restore GS
    pop fs                      ; Restore FS
    pop es                      ; Restore ES
    pop ds                      ; Restore DS
    
    iret                        ; Return from interrupt (pops EIP, CS, EFLAGS)

; =========================================================================
; IRQ1 HANDLER (KEYBOARD INTERRUPT)
; =========================================================================
; Called when user presses a key.
; Reads scancode and buffers the character.
irq1_handler_asm:
    ; Preserve segment registers
    push ds
    push es
    push fs
    push gs
    
    pusha                       ; Save all general purpose registers
    
    ; Set kernel data segments
    mov ax, 0x10                ; Kernel data selector
    mov ds, ax
    mov es, ax
    
    ; Call C function to handle keyboard interrupt
    call keyboard_irq_service   ; Defined in interrupts.c
    
    ; Restore all registers
    popa
    pop gs
    pop fs
    pop es
    pop ds
    
    iret                        ; Return from interrupt

; =========================================================================
; SYSCALL HANDLER (INT 0x80 SOFTWARE INTERRUPT)
; =========================================================================
; User tasks execute "int 0x80" to request kernel services.
; This handler saves state and calls C code to process the request.
syscall_handler_asm:
    pusha                       ; Save all general purpose registers
    
    ; Call C function to handle syscall
    call syscall_irq_service    ; Defined in interrupts.c
    
    popa                        ; Restore all general purpose registers
    
    iret                        ; Return from interrupt
