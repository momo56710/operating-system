#include "libc.h"

/* cp source dest */
int main(int argc, char **argv) {
    if (argc != 3) { dprintf(2, "usage: cp source dest\n"); return 1; }
    int in = open(argv[1], O_RDONLY);
    if (in < 0) { dprintf(2, "cp: cannot open %s\n", argv[1]); return 1; }
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) { dprintf(2, "cp: cannot create %s\n", argv[2]); return 1; }
    char buf[1024];
    long n, total = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        if (write_all(out, buf, n) < 0) { dprintf(2, "cp: write failed\n"); return 1; }
        total += n;
    }
    printf("copied %ld bytes\n", total);
    close(in); close(out);
    return 0;
}
