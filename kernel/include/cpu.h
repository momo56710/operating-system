#pragma once
#include <stdint.h>

/* FPU / SSE state: each user task has a 512-byte FXSAVE area that is swapped on every context switch. */
#define FPU_STATE_SIZE 512
extern uint8_t fpu_default_state[FPU_STATE_SIZE];   /* what a fresh program starts with */
void fpu_init(void);
void fpu_save(void *area);
void fpu_restore(const void *area);

/* Supervisor protections. SMEP: the kernel cannot run user code. SMAP: the kernel cannot read or write
 * user memory except inside copy_from_user and friends, which open a window with stac/clac. */
extern int cpu_smep, cpu_smap;
void cpu_harden_init(void);

#ifdef SMAP_TEST                       /* test switch: never open the window, so the first user access must fault */
static inline void user_access_begin(void) {}
static inline void user_access_end(void) {}
#else
static inline void user_access_begin(void) { if (cpu_smap) __asm__ volatile ("stac" : : : "memory"); }
static inline void user_access_end(void)   { if (cpu_smap) __asm__ volatile ("clac" : : : "memory"); }
#endif
