#include "gdt.h"
#include <stdint.h>

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed));

extern void gdt_flush(const struct gdtr *gdtr);

/* Dedicated stack for double faults (IST1), so a broken kernel stack
 * still produces a readable message instead of a triple fault. */
static uint8_t df_stack[16384] __attribute__((aligned(16)));

static struct tss tss;
static uint64_t gdt[7];   /* null, kcode, kdata, udata, ucode, TSS (2 slots) */

int gdt_on_df_stack(uint64_t addr) {
    return addr >= (uint64_t)df_stack && addr < (uint64_t)df_stack + sizeof df_stack;
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp[0] = rsp0;
}

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;   /* 0x08 kernel code: 64-bit, ring 0 */
    gdt[2] = 0x00CF92000000FFFFULL;   /* 0x10 kernel data */
    gdt[3] = 0x00CFF2000000FFFFULL;   /* 0x18 user data, ring 3 */
    gdt[4] = 0x00AFFA000000FFFFULL;   /* 0x20 user code, ring 3 */
    /* user data sits before user code because SYSRET expects that order */

    tss.iopb = sizeof tss;
    tss.ist[0] = (uint64_t)(df_stack + sizeof df_stack);

    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof tss - 1;
    gdt[5] = (limit & 0xFFFF)
           | ((base & 0xFFFFFF) << 16)
           | (0x89ULL << 40)                 /* present, 64-bit TSS (available) */
           | (((limit >> 16) & 0xF) << 48)
           | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;                     /* upper half of the 16-byte descriptor */

    struct gdtr r = { sizeof gdt - 1, (uint64_t)gdt };
    gdt_flush(&r);
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}
