#include "libc.h"

/* wc [file...]: lines, words and bytes. */
static void count(int fd, const char *name) {
    char buf[1024];
    long n, lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        bytes += n;
        for (long i = 0; i < n; i++) {
            if (buf[i] == '\n') lines++;
            int space = buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t';
            if (space) in_word = 0; else if (!in_word) { in_word = 1; words++; }
        }
    }
    printf("%7ld %7ld %7ld %s\n", lines, words, bytes, name);
}

int main(int argc, char **argv) {
    if (argc == 1) { count(0, ""); return 0; }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { dprintf(2, "wc: %s: cannot open\n", argv[i]); continue; }
        count(fd, argv[i]);
        close(fd);
    }
    return 0;
}
