#include "libc.h"

/* primes [limit]: sieve of Eratosthenes. Uses malloc for a big array. */
int main(int argc, char **argv) {
    unsigned long limit = argc > 1 ? (unsigned long)atoi(argv[1]) : 100000;
    unsigned char *composite = malloc(limit + 1);
    if (!composite) { printf("out of memory\n"); return 1; }
    memset(composite, 0, limit + 1);
    unsigned long t0 = uptime_ms(), count = 0, last = 0;
    for (unsigned long i = 2; i <= limit; i++) {
        if (composite[i]) continue;
        count++;
        last = i;
        for (unsigned long j = i * i; j <= limit; j += i) composite[j] = 1;
    }
    printf("%lu primes up to %lu (the largest is %lu), %lu ms\n", count, limit, last, uptime_ms() - t0);
    free(composite);
    return 0;
}
