#include "libc.h"

int main(int argc, char **argv) {
    int rc = 0;
    if (argc < 2) { dprintf(2, "usage: touch file...\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT);       /* creates it if missing, leaves contents alone */
        if (fd < 0) { dprintf(2, "touch: cannot create %s\n", argv[i]); rc = 1; continue; }
        close(fd);
    }
    return rc;
}
