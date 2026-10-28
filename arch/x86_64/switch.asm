bits 64
section .text

; void switch_to(uint64_t *save_rsp, uint64_t new_rsp)
; Saves the callee-saved registers on the current stack, stores the stack pointer in
; *save_rsp, switches to new_rsp and restores from there. The `ret` then resumes whatever
; the new task was doing when it was switched out, or enters task_trampoline for a new one.
global switch_to
switch_to:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; First code a new task runs. task_create put the entry function in r12 and its argument in r13.
extern task_exit
global task_trampoline
task_trampoline:
    sti                     ; we arrive here from schedule() with interrupts off
    mov rdi, r13
    call r12
    call task_exit
    ud2

; void enter_user(uint64_t rip, uint64_t rsp)   (does not return)
; Drops to ring 3 with an iretq frame: SS, RSP, RFLAGS (IF set), CS, RIP.
global enter_user
enter_user:
    cli
    push 0x1B               ; user data selector | RPL 3
    push rsi                ; user stack pointer
    push 0x202              ; RFLAGS: IF set, reserved bit 1
    push 0x23               ; user code selector | RPL 3
    push rdi                ; user entry point
    xor eax, eax            ; no kernel values leak into user registers
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

; void resume_user(struct interrupt_frame *f)   (does not return)
; Restores a saved frame and returns to ring 3. Used for the child side of fork.
global resume_user
resume_user:
    cli
    mov rsp, rdi
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
    add rsp, 16
    iretq
