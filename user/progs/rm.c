#include "libc.h"

int main(int argc, char **argv) {
    int rc = 0;
    if (argc < 2) { dprintf(2, "usage: rm file...\n"); return 1; }
    for (int i = 1; i < argc; i++)
        if (unlink(argv[i]) < 0) { dprintf(2, "rm: cannot remove %s\n", argv[i]); rc = 1; }
    return rc;
}
