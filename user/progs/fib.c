#include "libc.h"

static unsigned long fib_rec(int n) { return n < 2 ? (unsigned long)n : fib_rec(n - 1) + fib_rec(n - 2); }

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 27;
    unsigned long t0 = uptime_ms();
    unsigned long r = fib_rec(n);
    printf("recursive fib(%d) = %lu  (%lu ms)\n", n, r, uptime_ms() - t0);

    unsigned long a = 0, b = 1;                       /* 64-bit arithmetic check */
    for (int i = 0; i < 90; i++) { unsigned long t = a + b; a = b; b = t; }
    printf("iterative fib(90) = %lu  (expected 2880067194370816120)\n", a);
    return a == 2880067194370816120UL ? 0 : 1;
}
