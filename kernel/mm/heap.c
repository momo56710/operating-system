#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "kprintf.h"
#include "panic.h"
#include "spinlock.h"

/* Free-list heap in its own virtual window. Pages are mapped on first touch by the
 * page fault handler, and handed back to the PMM when the blocks on them are freed.
 *
 * Every block starts with a 16-byte header. `size` is the whole block (header included)
 * with bit 0 = in use; `prev_size` lets us step back to merge with a free neighbour.
 * A free block keeps its list links in the first 16 bytes of its payload, so the header
 * and links page of a free block always stays mapped; only the pages wholly inside it go. */

#define HEAP_BASE  0xFFFFC00000000000ULL
#define HEAP_MAX   (1ULL << 30)           /* 1 GiB of address space */
#define HDR        16ULL
#define MIN_BLOCK  32ULL
#define MIN_GROW   (256 * 1024ULL)

struct blk   { uint64_t size, prev_size; };
struct links { struct blk *next, *prev; };

static uint64_t heap_end = HEAP_BASE;
static struct blk *free_head;
static struct blk *last;                  /* highest-address block */
static spinlock_t heap_lock;

static inline uint64_t align_up(uint64_t v, uint64_t a)   { return (v + a - 1) & ~(a - 1); }
static inline uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1); }
static inline uint64_t bsize(struct blk *b) { return b->size & ~1ULL; }
static inline int      bused(struct blk *b) { return b->size & 1; }
static inline struct links *lk(struct blk *b) { return (struct links *)(b + 1); }

static void list_insert(struct blk *b) {
    lk(b)->prev = 0;
    lk(b)->next = free_head;
    if (free_head) lk(free_head)->prev = b;
    free_head = b;
}

static void list_remove(struct blk *b) {
    if (lk(b)->prev) lk(lk(b)->prev)->next = lk(b)->next; else free_head = lk(b)->next;
    if (lk(b)->next) lk(lk(b)->next)->prev = lk(b)->prev;
}

void heap_init(void) {
    vmm_lazy_region(HEAP_BASE, 0, VMM_WRITE | VMM_NX);
}

/* Give whole pages in [lo, hi) back to the PMM. They fault back in, zeroed, if reused. */
static void release_pages(uint64_t lo, uint64_t hi) {
    for (uint64_t p = lo; p < hi; p += PAGE_SIZE) {
        uint64_t phys = vmm_unmap(p);
        if (phys) pmm_free_frame(phys);
    }
}

/* `b` is a block already marked free, with size and prev_size set, and not in the list.
 * Merge it with free neighbours, put it on the list and return dead pages to the PMM.
 * `fresh` blocks (just carved from new address space) have nothing mapped to release. */
static void free_block(struct blk *b, int fresh) {
    uint64_t b_start = (uint64_t)b, b_end = b_start + bsize(b);
    int next_merged = 0;

    if (b_end < heap_end) {
        struct blk *n = (struct blk *)b_end;
        if (!bused(n)) { list_remove(n); b->size += bsize(n); next_merged = 1; }
    }
    if (b->prev_size) {
        struct blk *p = (struct blk *)(b_start - b->prev_size);
        if (!bused(p)) { list_remove(p); p->size += bsize(b); b = p; }
    }

    uint64_t m_start = (uint64_t)b, m_end = m_start + bsize(b);
    if (m_end < heap_end) ((struct blk *)m_end)->prev_size = bsize(b);
    else last = b;
    list_insert(b);

    if (fresh) return;
    /* Pages that became dead: this block's own span plus the header of a merged successor,
     * clipped to what lies wholly inside the merged block past its header and links. */
    uint64_t lo = align_up(m_start + HDR + 16, PAGE_SIZE);
    uint64_t lo2 = align_down(b_start, PAGE_SIZE);
    if (lo2 > lo) lo = lo2;
    uint64_t hi = align_down(m_end, PAGE_SIZE);
    uint64_t hi2 = align_up(b_end + (next_merged ? HDR + 16 : 0), PAGE_SIZE);
    if (hi2 < hi) hi = hi2;
    if (lo < hi) release_pages(lo, hi);
}

static int heap_expand(uint64_t need) {
    uint64_t grow = align_up(need < MIN_GROW ? MIN_GROW : need, PAGE_SIZE);
    if (heap_end - HEAP_BASE + grow > HEAP_MAX) return 0;

    struct blk *b = (struct blk *)heap_end;
    uint64_t prev = last ? bsize(last) : 0;
    heap_end += grow;
    vmm_lazy_set_len(HEAP_BASE, heap_end - HEAP_BASE);

    b->size = grow;                       /* first touch: the page fault handler maps it */
    b->prev_size = prev;
    free_block(b, 1);
    return 1;
}

