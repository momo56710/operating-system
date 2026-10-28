#include "libc.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Hello from user mode! I am process %d, running in ring 3.\n", getpid());
    return 42;                       /* checks that exit codes reach the parent */
}
