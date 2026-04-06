; context_switch.asm
; void context_switch_asm(task_t* prev, task_t* next);
; Save callee-saved regs + EIP, ESP, EFLAGS to support cooperative yield.

BITS 32

global context_switch_asm
global start_first_task_asm

; Offsets within cpu_context_t (must match scheduler.h)
%define OFF_EAX     0
%define OFF_EBX     4
%define OFF_ECX     8
%define OFF_EDX     12
%define OFF_ESI     16
%define OFF_EDI     20
%define OFF_EBP     24
%define OFF_ESP     28
%define OFF_EIP     32
%define OFF_EFLAGS  36

section .text

; cdecl: [esp+4]=prev, [esp+8]=next, [esp]=return addr to caller
context_switch_asm:
	; eax = prev, edx = next, ecx = ret_eip (temporaries)
	mov     eax, [esp + 4]          ; prev
	mov     edx, [esp + 8]          ; next
	mov     ecx, [esp]              ; return EIP of caller

	; Save previous context (callee-saved regs + eip, eflags, esp)
	mov     [eax + OFF_EBX], ebx
	mov     [eax + OFF_ESI], esi
	mov     [eax + OFF_EDI], edi
	mov     [eax + OFF_EBP], ebp
	mov     [eax + OFF_EIP], ecx
	pushfd
	pop     ecx
	mov     [eax + OFF_EFLAGS], ecx
	lea     ecx, [esp + 4]          ; esp as if after 'ret'
	mov     [eax + OFF_ESP], ecx

	; Restore next context (callee-saved regs, eflags, esp, then jump to eip)
	mov     ebx, [edx + OFF_EBX]
	mov     esi, [edx + OFF_ESI]
	mov     edi, [edx + OFF_EDI]
	mov     ebp, [edx + OFF_EBP]
	mov     ecx, [edx + OFF_EFLAGS]
	push    ecx
	popfd
	mov     esp, [edx + OFF_ESP]
	mov     ecx, [edx + OFF_EIP]
	push    ecx
	ret

; void start_first_task_asm(uint32_t initial_esp);
; Enter the first scheduled task by restoring a synthetic interrupt frame.
start_first_task_asm:
    mov     esp, [esp + 4]
    popa
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
