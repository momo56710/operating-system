bits 64
section .text
extern syscall_dispatch
extern syscall_kstack          ; kernel stack top of the running task, kept up to date by schedule()

section .bss
user_rsp_tmp: resq 1

section .text
; The CPU jumps here on `syscall` from ring 3 with: RCX = user RIP, R11 = user RFLAGS,
; RSP = still the USER stack, interrupts masked (SFMASK). Switch stacks first, then build
; the same frame an interrupt would, so one return path (iretq) serves every way into the kernel.
global syscall_entry
syscall_entry:
    mov [rel user_rsp_tmp], rsp
    mov rsp, [rel syscall_kstack]
    push 0x1B                   ; ss
    push qword [rel user_rsp_tmp]
    push r11                    ; rflags
    push 0x23                   ; cs
    push rcx                    ; rip
    push 0                      ; error code (none)
    push 0x80                   ; vector (not a real interrupt)
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
    sti                         ; blocking calls like sleep and wait may be preempted
    mov rdi, rsp
    call syscall_dispatch
    cli
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
