#include "libc.h"

static unsigned long rng = 88172645463325252UL;
static unsigned long next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void) {
    int bad = 0;

    /* malloc/free with content checks */
    enum { SLOTS = 200 };
    static unsigned char *p[SLOTS];
    static size_t sz[SLOTS];
    for (int it = 0; it < 5000; it++) {
        int s = next() % SLOTS;
        if (p[s]) {
            for (size_t i = 0; i < sz[s]; i++) if (p[s][i] != (unsigned char)(s + i)) { bad++; break; }
            free(p[s]); p[s] = 0;
        } else {
            sz[s] = 1 + next() % 3000;
            p[s] = malloc(sz[s]);
            if (!p[s]) { bad++; continue; }
            for (size_t i = 0; i < sz[s]; i++) p[s][i] = (unsigned char)(s + i);
        }
    }
    printf("malloc/free stress: %s\n", bad ? "FAILED" : "ok");

    /* a large brk growth, touched everywhere */
    char *big = sbrk(8 << 20);
    if (big == (void *)-1) { printf("sbrk 8 MiB failed\n"); return 1; }
    for (long i = 0; i < (8 << 20); i += 4096) big[i] = (char)i;
    for (long i = 0; i < (8 << 20); i += 4096) if (big[i] != (char)i) bad++;
    printf("sbrk 8 MiB, touched every page: %s\n", bad ? "FAILED" : "ok");

    /* mmap, munmap */
    unsigned char *m = mmap(1 << 20);
    if (!m) { printf("mmap failed\n"); return 1; }
    for (int i = 0; i < (1 << 20); i += 4096) m[i] = 0xAB;
    int unmapped = munmap(m, 1 << 20);
    printf("mmap 1 MiB, write, munmap -> %d (expected 0)\n", unmapped);
    if (unmapped != 0) bad++;

    /* time */
    unsigned long t0 = uptime_ms();
    sleep_ms(200);
    unsigned long dt = uptime_ms() - t0;
    printf("sleep_ms(200) took %lu ms\n", dt);
    if (dt < 200 || dt > 230) bad++;

    printf("umem: %s\n", bad ? "FAILED" : "PASSED");
    return bad ? 1 : 0;
}
