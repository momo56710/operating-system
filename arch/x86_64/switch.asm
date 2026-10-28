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
