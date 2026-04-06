; context_switch.asm
; Contains assembly routines for task context switching.
; Context switching saves one task's CPU state and restores another's.
; This allows the kernel to switch between multiple running tasks.
;
; void context_switch_asm(task_t* prev, task_t* next);
;   - Saves current task's CPU state (registers, stack pointer, instruction pointer)
;   - Restores next task's CPU state
;   - Never returns to caller (execution jumps to next task's code)
;
; Calling convention: cdecl (32-bit C calling convention)
;   - Parameters on stack: [esp+4]=prev, [esp+8]=next, [esp]=return address

BITS 32

global context_switch_asm
global start_first_task_asm

; Offsets within cpu_context_t structure (must match scheduler.h definitions)
; These offsets tell us where each register is stored in the struct
%define OFF_EAX     0           ; General purpose register A
%define OFF_EBX     4           ; General purpose register B (callee-saved)
%define OFF_ECX     8           ; General purpose register C
%define OFF_EDX     12          ; General purpose register D
%define OFF_ESI     16          ; Source index (callee-saved)
%define OFF_EDI     20          ; Destination index (callee-saved)
%define OFF_EBP     24          ; Base pointer / frame pointer (callee-saved)
%define OFF_ESP     28          ; Stack pointer
%define OFF_EIP     32          ; Instruction pointer (where to resume)
%define OFF_EFLAGS  36          ; CPU flags register

section .text

context_switch_asm:
    ; Input (on stack for cdecl calling convention):
    ;   [esp+0] = return address to caller
    ;   [esp+4] = prev: pointer to previous task's task_t structure
    ;   [esp+8] = next: pointer to next task's task_t structure
    
    ; Load task pointers and return address into registers
    mov     eax, [esp + 4]          ; EAX = prev task_t*
    mov     edx, [esp + 8]          ; EDX = next task_t*
    mov     ecx, [esp]              ; ECX = return EIP (where caller called us from)

    ; Skip to cpu_context_t inside task_t (task_t has an 8-byte header before context)
    add     eax, 8                  ; EAX now points to prev->context
    add     edx, 8                  ; EDX now points to next->context

    ; =======================================================================
    ; SAVE PREVIOUS TASK'S CONTEXT
    ; =======================================================================
    ; We only save callee-saved registers (those WE must preserve).
    ; Caller-saved registers (EAX, ECX, EDX) don't need saving here.
    
    mov     [eax + OFF_EBX], ebx    ; Save EBX (callee-saved)
    mov     [eax + OFF_ESI], esi    ; Save ESI (callee-saved)
    mov     [eax + OFF_EDI], edi    ; Save EDI (callee-saved)
    mov     [eax + OFF_EBP], ebp    ; Save EBP (callee-saved, frame pointer)
    mov     [eax + OFF_EIP], ecx    ; Save EIP (return address - where to resume)
    
    ; Save CPU flags
    pushfd                          ; Push EFLAGS onto stack
    pop     ecx                     ; Pop into ECX
    mov     [eax + OFF_EFLAGS], ecx ; Save EFLAGS
    
    ; Save ESP (stack pointer). We must save it as if we're about to return,
    ; so it should point to the return address slot on the stack.
    lea     ecx, [esp + 4]          ; ECX = ESP + 4 (skip return addr)
    mov     [eax + OFF_ESP], ecx    ; Save ESP

    ; =======================================================================
    ; RESTORE NEXT TASK'S CONTEXT
    ; =======================================================================
    ; Load all CPU state for the next task
    
    mov     ebx, [edx + OFF_EBX]    ; Restore EBX
    mov     esi, [edx + OFF_ESI]    ; Restore ESI
    mov     edi, [edx + OFF_EDI]    ; Restore EDI
    mov     ebp, [edx + OFF_EBP]    ; Restore EBP
    mov     esp, [edx + OFF_ESP]    ; Restore stack pointer (switch stacks!)
    
    ; Restore flags
    mov     ecx, [edx + OFF_EFLAGS] ; Load EFLAGS into temp
    push    ecx                     ; Push onto stack
    popfd                           ; Pop from stack into EFLAGS register
    
    ; Jump to next task's instruction pointer
    ; This is the magic: we load EIP and jump to it,
    ; so execution continues in the next task as if it never stopped.
    mov     ecx, [edx + OFF_EIP]    ; Load EIP of next task
    jmp     ecx                     ; Jump to next task (never returns here)

; void start_first_task_asm(uint32_t initial_esp);
; Special function to start the first task.
; Because the first task has no previous context to restore from,
; we instead use a synthetic interrupt frame and IRET to enter it.
;
; Input: [esp+4] = initial_esp (stack pointer of first task with synthetic frame)
start_first_task_asm:
    mov     esp, [esp + 4]          ; Load the initial stack pointer
    popa                            ; Restore all general purpose registers from stack
    iret                            ; Return from interrupt (loads CS, EIP, EFLAGS from stack)

; Mark stack as non-executable for security (for GNU tools)
section .note.GNU-stack noalloc noexec nowrite progbits
