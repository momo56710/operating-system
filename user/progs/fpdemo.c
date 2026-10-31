#include "libc.h"

/* fpdemo: proves floating-point registers are private to each process. Four processes do exact
 * arithmetic on doubles and on x87 long doubles while the scheduler keeps switching between them
 * (every sleep forces a switch). One leaked register would make a sum wrong. */
#define N 60000

static int child(int id, double inherited) {
    int bad = 0;
    if (inherited != 3.14159265358979) { printf("  child %d: the register value inherited through fork was lost\n", id); bad |= 1; }

    double inc = id == 0 ? 0.5 : id == 1 ? 0.25 : id == 2 ? 0.125 : 2.0;
    double acc = 0;
    volatile long double lacc = 0;                     /* x87 registers */
    for (int n = 0; n < N; n++) {
        acc += inc;
        lacc += (long double)inc * 3;
        if (n % 3000 == 0) sleep_ms(1);                /* give the scheduler a reason to switch */
    }
    if (acc != N * inc) { printf("  child %d: double sum wrong: %f\n", id, acc); bad |= 2; }
    if (lacc != (long double)N * inc * 3) { printf("  child %d: long double sum wrong\n", id); bad |= 4; }

    double basel = 0;                                  /* sum of 1/k^2 -> pi^2/6 */
    for (int k = 1; k <= 200000; k++) {
        basel += 1.0 / ((double)k * k);
        if (k % 40000 == 0) sleep_ms(1);
    }
    double want = 1.6449340668482264 - 1.0 / 200000 + 0.5 / (200000.0 * 200000.0);
    double diff = basel - want;
    if (diff < 0) diff = -diff;
    if (diff > 1e-9) { printf("  child %d: series off by %f\n", id, diff); bad |= 8; }

    double r = __builtin_sqrt(2.0);                    /* an SSE instruction */
    if (r * r < 1.999999999 || r * r > 2.000000001) bad |= 16;
    printf("  child %d: sums exact, basel(200000) = %f, sqrt(2) = %f%s\n", id, basel, r, bad ? "  FAILED" : "");
    return bad;
}

int main(void) {
    volatile double parent_val = 3.14159265358979;     /* held in an SSE register across fork */
    printf("floating point: 4 processes, preempted constantly\n");
    int kids = 4;
    for (int i = 0; i < kids; i++)
        if (fork() == 0) exit(child(i, parent_val));
    int failures = 0, st;
    while (wait(&st) > 0) if (st) failures++;
    printf("fpdemo: %s\n", failures ? "FAILED" : "PASSED");
    printf("printf %%f check: %f %.2f %8.3f %.0f %f\n", 3.14159, 2.675, -1.5, 2.5, 0.000001);
    return failures != 0;
}
