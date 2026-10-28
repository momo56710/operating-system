#include "panic.h"
#include "kprintf.h"
#include "io.h"
#include <stdint.h>

__attribute__((noreturn))
void panic(const char *fmt, ...) {
    uint64_t rsp, rbp, rflags, cr0, cr2, cr3, cr4;
    __asm__ volatile ("cli");
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    kprintf_force_unlock();
    kprintf("\n*** KERNEL PANIC: ");
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf(" ***\n");
    kprintf("caller=%p rsp=%p rbp=%p\n", __builtin_return_address(0), (void *)rsp, (void *)rbp);
    kprintf("rflags=%016lx cr0=%016lx cr2=%016lx\n", rflags, cr0, cr2);
    kprintf("cr3=%016lx cr4=%016lx\n", cr3, cr4);
    kprintf("System halted.\n");
    halt_forever();
}
