#include "libc.h"

/* tree [depth]: every process forks two children until the depth runs out, then waits for them.
 * Shows many processes at once and parents collecting exit codes. */
static int grow(int depth, int label) {
    printf("process %d (label %d, depth %d)\n", getpid(), label, depth);
    if (depth == 0) { sleep_ms(100); return 1; }
    int children = 0, total = 1;
    for (int i = 0; i < 2; i++) {
        int pid = fork();
        if (pid == 0) exit(grow(depth - 1, label * 2 + i));
        if (pid > 0) children++;
    }
    int status;
    while (children--) { if (wait(&status) > 0) total += status; }
    return total;
}

int main(int argc, char **argv) {
    int depth = argc > 1 ? atoi(argv[1]) : 3;
    int total = grow(depth, 1);
    printf("tree of depth %d had %d processes (expected %d)\n", depth, total, (1 << (depth + 1)) - 1);
    return 0;
}
