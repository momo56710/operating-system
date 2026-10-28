#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "kprintf.h"

struct elf_header {
    uint8_t  ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct elf_phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define ET_EXEC 2
#define EM_X86_64 62
#define PAGE 4096ULL

/* Make sure the page at `va` exists in `pml4`; if two segments share a page, widen its permissions. */
static uint8_t *ensure_page(uint64_t pml4, uint64_t va, uint64_t flags) {
    uint64_t have_flags = 0;
    uint64_t phys = vmm_lookup(pml4, va, &have_flags);
    if (phys) {
        /* writable if either segment is, executable if either is */
        uint64_t merged = VMM_USER | ((have_flags | flags) & VMM_WRITE);
        if ((have_flags & VMM_NX) && (flags & VMM_NX)) merged |= VMM_NX;
        if (merged != (have_flags & (VMM_USER | VMM_WRITE | VMM_NX))) {
            vmm_unmap_in(pml4, va);
            if (vmm_map_in(pml4, va, phys, merged) < 0) return 0;
        }
        return phys_to_virt(phys);
    }
    phys = pmm_alloc_frame();
    if (!phys) return 0;
    memset(phys_to_virt(phys), 0, PAGE);
    if (vmm_map_in(pml4, va, phys, flags) < 0) { pmm_free_frame(phys); return 0; }
    return phys_to_virt(phys);
}

int elf_load(uint64_t pml4, const void *image, size_t size, uint64_t *entry, uint64_t *image_end) {
    const uint8_t *img = image;
    if (size < sizeof(struct elf_header)) return -1;
    const struct elf_header *h = image;
    if (memcmp(h->ident, "\x7f" "ELF", 4) || h->ident[4] != 2 || h->ident[5] != 1) return -1;   /* 64-bit, little endian */
    if (h->type != ET_EXEC || h->machine != EM_X86_64) return -1;
    if (h->phentsize != sizeof(struct elf_phdr) || !h->phnum) return -1;
    if (h->phoff > size || (uint64_t)h->phnum * sizeof(struct elf_phdr) > size - h->phoff) return -1;
    if (h->entry < 0x1000 || h->entry >= USER_MAX) return -1;

    uint64_t end = 0;
    int loaded = 0;
    for (int i = 0; i < h->phnum; i++) {
        const struct elf_phdr *p = (const struct elf_phdr *)(img + h->phoff) + i;
        if (p->type != PT_LOAD || !p->memsz) continue;
        if (p->filesz > p->memsz) return -1;
        if (p->offset > size || p->filesz > size - p->offset) return -1;             /* data must be inside the file */
        if (p->vaddr < 0x1000 || p->vaddr >= USER_MAX) return -1;                    /* keep page 0 unmapped */
        if (p->memsz > USER_MAX - p->vaddr) return -1;                               /* no wrap-around, stays in user half */

        uint64_t flags = VMM_USER;
        if (p->flags & PF_W) flags |= VMM_WRITE;
        if (!(p->flags & PF_X)) flags |= VMM_NX;

        uint64_t first = p->vaddr & ~(PAGE - 1), last = (p->vaddr + p->memsz + PAGE - 1) & ~(PAGE - 1);
        for (uint64_t va = first; va < last; va += PAGE)
            if (!ensure_page(pml4, va, flags)) return -1;

        for (uint64_t done = 0; done < p->filesz; ) {                               /* copy file bytes page by page */
            uint64_t va = p->vaddr + done;
            uint64_t off = va & (PAGE - 1), n = PAGE - off;
            if (n > p->filesz - done) n = p->filesz - done;
            uint64_t phys = vmm_lookup(pml4, va & ~(PAGE - 1), 0);
            memcpy((uint8_t *)phys_to_virt(phys) + off, img + p->offset + done, n);
            done += n;
        }
        if (last > end) end = last;
        loaded++;
    }
    if (!loaded) return -1;
    *entry = h->entry;
    *image_end = end;
    return 0;
}
