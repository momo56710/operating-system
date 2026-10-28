#include "libc.h"

int main(void) {
    int shared = 100;                     /* the child gets its own copy */
    printf("parent %d: forking\n", getpid());
    int pid = fork();
    if (pid < 0) { printf("fork failed\n"); return 1; }
    if (pid == 0) {
        shared += 1;
        printf("  child %d: shared = %d (own copy), exiting with 7\n", getpid(), shared);
        exit(7);
    }
    int status = -1;
    int done = wait(&status);
    printf("parent: child %d finished with status %d (expected pid %d, status 7), my shared = %d\n",
           done, status, pid, shared);

    printf("parent: forking a child that will exec hello\n");
    pid = fork();
    if (pid == 0) {
        char *argv[] = { "hello", 0 };
        exec("/bin/hello", argv);
        printf("  exec failed!\n");
        exit(99);
    }
    wait(&status);
    printf("parent: exec'd child exited with %d (hello returns 42)\n", status);

    int bad = exec("/bin/no-such-program", 0);
    printf("parent: exec of a missing program returned %d (expected -1)\n", bad);

    printf("parent: starting 8 children at once\n");
    for (int i = 0; i < 8; i++) {
        if (fork() == 0) { sleep_ms(50 + i * 10); exit(i); }
    }
    int sum = 0, count = 0;
    while ((done = wait(&status)) > 0) { sum += status; count++; }
    printf("parent: reaped %d children, exit codes add up to %d (expected 8 and 28)\n", count, sum);
    printf("parent: wait with no children returns %d (expected -1)\n", wait(&status));
    return 0;
}
