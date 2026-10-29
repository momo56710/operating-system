#include "libc.h"

/* A small first-fit allocator on top of sbrk. Freed blocks go on a list and are reused,
 * but neighbouring free blocks are not merged (fine for the test programs, not for production). */
struct hdr { size_t size; struct hdr *next; };   /* size excludes the header; 16-byte header keeps alignment */

static struct hdr *free_list;

void *malloc(size_t n) {
    if (!n) return 0;
    n = (n + 15) & ~15UL;
    for (struct hdr **pp = &free_list; *pp; pp = &(*pp)->next) {
        struct hdr *b = *pp;
        if (b->size < n) continue;
        if (b->size >= n + sizeof(struct hdr) + 16) {          /* split */
            struct hdr *rest = (struct hdr *)((char *)(b + 1) + n);
            rest->size = b->size - n - sizeof(struct hdr);
            rest->next = b->next;
            b->size = n;
            *pp = rest;
        } else {
            *pp = b->next;
        }
        return b + 1;
    }
    struct hdr *b = sbrk((long)(n + sizeof(struct hdr)));
    if (b == (void *)-1) return 0;
    b->size = n;
    return b + 1;
}

void free(void *p) {
    if (!p) return;
    struct hdr *b = (struct hdr *)p - 1;
    b->next = free_list;
    free_list = b;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    struct hdr *b = (struct hdr *)p - 1;
    if (b->size >= n) return p;                      /* the block is already big enough */
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, b->size);
    free(p);
    return q;
}
