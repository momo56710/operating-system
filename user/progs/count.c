#include "libc.h"

/* count [n] [delay_ms]: print a tick every delay_ms, n times. Shows sleeping and arguments. */
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 5;
    int delay = argc > 2 ? atoi(argv[2]) : 500;
    for (int i = 1; i <= n; i++) {
        printf("tick %d  (uptime %lu ms)\n", i, uptime_ms());
        sleep_ms(delay);
    }
    return 0;
}
