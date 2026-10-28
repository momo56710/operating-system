bits 64
section .text

; void gdt_flush(const struct gdtr *gdtr)
; Loads the new GDT, then reloads every segment register.
global gdt_flush
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10            ; kernel data selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax            ; fs/gs: null for now
    mov fs, ax
    mov gs, ax
    pop rax                 ; return address
    push 0x08               ; kernel code selector
    push rax
    retfq                   ; far return = reload CS and jump back
