#include "libc.h"

/* Each mode misbehaves in a different way; the kernel must survive all of them. */
static int recurse(int n) { volatile char pad[1024]; pad[0] = (char)n; return recurse(n + 1) + pad[0]; }

int main(int argc, char **argv) {
    int mode = argc > 1 ? atoi(argv[1]) : 1;
    printf("crash: mode %d\n", mode);
    switch (mode) {
    case 1: *(volatile int *)0 = 1; break;                                         /* null pointer write */
    case 2: __asm__ volatile ("hlt"); break;                                        /* privileged instruction */
    case 3: return *(volatile int *)0xFFFFFFFF80000000UL;                          /* read kernel memory */
    case 4: { volatile int a = 1, b = 0; return a / b; }                           /* divide by zero */
    case 5: return recurse(0);                                                     /* stack overflow */
    case 6: __asm__ volatile ("cli"); break;                                       /* privileged instruction */
    case 7: *(volatile char *)0x400000 = 0; break;                                 /* write to own read-only code */
    case 8: ((void (*)(void))0x500000)(); break;                                   /* jump to unmapped memory */
    case 9: {                                                                      /* bad pointers given to system calls */
        long a = write(1, (void *)0xFFFFFFFF80000000UL, 16);
        long b = write(1, (void *)1, 4096);
        long c = write(1, "ok\n", 3);
        printf("bad kernel pointer -> %ld, bad low pointer -> %ld, good pointer -> %ld\n", a, b, c);
        return 0;
    }
    }
    printf("crash: still alive?! (should not get here)\n");
    return 0;
}