static void *kmalloc_locked(size_t n) {
    if (!n || n > HEAP_MAX) return 0;
    uint64_t need = align_up(n, 16) + HDR;
    if (need < MIN_BLOCK) need = MIN_BLOCK;

    for (;;) {
        for (struct blk *b = free_head; b; b = lk(b)->next) {
            if (bsize(b) < need) continue;
            list_remove(b);
            uint64_t total = bsize(b);
            if (total - need >= MIN_BLOCK) {              /* split off the tail */
                struct blk *rem = (struct blk *)((uint64_t)b + need);
                rem->size = total - need;
                rem->prev_size = need;
                uint64_t rem_end = (uint64_t)rem + bsize(rem);
                if (rem_end < heap_end) ((struct blk *)rem_end)->prev_size = bsize(rem);
                else last = rem;
                list_insert(rem);
                b->size = need | 1;
            } else {
                b->size = total | 1;
            }
            return b + 1;
        }
        if (!heap_expand(need)) return 0;
    }
}

static void kfree_locked(void *p) {
    if (!p) return;
    uint64_t a = (uint64_t)p;
    if (a < HEAP_BASE + HDR || a >= heap_end || (a & 15))
        panic("kfree: %p is not a heap pointer", p);
    struct blk *b = (struct blk *)p - 1;
    if (!bused(b)) panic("kfree: double free of %p", p);
    b->size &= ~1ULL;
    free_block(b, 0);
}

void *kmalloc(size_t n) {
    uint64_t f = spin_lock_irqsave(&heap_lock);
    void *r = kmalloc_locked(n);
    spin_unlock_irqrestore(&heap_lock, f);
    return r;
}

void kfree(void *p) {
    uint64_t f = spin_lock_irqsave(&heap_lock);
    kfree_locked(p);
    spin_unlock_irqrestore(&heap_lock, f);
}

static void heap_stats_locked(uint64_t *mapped_span, uint64_t *used_bytes, uint64_t *free_bytes) {
    uint64_t used = 0, fr = 0;
    for (uint64_t a = HEAP_BASE; a < heap_end; a += bsize((struct blk *)a)) {
        struct blk *b = (struct blk *)a;
        if (bused(b)) used += bsize(b); else fr += bsize(b);
    }
    if (mapped_span) *mapped_span = heap_end - HEAP_BASE;
    if (used_bytes) *used_bytes = used;
    if (free_bytes) *free_bytes = fr;
}

static int heap_check_locked(void) {
    uint64_t expect_prev = 0, nfree = 0, prev_free = 0;
    for (uint64_t a = HEAP_BASE; a < heap_end; ) {
        struct blk *b = (struct blk *)a;
        uint64_t s = bsize(b);
        if (s < MIN_BLOCK || (s & 15) || a + s > heap_end) { kprintf("heap_check: bad size %lx at %lx\n", s, a); return -1; }
        if (b->prev_size != expect_prev) { kprintf("heap_check: bad prev_size at %lx\n", a); return -1; }
        if (!bused(b)) {
            if (prev_free) { kprintf("heap_check: adjacent free blocks at %lx\n", a); return -1; }
            nfree++;
        }
        prev_free = !bused(b);
        expect_prev = s;
        a += s;
    }
    uint64_t listed = 0;
    for (struct blk *b = free_head; b && listed <= nfree; b = lk(b)->next) {
        if (bused(b)) { kprintf("heap_check: used block on free list\n"); return -1; }
        listed++;
    }
    if (listed != nfree) { kprintf("heap_check: free list has %lu, heap has %lu\n", listed, nfree); return -1; }
    return 0;
}

void heap_stats(uint64_t *mapped_span, uint64_t *used_bytes, uint64_t *free_bytes) {
    uint64_t f = spin_lock_irqsave(&heap_lock);
    heap_stats_locked(mapped_span, used_bytes, free_bytes);
    spin_unlock_irqrestore(&heap_lock, f);
}

int heap_check(void) {
    uint64_t f = spin_lock_irqsave(&heap_lock);
    int r = heap_check_locked();
    spin_unlock_irqrestore(&heap_lock, f);
    return r;
}
