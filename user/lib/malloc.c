#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sys.h"

/* A first-fit allocator over sbrk. Every block has a 16-byte header with its size and the size of the
 * block before it, so a freed block merges with free neighbours on both sides. A free block keeps
 * list links in its payload. realloc grows in place when the next block is free. */

struct blk { size_t size, prev_size; };           /* size includes the header; bit 0 = in use */
struct links { struct blk *next, *prev; };

#define HDR 16
#define MIN_BLOCK 32
#define GROW_MIN (256 * 1024)

static char *heap_start, *heap_end;
static struct blk *free_head, *last;

static size_t bsize(struct blk *b) { return b->size & ~(size_t)1; }
static int bused(struct blk *b) { return (int)(b->size & 1); }
static struct links *lk(struct blk *b) { return (struct links *)(b + 1); }

static void list_insert(struct blk *b) {
    lk(b)->prev = 0; lk(b)->next = free_head;
    if (free_head) lk(free_head)->prev = b;
    free_head = b;
}
static void list_remove(struct blk *b) {
    if (lk(b)->prev) lk(lk(b)->prev)->next = lk(b)->next; else free_head = lk(b)->next;
    if (lk(b)->next) lk(lk(b)->next)->prev = lk(b)->prev;
}

/* b is a block marked free, not on the list: merge it with free neighbours and list it. */
static void release(struct blk *b) {
    char *end = (char *)b + bsize(b);
    if (end < heap_end) {
        struct blk *n = (struct blk *)end;
        if (!bused(n)) { list_remove(n); b->size += bsize(n); if (n == last) last = b; }
    }
    if (b->prev_size) {
        struct blk *p = (struct blk *)((char *)b - b->prev_size);
        if (!bused(p)) { list_remove(p); p->size += bsize(b); if (b == last) last = p; b = p; }
    }
    end = (char *)b + bsize(b);
    if (end < heap_end) ((struct blk *)end)->prev_size = bsize(b); else last = b;
    list_insert(b);
}

static int grow(size_t need) {
    size_t amount = need < GROW_MIN ? GROW_MIN : (need + 4095) & ~(size_t)4095;
    char *p = sbrk((long)amount);
    if (p == (void *)-1) return 0;
    if (!heap_start) { heap_start = p; heap_end = p; }
    if (p != heap_end) return 0;                    /* someone else moved the break: cannot extend contiguously */
    struct blk *b = (struct blk *)p;
    b->size = amount;
    b->prev_size = last ? bsize(last) : 0;
    heap_end += amount;
    release(b);
    return 1;
}

void *malloc(size_t n) {
    if (!n) n = 1;
    if (n > ((size_t)1 << 40)) return 0;
    size_t need = ((n + 15) & ~(size_t)15) + HDR;
    if (need < MIN_BLOCK) need = MIN_BLOCK;
    for (;;) {
        for (struct blk *b = free_head; b; b = lk(b)->next) {
            if (bsize(b) < need) continue;
            list_remove(b);
            size_t total = bsize(b);
            if (total - need >= MIN_BLOCK) {
                struct blk *rest = (struct blk *)((char *)b + need);
                rest->size = total - need;
                rest->prev_size = need;
                char *rend = (char *)rest + bsize(rest);
                if (rend < heap_end) ((struct blk *)rend)->prev_size = bsize(rest); else last = rest;
                list_insert(rest);
                b->size = need | 1;
            } else {
                b->size = total | 1;
            }
            return b + 1;
        }
        if (!grow(need)) return 0;
    }
}

void free(void *p) {
    if (!p) return;
    struct blk *b = (struct blk *)p - 1;
    b->size &= ~(size_t)1;
    release(b);
}

void *calloc(size_t n, size_t size) {
    if (size && n > ((size_t)1 << 40) / size) return 0;
    void *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    struct blk *b = (struct blk *)p - 1;
    size_t need = ((n + 15) & ~(size_t)15) + HDR;
    if (need < MIN_BLOCK) need = MIN_BLOCK;
    size_t have = bsize(b);
    if (have >= need) return p;
    char *end = (char *)b + have;                                   /* try to absorb the next block */
    if (end < heap_end) {
        struct blk *nx = (struct blk *)end;
        if (!bused(nx) && have + bsize(nx) >= need) {
            list_remove(nx);
            size_t total = have + bsize(nx);
            if (nx == last) last = b;
            b->size = total | 1;
            char *tend = (char *)b + total;
            if (total - need >= MIN_BLOCK) {                        /* give back what is not needed */
                struct blk *rest = (struct blk *)((char *)b + need);
                rest->size = total - need;
                rest->prev_size = need;
                b->size = need | 1;
                if (b == last) last = rest;
                if (tend < heap_end) ((struct blk *)tend)->prev_size = bsize(rest);
                list_insert(rest);
            } else if (tend < heap_end) {
                ((struct blk *)tend)->prev_size = total;
            }
            return p;
        }
    }
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, have - HDR);
    free(p);
    return q;
}
