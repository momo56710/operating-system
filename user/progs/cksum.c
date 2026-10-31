#include "libc.h"

/* cksum [file...]: CRC-32 (the same one zlib and gzip use) and size, to compare files byte for byte. */
static unsigned crc_table[256];

static void init_table(void) {
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}

static void sum_fd(int fd, const char *name) {
    unsigned crc = 0xFFFFFFFFu;
    unsigned long size = 0;
    char buf[2048];
    long n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (long i = 0; i < n; i++) crc = crc_table[(crc ^ (unsigned char)buf[i]) & 255] ^ (crc >> 8);
        size += n;
    }
    printf("%08x %lu %s\n", ~crc, size, name);
}

int main(int argc, char **argv) {
    init_table();
    if (argc == 1) { sum_fd(0, ""); return 0; }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { dprintf(2, "cksum: %s: cannot open\n", argv[i]); continue; }
        sum_fd(fd, argv[i]);
        close(fd);
    }
    return 0;
}
