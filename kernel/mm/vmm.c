#include "vmm.h"
#include "pmm.h"
#include "io.h"
#include "kprintf.h"
#include "panic.h"
#include "string.h"
#include "spinlock.h"

#define PTE_P    (1ULL << 0)
#define PTE_PS   (1ULL << 7)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define SIZE_4K  0x1000ULL
#define SIZE_2M  0x200000ULL

extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[],
            __data_start[], __data_end[];

uint64_t hhdm_offset;
static spinlock_t vmm_lock;
static uint64_t kernel_pml4;             /* physical address of the top-level table */

struct lazy_region { uint64_t base, len, flags; };
static struct lazy_region lazy[8];
static int nlazy;

static inline uint64_t *table(uint64_t phys) { return (uint64_t *)(phys + hhdm_offset); }
static inline void invlpg(uint64_t v) { __asm__ volatile ("invlpg (%0)" : : "r"(v) : "memory"); }

static uint64_t new_table(void) {
    uint64_t p = pmm_alloc_frame();
    if (!p) panic("vmm: out of physical memory for page tables");
    memset(table(p), 0, PAGE_SIZE);
    return p;
}

/* Descend one level. Returns the next table's physical address, or 0 if absent
 * (and not creating) or if the entry is a large page in the way. */
static uint64_t next_level(uint64_t *tbl, int idx, int create, uint64_t flags) {
    uint64_t e = tbl[idx];
    if (e & PTE_P) {
        if (e & PTE_PS) return 0;
        if ((flags & VMM_USER) && !(e & VMM_USER)) tbl[idx] = e | VMM_USER;
        return e & PTE_ADDR;
    }
    if (!create) return 0;
    uint64_t p = new_table();
    tbl[idx] = p | PTE_P | VMM_WRITE | (flags & VMM_USER);
    return p;
}

#define IDX4(v) (((v) >> 39) & 511)
#define IDX3(v) (((v) >> 30) & 511)
#define IDX2(v) (((v) >> 21) & 511)
#define IDX1(v) (((v) >> 12) & 511)

static int map_in(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags, int large) {
    uint64_t pdpt = next_level(table(pml4), IDX4(virt), 1, flags);
    if (!pdpt) return -1;
    uint64_t pd = next_level(table(pdpt), IDX3(virt), 1, flags);
    if (!pd) return -1;

    if (large) {
        uint64_t *e = &table(pd)[IDX2(virt)];
        uint64_t want = phys | flags | PTE_P | PTE_PS;
        if (*e & PTE_P) return *e == want ? 0 : -1;
        *e = want;
        return 0;
    }
    uint64_t pt = next_level(table(pd), IDX2(virt), 1, flags);
    if (!pt) return -1;
    uint64_t *e = &table(pt)[IDX1(virt)];
    uint64_t want = phys | flags | PTE_P;
    if (*e & PTE_P) return (*e & PTE_ADDR) == phys ? 0 : -1;   /* same target twice is fine */
    *e = want;
    return 0;
}

static uint64_t *leaf(uint64_t virt, int *is_large) {
    uint64_t pdpt = next_level(table(kernel_pml4), IDX4(virt), 0, 0);
    if (!pdpt) return 0;
    uint64_t pd = next_level(table(pdpt), IDX3(virt), 0, 0);
    if (!pd) return 0;
    uint64_t *e = &table(pd)[IDX2(virt)];
    if (!(*e & PTE_P)) return 0;
    if (*e & PTE_PS) { *is_large = 1; return e; }
    uint64_t pt = *e & PTE_ADDR;
    e = &table(pt)[IDX1(virt)];
    *is_large = 0;
    return (*e & PTE_P) ? e : 0;
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t f = spin_lock_irqsave(&vmm_lock);
    int r = map_in(kernel_pml4, virt, phys, flags, 0);
    if (r == 0) invlpg(virt);
    spin_unlock_irqrestore(&vmm_lock, f);
    return r;
}

