#pragma once
#include <stdint.h>

/* Layout matches the push order in arch/x86_64/isr.asm. */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;   /* pushed by the CPU */
};

typedef void (*irq_handler_t)(void);

void irq_register(int irq, irq_handler_t handler);
void interrupt_handler(struct interrupt_frame *f);
