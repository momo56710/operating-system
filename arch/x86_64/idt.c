#include "idt.h"
#include "gdt.h"
#include <stdint.h>

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;         /* low 3 bits: IST index */
    uint8_t  type_attr;   /* 0x8E = present, ring 0, interrupt gate */
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void *isr_stub_table[256];

static struct idt_entry idt[256];

static void set_gate(int v, void *handler, uint8_t ist) {
    uint64_t a = (uint64_t)handler;
    idt[v].offset_lo  = a & 0xFFFF;
    idt[v].selector   = KERNEL_CS;
    idt[v].ist        = ist;
    idt[v].type_attr  = 0x8E;
    idt[v].offset_mid = (a >> 16) & 0xFFFF;
    idt[v].offset_hi  = a >> 32;
    idt[v].zero       = 0;
}

void idt_set_present(int v, int present) {
    if (present) idt[v].type_attr |= 0x80;
    else         idt[v].type_attr &= 0x7F;
}

void idt_init(void) {
    for (int v = 0; v < 256; v++)
        set_gate(v, isr_stub_table[v], v == 8 ? 1 : 0);   /* double fault uses IST1 */

    struct idtr r = { sizeof idt - 1, (uint64_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(r));
}
