#pragma once
#include <stdint.h>

/* Spinlocks that also turn interrupts off while held, so a timer tick can never
 * switch away from the holder. On one CPU the spin never actually waits. */
typedef struct { volatile int locked; } spinlock_t;

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & 0x200) __asm__ volatile ("sti" : : : "memory");
}

static inline uint64_t spin_lock_irqsave(spinlock_t *l) {
    uint64_t flags = irq_save();
    while (__atomic_exchange_n(&l->locked, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile ("pause");
    return flags;
}

static inline void spin_unlock(spinlock_t *l) {
    __atomic_store_n(&l->locked, 0, __ATOMIC_RELEASE);
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    spin_unlock(l);
    irq_restore(flags);
}
