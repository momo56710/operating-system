#include "libc.h"

/* bench: rough speed numbers for the CPU, system calls, fork+wait and memory. */
static unsigned long timed(unsigned long t0) { return uptime_ms() - t0; }

int main(void) {
    unsigned long t0 = uptime_ms();
    volatile unsigned long x = 0;
    (void)x;
    for (unsigned long i = 0; i < 50000000UL; i++) x += i;
    printf("50M loop iterations:     %lu ms\n", timed(t0));

    t0 = uptime_ms();
    for (int i = 0; i < 100000; i++) getpid();
    printf("100k getpid() calls:     %lu ms\n", timed(t0));

    t0 = uptime_ms();
    for (int i = 0; i < 200; i++) {
        int pid = fork();
        if (pid == 0) exit(0);
        int st;
        wait(&st);
    }
    printf("200 fork+exit+wait:      %lu ms\n", timed(t0));

    t0 = uptime_ms();
    for (int i = 0; i < 2000; i++) { void *p = malloc(4096); *(volatile char *)p = 1; free(p); }
    printf("2000 malloc/free pairs:  %lu ms\n", timed(t0));

    t0 = uptime_ms();
    char *big = sbrk(16 << 20);
    for (long i = 0; i < (16 << 20); i += 4096) big[i] = 1;
    printf("16 MiB sbrk + touch:     %lu ms\n", timed(t0));
    return 0;
}
