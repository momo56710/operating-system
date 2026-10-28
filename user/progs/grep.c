#include "libc.h"

/* grep pattern [file...]: print the lines that contain the text. Reads standard input without files. */
static int grep_fd(int fd, const char *pat) {
    static char line[1024];
    int matches = 0;
    long n;
    while ((n = read_line(fd, line, sizeof line)) > 0)
        if (strstr(line, pat)) { write_all(1, line, n); matches++; }
    return matches;
}

int main(int argc, char **argv) {
    if (argc < 2) { dprintf(2, "usage: grep pattern [file...]\n"); return 2; }
    int matches = 0;
    if (argc == 2) return grep_fd(0, argv[1]) ? 0 : 1;
    for (int i = 2; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { dprintf(2, "grep: %s: cannot open\n", argv[i]); continue; }
        matches += grep_fd(fd, argv[1]);
        close(fd);
    }
    return matches ? 0 : 1;
}
