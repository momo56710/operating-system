#pragma once
#include <stdint.h>
#include "limine.h"

#define PAGE_SIZE 4096UL

void     pmm_init(struct limine_memmap_response *mm, uint64_t hhdm);
uint64_t pmm_alloc_frame(void);          /* physical address, or 0 when out of memory */
void     pmm_free_frame(uint64_t phys);
uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames(void);
void     pmm_print_stats(void);
