#include "libc.h"

/* mv source dest: rename or move a file (copies it, then removes the original). */
int main(int argc, char **argv) {
    if (argc != 3) { dprintf(2, "usage: mv source dest\n"); return 1; }
    struct stat st;
    if (stat(argv[1], &st) < 0) { dprintf(2, "mv: %s: not found\n", argv[1]); return 1; }
    if (st.type == FT_DIR) { dprintf(2, "mv: directories are not supported\n"); return 1; }

    char dest[256];
    strncpy(dest, argv[2], sizeof dest - 1);
    dest[sizeof dest - 1] = 0;
    struct stat dst;
    if (stat(dest, &dst) == 0 && dst.type == FT_DIR) {          /* mv file somedir  ->  somedir/file */
        const char *base = strrchr(argv[1], '/');
        base = base ? base + 1 : argv[1];
        size_t n = strlen(dest);
        if (n && dest[n - 1] != '/') strcat(dest, "/");
        strcat(dest, base);
    }
    if (!strcmp(argv[1], dest)) return 0;

    int in = open(argv[1], O_RDONLY);
    int out = open(dest, O_WRONLY | O_CREAT | O_TRUNC);
    if (in < 0 || out < 0) { dprintf(2, "mv: cannot open %s\n", in < 0 ? argv[1] : dest); return 1; }
    char buf[1024];
    long n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write_all(out, buf, n) < 0) { dprintf(2, "mv: write failed, original kept\n"); return 1; }
    close(in); close(out);
    return unlink(argv[1]) < 0;
}
