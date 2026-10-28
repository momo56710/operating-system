#include "libc.h"

/* ls [-l] [path...] */
static int list(const char *path, int longfmt, int show_name) {
    struct stat st;
    if (stat(path, &st) < 0) { dprintf(2, "ls: %s: not found\n", path); return 1; }
    if (st.type != FT_DIR) {
        if (longfmt) printf("%c %8lu %s\n", st.type == FT_CHAR ? 'c' : '-', st.size, path);
        else printf("%s\n", path);
        return 0;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { dprintf(2, "ls: cannot open %s\n", path); return 1; }
    if (show_name) printf("%s:\n", path);
    struct dirent d;
    for (unsigned long i = 0; readdir(fd, i, &d) > 0; i++) {
        const char *mark = d.type == FT_DIR ? "/" : "";
        if (longfmt) printf("%c %8lu %s%s\n", d.type == FT_DIR ? 'd' : d.type == FT_CHAR ? 'c' : '-', d.size, d.name, mark);
        else printf("%s%s\n", d.name, mark);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    int longfmt = 0, first = 1, rc = 0;
    if (argc > 1 && !strcmp(argv[1], "-l")) { longfmt = 1; first = 2; }
    if (first >= argc) return list(".", longfmt, 0);
    for (int i = first; i < argc; i++) rc |= list(argv[i], longfmt, argc - first > 1);
    return rc;
}