uint64_t vmm_unmap(uint64_t virt) {
    uint64_t f = spin_lock_irqsave(&vmm_lock);
    int large;
    uint64_t *e = leaf(virt, &large);
    uint64_t phys = 0;
    if (e && !large) {
        phys = *e & PTE_ADDR;
        *e = 0;
        invlpg(virt);
    }
    spin_unlock_irqrestore(&vmm_lock, f);
    return phys;
}

uint64_t vmm_translate(uint64_t virt) {
    uint64_t f = spin_lock_irqsave(&vmm_lock);
    int large;
    uint64_t *e = leaf(virt, &large);
    uint64_t r = 0;
    if (e) r = large ? (*e & PTE_ADDR & ~(SIZE_2M - 1)) | (virt & (SIZE_2M - 1))
                     : (*e & PTE_ADDR) | (virt & (SIZE_4K - 1));
    spin_unlock_irqrestore(&vmm_lock, f);
    return r;
}

/* Map a physical range into the higher-half direct map, using 2 MiB pages where possible. */
static void map_hhdm_range(uint64_t base, uint64_t end, uint64_t flags) {
    base &= ~(SIZE_4K - 1);
    end = (end + SIZE_4K - 1) & ~(SIZE_4K - 1);
    for (uint64_t p = base; p < end; ) {
        int large = !(p & (SIZE_2M - 1)) && end - p >= SIZE_2M;
        if (map_in(kernel_pml4, p + hhdm_offset, p, flags, large) < 0)
            panic("vmm: HHDM mapping conflict at %lx", p);
        p += large ? SIZE_2M : SIZE_4K;
    }
}

static void map_kernel_range(char *start, char *end, uint64_t flags,
                             struct limine_executable_address_response *exec) {
    for (uint64_t v = (uint64_t)start; v < (uint64_t)end; v += SIZE_4K) {
        uint64_t phys = exec->physical_base + (v - exec->virtual_base);
        if (map_in(kernel_pml4, v, phys, flags, 0) < 0)
            panic("vmm: kernel mapping conflict at %lx", v);
    }
}

void vmm_init(struct limine_memmap_response *mm,
              struct limine_executable_address_response *exec, uint64_t hhdm) {
    hhdm_offset = hhdm;

    wrmsr(0xC0000080, rdmsr(0xC0000080) | (1ULL << 11));        /* EFER.NXE: honour the NX bit */
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1ULL << 16;                                           /* CR0.WP: ring 0 respects read-only */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    wrmsr(0x277, 0x0007010600070106ULL);                         /* PAT index 1 (PWT) = write-combining */

    kernel_pml4 = new_table();

    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_BAD_MEMORY) continue;
        uint64_t flags = VMM_WRITE | VMM_NX;
        if (e->type == LIMINE_MEMMAP_FRAMEBUFFER) flags |= VMM_PWT;
        map_hhdm_range(e->base, e->base + e->length, flags);
    }

    map_kernel_range(__text_start,   __text_end,   0,                         exec);   /* r-x */
    map_kernel_range(__rodata_start, __rodata_end, VMM_NX,                    exec);   /* r-- */
    map_kernel_range(__data_start,   __data_end,   VMM_WRITE | VMM_NX,        exec);   /* rw- */

    __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_pml4) : "memory");
}

void vmm_lazy_region(uint64_t base, uint64_t len, uint64_t flags) {
    if (nlazy == 8) panic("vmm: too many lazy regions");
    lazy[nlazy++] = (struct lazy_region){ base, len, flags };
}

void vmm_lazy_set_len(uint64_t base, uint64_t len) {
    for (int i = 0; i < nlazy; i++)
        if (lazy[i].base == base) { lazy[i].len = len; return; }
}

int vmm_handle_fault(uint64_t addr, uint64_t error) {
    if (error & 1) return 0;                    /* protection violation: never a lazy page */
    for (int i = 0; i < nlazy; i++) {
        if (addr < lazy[i].base || addr >= lazy[i].base + lazy[i].len) continue;
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return 0;
        memset(table(phys), 0, PAGE_SIZE);
        if (vmm_map(addr & ~(SIZE_4K - 1), phys, lazy[i].flags) < 0) {
            pmm_free_frame(phys);
            return 0;
        }
        return 1;
    }
    return 0;
}
