#include "kstack.h"
#include "pmm.h"
#include "vmm.h"
#include "spinlock.h"
#include "string.h"

#define KSTACK_BASE  0xFFFFC80000000000ULL
#define KSTACK_SLOTS 512
#define KSTACK_PAGES 4                                   /* 16 KiB of stack */
#define SLOT_BYTES   ((KSTACK_PAGES + 1) * 4096ULL)      /* plus one guard page at the bottom */

static uint8_t used[KSTACK_SLOTS];
static spinlock_t lock;

static uint64_t slot_base(int slot) { return KSTACK_BASE + (uint64_t)slot * SLOT_BYTES; }

int kstack_alloc(uint64_t *top) {
    uint64_t f = spin_lock_irqsave(&lock);
    int slot = -1;
    for (int i = 0; i < KSTACK_SLOTS; i++) if (!used[i]) { slot = i; used[i] = 1; break; }
    spin_unlock_irqrestore(&lock, f);
    if (slot < 0) return -1;

    uint64_t base = slot_base(slot);
    for (int p = 0; p < KSTACK_PAGES; p++) {             /* page 0 of the slot is the guard and stays unmapped */
        uint64_t frame = pmm_alloc_frame();
        if (!frame || vmm_map(base + 4096 + (uint64_t)p * 4096, frame, VMM_WRITE | VMM_NX) < 0) {
            if (frame) pmm_free_frame(frame);
            for (int q = 0; q < p; q++) pmm_free_frame(vmm_unmap(base + 4096 + (uint64_t)q * 4096));
            f = spin_lock_irqsave(&lock); used[slot] = 0; spin_unlock_irqrestore(&lock, f);
            return -1;
        }
        memset(phys_to_virt(frame), 0, 4096);
    }
    *top = base + SLOT_BYTES;
    return slot;
}

void kstack_free(int slot) {
    if (slot < 0 || slot >= KSTACK_SLOTS) return;
    uint64_t base = slot_base(slot);
    for (int p = 0; p < KSTACK_PAGES; p++) {
        uint64_t phys = vmm_unmap(base + 4096 + (uint64_t)p * 4096);
        if (phys) pmm_free_frame(phys);
    }
    uint64_t f = spin_lock_irqsave(&lock);
    used[slot] = 0;
    spin_unlock_irqrestore(&lock, f);
}

int kstack_guard_hit(uint64_t addr) {
    if (addr < KSTACK_BASE || addr >= KSTACK_BASE + (uint64_t)KSTACK_SLOTS * SLOT_BYTES) return -1;
    uint64_t off = (addr - KSTACK_BASE) % SLOT_BYTES;
    return off < 4096 ? (int)((addr - KSTACK_BASE) / SLOT_BYTES) : -1;
}
