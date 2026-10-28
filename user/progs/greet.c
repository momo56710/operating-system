#include "libc.h"

/* greet [name]: says hello and counts its own runs in /disk/visits.txt. This program is only on the
 * disk image (/disk/bin/greet), so running it proves programs load from the disk, and the counter
 * proves that files written to the disk survive a reboot. */
int main(int argc, char **argv) {
    int visits = 0;
    int fd = open("/disk/visits.txt", O_RDONLY);
    if (fd >= 0) {
        char buf[16];
        long n = read(fd, buf, sizeof buf - 1);
        if (n > 0) { buf[n] = 0; visits = atoi(buf); }
        close(fd);
    }
    visits++;
    fd = open("/disk/visits.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("greet: cannot write /disk/visits.txt (is a disk attached?)\n"); return 1; }
    char out[16];
    int len = 0, v = visits;
    char tmp[12];
    int t = 0;
    do { tmp[t++] = '0' + v % 10; v /= 10; } while (v);
    while (t) out[len++] = tmp[--t];
    out[len++] = '\n';
    write_all(fd, out, len);
    close(fd);
    printf("Hello, %s! This program has run %d time%s.\n", argc > 1 ? argv[1] : "world", visits, visits == 1 ? "" : "s");
    return 0;
}
