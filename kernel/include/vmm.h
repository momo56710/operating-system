#pragma once
#include <stdint.h>
#include "limine.h"

/* Hardware page-table bits. Pages are executable unless VMM_NX is passed. */
#define VMM_WRITE (1ULL << 1)
#define VMM_USER  (1ULL << 2)
#define VMM_PWT   (1ULL << 3)    /* write-combining after our PAT setup */
#define VMM_PCD   (1ULL << 4)    /* uncached, for MMIO */
#define VMM_DEVICE (1ULL << 9)   /* software bit: a device page mapped into a process; the frame is not the process's to free */
#define VMM_NX    (1ULL << 63)

extern uint64_t hhdm_offset;

static inline void *phys_to_virt(uint64_t phys) { return (void *)(phys + hhdm_offset); }

void     vmm_init(struct limine_memmap_response *mm,
                  struct limine_executable_address_response *exec, uint64_t hhdm);
int      vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);      /* 0 ok, -1 error */
uint64_t vmm_unmap(uint64_t virt);        /* returns the physical frame it held, or 0 */
uint64_t vmm_translate(uint64_t virt);    /* physical address, or 0 if unmapped */

/* Regions that are valid but filled in on first touch by the page fault handler. */
void vmm_lazy_region(uint64_t base, uint64_t len, uint64_t flags);
void vmm_lazy_set_len(uint64_t base, uint64_t len);
int  vmm_handle_fault(uint64_t addr, uint64_t error);   /* 1 = fixed, retry the access */

/* Per-process address spaces. The upper half (kernel) is shared; the lower half is private. */
#define USER_MAX 0x0000800000000000ULL     /* first address user code may NOT touch */

uint64_t vmm_kernel_pml4(void);
void     vmm_switch(uint64_t pml4);                                       /* load CR3 */
uint64_t vmm_new_space(void);
void     vmm_free_space(uint64_t pml4);                                   /* frees all user pages too */
uint64_t vmm_clone_space(uint64_t src);                                   /* for fork; 0 = out of memory */
int      vmm_map_in(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_unmap_in(uint64_t pml4, uint64_t virt);
uint64_t vmm_lookup(uint64_t pml4, uint64_t virt, uint64_t *flags);       /* phys or 0 */
void *vmm_map_mmio(uint64_t phys, uint64_t bytes);   /* uncached mapping of device registers */
