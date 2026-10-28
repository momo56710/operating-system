#include "libc.h"

/* head [-n N] [file]: the first N lines (default 10). */
int main(int argc, char **argv) {
    int n = 10, i = 1;
    if (argc > 2 && !strcmp(argv[1], "-n")) { n = atoi(argv[2]); i = 3; }
    int fd = 0;
    if (i < argc && (fd = open(argv[i], O_RDONLY)) < 0) { dprintf(2, "head: cannot open %s\n", argv[i]); return 1; }
    static char line[1024];
    long len;
    while (n-- > 0 && (len = read_line(fd, line, sizeof line)) > 0) write_all(1, line, len);
    return 0;
}
