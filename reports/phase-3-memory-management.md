# Phase 3: Memory management

Date: 2026-10-28
Status: **complete.** Exit gate passed. Tag to apply: `v0.3-memory`.

## What I did
Built the three layers in order, testing each before the next.

**Layer 1: physical memory manager** (`kernel/mm/pmm.c`)
- Reads the Limine memory map. A bitmap with one bit per 4 KiB frame (1 = used) is placed inside the first big usable region, and its own frames are marked used.
- Only usable memory above 1 MiB is handed out. Low memory stays reserved for legacy hardware. Bootloader-reclaimable memory is *not* freed, because we are still running on Limine's stack.
- `pmm_alloc_frame()` is next-fit using a word hint and `ctz`. `pmm_free_frame()` panics on a bad address or a double free. `pmm_print_stats()` prints total, used and free memory.

**Layer 2: virtual memory manager** (`kernel/mm/vmm.c`)
- Builds our own 4-level page tables (PML4, PDPT, PD, PT) and loads CR3. `vmm_map`, `vmm_unmap` and `vmm_translate` work on 4 KiB pages. Mapping the same page to the same frame twice is allowed; conflicting mappings fail.
- Maps the whole higher-half direct map (HHDM) from the memory map, with 2 MiB pages where aligned.
- Maps the kernel image by section: text r-x, rodata r-- (NX), data and bss rw- (NX). `linker.ld` now exports `__text_start/end`, `__rodata_start/end` and `__data_start/end`, and `main.c` requests the executable address from Limine.
- Sets EFER.NXE (honour NX), CR0.WP (ring 0 respects read-only pages) and programs the PAT so the framebuffer is write-combining.
- `invlpg` runs after every map and unmap.
- **Page fault handler** (`vmm_handle_fault`): a not-present fault inside a registered lazy region maps a zeroed frame and retries. Everything else, such as a protection violation or an address outside any region, is still reported as a real fault.

**Layer 3: kernel heap** (`kernel/mm/heap.c`)
- `kmalloc`/`kfree`: first-fit free list with 16-byte block headers, block splitting, and boundary tags so freed blocks merge with both neighbours. It detects double frees and bad pointers.
- It lives in its own 1 GiB virtual window at `0xffffc00000000000`. It grows by reserving address space, and **pages are mapped on first touch** by the page fault handler.
- When blocks are freed, the pages wholly inside the free space are unmapped and returned to the PMM, so memory goes back to the system. Header and list-link pages are always kept mapped.
- `heap_check()` walks every block and checks sizes, boundary tags, adjacent free blocks and the free list.

**Tests** (`kernel/mm/memtest.c`)
- A quick test runs on every boot: PMM, VMM and small heap checks.
- A full test runs with `make iso EXTRA=-DMM_TEST`: a 40,000-operation random alloc/free stress test with content checks, then two 100 MiB cycles that touch every page.

## Results
Boot output:

    PMM: total 509 MiB, used 0 MiB, free 509 MiB (130511 frames free)
    VMM: kernel page tables loaded (CR3 switched)
    Memory self-test: PMM alloc/free: ok / VMM map/translate/unmap: ok / kmalloc/kfree: ok

Full test:

    stress: 40000 random allocs/frees, heap span 11008 KiB, used 0
    100 MiB #1: touched 25600 pages, PMM free 409 MiB
    100 MiB #2: touched 25600 pages, PMM free 409 MiB
    PMM free frames: start 130480, after #1 130430, after #2 130430

- No corruption in the stress test, and `heap_check()` was clean throughout.
- **No leak:** the free frame count is identical after both 100 MiB cycles. The 50-frame difference from the start is the page tables for that heap region, which are kept.
- Bad accesses still fault: a null read (`PAGE FAULT at 0x0`), a read past the end of the heap window, and a write to kernel code (`protection violation, write`, proving the read-only mapping and CR0.WP work).
- Keyboard typing still works after the CR3 switch.

## Exit gate
You can allocate 100 MB through `kmalloc`, free it, and allocate again without leaks or faults: **passed.**

## Notes
- Page-table pages are never freed (even when a table becomes empty). That is a small, bounded cost for now.
- Only RAM is mapped in the HHDM. Device memory (the APIC at `0xfee00000`, PCI, and so on) must be mapped on demand with `vmm_map(..., VMM_PCD)`. That is exactly what Phase 4 does for the APIC, and why my Phase 2 probe of the APIC page-faulted.
- The roadmap lists a slab allocator for fixed-size objects as "later". I haven't built it, and the heap works without it.
- The kernel still runs on the stack Limine provided. Phase 4 allocates real kernel stacks per task.
- The kernel image is also reachable through the HHDM with write permission, as an alias. I'll tighten this in the Phase 7 hardening work.

## Not verified
- UEFI boot and VirtualBox. The write-combining framebuffer setting was only exercised on QEMU, not real hardware.
- Very low-memory behaviour: only a 512 MiB QEMU configuration was tested.
- Nothing from Phases 0-3 is on a real machine.
