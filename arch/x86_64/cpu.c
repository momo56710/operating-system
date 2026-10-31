#include "cpu.h"
#include "kprintf.h"

uint8_t fpu_default_state[FPU_STATE_SIZE] __attribute__((aligned(16)));
int cpu_smep, cpu_smap;

static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static inline uint64_t read_cr4(void) { uint64_t v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v; }
static inline void write_cr4(uint64_t v) { __asm__ volatile ("mov %0, %%cr4" : : "r"(v)); }

void fpu_save(void *area) { __asm__ volatile ("fxsave (%0)" : : "r"(area) : "memory"); }
void fpu_restore(const void *area) { __asm__ volatile ("fxrstor (%0)" : : "r"(area) : "memory"); }

/* Turn on SSE for user programs and record a clean register state to start each of them with.
 * The kernel itself is built without SSE, so it never touches these registers. */
void fpu_init(void) {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);                         /* EM off: there is an FPU */
    cr0 |= 1ULL << 1;                            /* MP on */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    write_cr4(read_cr4() | (1ULL << 9) | (1ULL << 10));      /* OSFXSR, OSXMMEXCPT */
    __asm__ volatile ("fninit");
    uint32_t mxcsr = 0x1F80;                     /* all SSE exceptions masked, round to nearest */
    __asm__ volatile ("ldmxcsr %0" : : "m"(mxcsr));
    fpu_save(fpu_default_state);
}

void cpu_harden_init(void) {
    uint32_t a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d);
    if (a >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        cpu_smep = (b >> 7) & 1;
        cpu_smap = (b >> 20) & 1;
    }
    uint64_t cr4 = read_cr4();
    if (cpu_smep) cr4 |= 1ULL << 20;
    if (cpu_smap) cr4 |= 1ULL << 21;
    write_cr4(cr4);
    kprintf("CPU: FPU/SSE state saved per task, SMEP %s, SMAP %s\n", cpu_smep ? "on" : "not supported", cpu_smap ? "on" : "not supported");
}
