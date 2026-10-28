#include "memtest.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "kprintf.h"

static int failures;

#define CHECK(cond, msg) do { if (!(cond)) { kprintf("  FAIL: %s\n", msg); failures++; } } while (0)

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

int mm_selftest_quick(void) {
    failures = 0;

    /* PMM: frames are distinct, aligned, and come back when freed. */
    uint64_t before = pmm_free_frames();
    static uint64_t frames[256];
    for (int i = 0; i < 256; i++) {
        frames[i] = pmm_alloc_frame();
        CHECK(frames[i] && !(frames[i] & 0xFFF), "pmm_alloc_frame returned a bad frame");
        for (int j = 0; j < i; j++) CHECK(frames[i] != frames[j], "pmm returned a duplicate frame");
    }
    CHECK(pmm_free_frames() == before - 256, "free count after 256 allocs");
    for (int i = 0; i < 256; i++) pmm_free_frame(frames[i]);
    CHECK(pmm_free_frames() == before, "free count after freeing them again");
    kprintf("  PMM alloc/free: %s\n", failures ? "FAIL" : "ok");

    /* VMM: map, write through the mapping, translate, unmap. */
    int f0 = failures;
    uint64_t virt = 0xFFFFD00000000000ULL;
    uint64_t phys = pmm_alloc_frame();
    CHECK(vmm_translate(virt) == 0, "address should start unmapped");
    CHECK(vmm_map(virt, phys, VMM_WRITE | VMM_NX) == 0, "vmm_map failed");
    CHECK(vmm_translate(virt + 0x123) == phys + 0x123, "vmm_translate mismatch");
    *(volatile uint64_t *)virt = 0xDEADBEEFCAFEF00DULL;
    CHECK(*(volatile uint64_t *)phys_to_virt(phys) == 0xDEADBEEFCAFEF00DULL,
          "write through the mapping not visible through the direct map");
    CHECK(vmm_unmap(virt) == phys, "vmm_unmap returned the wrong frame");
    CHECK(vmm_translate(virt) == 0, "address still mapped after unmap");
    pmm_free_frame(phys);
    kprintf("  VMM map/translate/unmap: %s\n", failures > f0 ? "FAIL" : "ok");

    /* Heap: small blocks work and the heap stays consistent. */
    f0 = failures;
    char *a = kmalloc(100), *b = kmalloc(5000), *c = kmalloc(1);
    CHECK(a && b && c && a != b && b != c, "kmalloc returned null or duplicates");
    for (int i = 0; i < 100; i++) a[i] = (char)i;
    for (int i = 0; i < 5000; i++) b[i] = (char)(i * 7);
    for (int i = 0; i < 100; i++) CHECK(a[i] == (char)i, "block a corrupted");
    kfree(b); kfree(a); kfree(c);
    CHECK(heap_check() == 0, "heap inconsistent after small test");
    kprintf("  kmalloc/kfree: %s\n", failures > f0 ? "FAIL" : "ok");
    return failures;
}

static void fill(uint8_t *p, uint64_t n, uint8_t seed) { for (uint64_t i = 0; i < n; i++) p[i] = seed + (uint8_t)i; }
static int verify(uint8_t *p, uint64_t n, uint8_t seed) {
    for (uint64_t i = 0; i < n; i++) if (p[i] != (uint8_t)(seed + (uint8_t)i)) return 0;
    return 1;
}

/* Allocate 100 MiB, touch every page, free it; repeat; the frame count must not drift. */
static void big_cycle(const char *label, uint64_t *free_after) {
    uint64_t size = 100ULL << 20;
    uint8_t *p = kmalloc(size);
    CHECK(p != 0, "100 MiB kmalloc failed");
    if (!p) return;
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) *(volatile uint64_t *)(p + off) = off ^ 0xA5A5;
    int ok = 1;
    for (uint64_t off = 0; off < size; off += PAGE_SIZE)
        if (*(volatile uint64_t *)(p + off) != (off ^ 0xA5A5)) { ok = 0; break; }
    CHECK(ok, "100 MiB block contents corrupted");
    kprintf("  %s: touched %lu pages, PMM free %lu MiB\n", label, size / PAGE_SIZE,
            pmm_free_frames() * PAGE_SIZE >> 20);
    kfree(p);
    *free_after = pmm_free_frames();
    CHECK(heap_check() == 0, "heap inconsistent after big free");
}

int mm_selftest_full(void) {
    failures = 0;
    uint64_t before = pmm_free_frames();

    /* Random-size stress with content checks. */
    enum { SLOTS = 1024, ITERS = 40000 };
    static void *ptr[SLOTS];
    static uint64_t sz[SLOTS];
    static uint8_t seed[SLOTS];
    int bad = 0;
    for (int it = 0; it < ITERS; it++) {
        int s = rng() % SLOTS;
        if (ptr[s]) {
            if (!verify(ptr[s], sz[s], seed[s])) bad++;
            kfree(ptr[s]);
            ptr[s] = 0;
        } else {
            uint64_t r = rng() % 100;
            sz[s] = r < 70 ? 1 + rng() % 256 : r < 95 ? 257 + rng() % 8000 : 8193 + rng() % 190000;
            seed[s] = rng();
            ptr[s] = kmalloc(sz[s]);
            if (!ptr[s]) { bad++; continue; }
            fill(ptr[s], sz[s], seed[s]);
        }
        if (it % 8000 == 0 && heap_check() != 0) bad++;
    }
    for (int s = 0; s < SLOTS; s++)
        if (ptr[s]) { if (!verify(ptr[s], sz[s], seed[s])) bad++; kfree(ptr[s]); ptr[s] = 0; }
    CHECK(bad == 0, "stress test saw corruption or allocation failures");
    CHECK(heap_check() == 0, "heap inconsistent after stress test");
    uint64_t span, used, fr;
    heap_stats(&span, &used, &fr);
    CHECK(used == 0, "bytes still in use after freeing everything");
    kprintf("  stress: %d random allocs/frees, heap span %lu KiB, used %lu\n", ITERS, span >> 10, used);

    uint64_t a1, a2;
    big_cycle("100 MiB #1", &a1);
    big_cycle("100 MiB #2", &a2);
    CHECK(a1 == a2, "frame count drifted between the two 100 MiB cycles (leak)");
    kprintf("  PMM free frames: start %lu, after #1 %lu, after #2 %lu (delta vs start %ld, page tables only)\n",
            before, a1, a2, (long)(before - a2));
    return failures;
}
