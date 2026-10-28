#include "libc.h"

int main(int argc, char **argv) {
    int newline = 1, first = 1;
    if (argc > 1 && !strcmp(argv[1], "-n")) { newline = 0; first = 2; }
    for (int i = first; i < argc; i++) {
        write_all(1, argv[i], strlen(argv[i]));
        if (i + 1 < argc) write_all(1, " ", 1);
    }
    if (newline) write_all(1, "\n", 1);
    return 0;
}
