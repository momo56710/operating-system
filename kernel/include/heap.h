#pragma once
#include <stddef.h>
#include <stdint.h>

void  heap_init(void);
void *kmalloc(size_t n);
void  kfree(void *p);
void  heap_stats(uint64_t *mapped_span, uint64_t *used_bytes, uint64_t *free_bytes);
int   heap_check(void);                  /* walks every block; 0 = consistent */
