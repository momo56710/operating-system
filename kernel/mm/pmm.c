#include "pmm.h"
#include "panic.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

/* One bit per 4 KiB frame: 1 = used or unusable, 0 = free. */
static uint64_t *bitmap;
static uint64_t nframes, total_frames, free_frames, hint_word;
static uint64_t hhdm_base;
static spinlock_t pmm_lock;

static inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

void pmm_init(struct limine_memmap_response *mm, uint64_t hhdm) {
    hhdm_base = hhdm;

    uint64_t top = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length > top)
            top = e->base + e->length;
    }

    nframes = top / PAGE_SIZE;
    uint64_t words = (nframes + 63) / 64;
    uint64_t bytes = align_up(words * 8, PAGE_SIZE);

    /* Put the bitmap inside the first usable region above 1 MiB that is big enough. */
    uint64_t bm_phys = 0;
    for (uint64_t i = 0; i < mm->entry_count && !bm_phys; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base >= 0x100000 && e->length >= bytes)
            bm_phys = e->base;
    }
    if (!bm_phys) panic("pmm: no region large enough for the frame bitmap (%lu bytes)", bytes);

    bitmap = (uint64_t *)(bm_phys + hhdm_base);
    memset(bitmap, 0xFF, words * 8);                     /* everything used until proven free */

    /* Free the usable frames above 1 MiB; low memory stays reserved for legacy hardware. */
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        for (uint64_t p = e->base; p < e->base + e->length; p += PAGE_SIZE) {
            if (p < 0x100000) continue;
            bitmap[(p / PAGE_SIZE) / 64] &= ~(1ULL << ((p / PAGE_SIZE) % 64));
            total_frames++;
        }
    }
    free_frames = total_frames;

    for (uint64_t p = bm_phys; p < bm_phys + bytes; p += PAGE_SIZE) {   /* the bitmap itself */
        bitmap[(p / PAGE_SIZE) / 64] |= 1ULL << ((p / PAGE_SIZE) % 64);
        free_frames--;
    }
    hint_word = 0;
}

static uint64_t alloc_locked(void) {
    uint64_t words = (nframes + 63) / 64;
    for (uint64_t n = 0; n < words; n++) {
        uint64_t w = (hint_word + n) % words;
        if (bitmap[w] == ~0ULL) continue;
        int bit = __builtin_ctzll(~bitmap[w]);
        uint64_t frame = w * 64 + bit;
        if (frame >= nframes) continue;
        bitmap[w] |= 1ULL << bit;
        hint_word = w;
        free_frames--;
        return frame * PAGE_SIZE;
    }
    return 0;
}

static void free_locked(uint64_t phys) {
    uint64_t frame = phys / PAGE_SIZE;
    if ((phys & (PAGE_SIZE - 1)) || frame >= nframes)
        panic("pmm_free_frame: bad address %lx", phys);
    if (!(bitmap[frame / 64] & (1ULL << (frame % 64))))
        panic("pmm_free_frame: double free of %lx", phys);
    bitmap[frame / 64] &= ~(1ULL << (frame % 64));
    free_frames++;
    if (frame / 64 < hint_word) hint_word = frame / 64;
}

uint64_t pmm_alloc_frame(void) {
    uint64_t f = spin_lock_irqsave(&pmm_lock);
    uint64_t r = alloc_locked();
    spin_unlock_irqrestore(&pmm_lock, f);
    return r;
}

void pmm_free_frame(uint64_t phys) {
    uint64_t f = spin_lock_irqsave(&pmm_lock);
    free_locked(phys);
    spin_unlock_irqrestore(&pmm_lock, f);
}

uint64_t pmm_total_frames(void) { return total_frames; }
uint64_t pmm_free_frames(void)  { return free_frames; }

void pmm_print_stats(void) {
    kprintf("PMM: total %lu MiB, used %lu MiB, free %lu MiB (%lu frames free)\n",
            total_frames * PAGE_SIZE >> 20, (total_frames - free_frames) * PAGE_SIZE >> 20,
            free_frames * PAGE_SIZE >> 20, free_frames);
}
