#include "libc.h"

/* sort [n]: quicksort n pseudo-random numbers and check the result. */
static unsigned long rng = 0x2545F4914F6CDD1DUL;
static unsigned long next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static void quicksort(long *a, long lo, long hi) {
    while (lo < hi) {
        long pivot = a[(lo + hi) / 2], i = lo, j = hi;
        while (i <= j) {
            while (a[i] < pivot) i++;
            while (a[j] > pivot) j--;
            if (i <= j) { long t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j - lo < hi - i) { quicksort(a, lo, j); lo = i; }     /* recurse into the smaller half */
        else                 { quicksort(a, i, hi); hi = j; }
    }
}

int main(int argc, char **argv) {
    long n = argc > 1 ? atoi(argv[1]) : 50000;
    long *a = malloc(n * sizeof *a);
    if (!a) { printf("out of memory\n"); return 1; }
    for (long i = 0; i < n; i++) a[i] = (long)(next() % 1000000);
    unsigned long t0 = uptime_ms();
    quicksort(a, 0, n - 1);
    unsigned long ms = uptime_ms() - t0;
    int ok = 1;
    for (long i = 1; i < n; i++) if (a[i - 1] > a[i]) ok = 0;
    printf("sorted %ld numbers in %lu ms: %s\n", n, ms, ok ? "ORDER OK" : "NOT SORTED");
    printf("smallest %ld, median %ld, largest %ld\n", a[0], a[n / 2], a[n - 1]);
    free(a);
    return ok ? 0 : 1;
}
