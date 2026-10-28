#include "interrupt.h"
#include "pic.h"
#include "io.h"
#include "kprintf.h"

static const char *exception_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor overrun",
    "Invalid TSS", "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved", "x87 floating point", "Alignment check", "Machine check",
    "SIMD floating point", "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor injection",
    "VMM communication", "Security", "Reserved"
};

static irq_handler_t irq_handlers[16];

void irq_register(int irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

static uint64_t read_cr(int n) {
    uint64_t v = 0;
    if (n == 2) __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    if (n == 3) __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void exception(struct interrupt_frame *f) {
    kprintf("\n*** EXCEPTION %lu: %s ***\n", f->vector, exception_names[f->vector]);

    if (f->vector == 14) {
        uint64_t e = f->error;
        kprintf("PAGE FAULT at 0x%lx (%s, %s, %s%s)\n", read_cr(2),
                (e & 1)  ? "protection violation" : "page not present",
                (e & 2)  ? "write" : "read",
                (e & 4)  ? "user" : "kernel",
                (e & 16) ? ", instruction fetch" : "");
    }

    kprintf("error=%016lx rip=%016lx cs=%04lx\n", f->error, f->rip, f->cs);
    kprintf("rflags=%016lx rsp=%016lx ss=%04lx\n", f->rflags, f->rsp, f->ss);
    kprintf("rax=%016lx rbx=%016lx rcx=%016lx\n", f->rax, f->rbx, f->rcx);
    kprintf("rdx=%016lx rsi=%016lx rdi=%016lx\n", f->rdx, f->rsi, f->rdi);
    kprintf("rbp=%016lx r8 =%016lx r9 =%016lx\n", f->rbp, f->r8, f->r9);
    kprintf("r10=%016lx r11=%016lx r12=%016lx\n", f->r10, f->r11, f->r12);
    kprintf("r13=%016lx r14=%016lx r15=%016lx\n", f->r13, f->r14, f->r15);
    kprintf("cr2=%016lx cr3=%016lx\n", read_cr(2), read_cr(3));
    kprintf("System halted.\n");
    halt_forever();
}

void interrupt_handler(struct interrupt_frame *f) {
    if (f->vector < 32) {
        exception(f);
    } else if (f->vector < IRQ_BASE + 16) {
        int irq = f->vector - IRQ_BASE;
        if (pic_is_spurious(irq)) return;
        if (irq_handlers[irq]) irq_handlers[irq]();
        pic_eoi(irq);
    }
}
