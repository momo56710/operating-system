#include "libc.h"

int main(int argc, char **argv) {
    if (argc != 2) { dprintf(2, "usage: sleep seconds\n"); return 1; }
    sleep_ms((unsigned long)atoi(argv[1]) * 1000);
    return 0;
}
