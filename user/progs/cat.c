#include "libc.h"

/* cat [file...]: copy files (or standard input) to standard output. */
static int copy_fd(int fd) {
    char buf[1024];
    long n;
    while ((n = read(fd, buf, sizeof buf)) > 0)
        if (write_all(1, buf, n) < 0) return 1;
    return n < 0;
}

int main(int argc, char **argv) {
    if (argc == 1) return copy_fd(0);
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { dprintf(2, "cat: %s: cannot open\n", argv[i]); rc = 1; continue; }
        rc |= copy_fd(fd);
        close(fd);
    }
    return rc;
}
