bits 64
section .text
extern interrupt_handler

; Vectors where the CPU pushes an error code itself. For all the others we push
; a dummy 0 so the stack layout (struct interrupt_frame) is always the same.
%macro isr_err 1
isr %+ %1:
    push %1
    jmp isr_common
%endmacro

%macro isr_noerr 1
isr %+ %1:
    push 0
    push %1
    jmp isr_common
%endmacro

%assign i 0
%rep 256
%if i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30
    isr_err i
%else
    isr_noerr i
%endif
%assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp            ; &struct interrupt_frame
    call interrupt_handler
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16             ; drop vector + error code
    iretq

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr %+ i
%assign i i+1
%endrep
