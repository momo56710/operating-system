#include "libc.h"

/* A small netcat.
 *   nc host port          send standard input (until end of file), then print what comes back
 *   nc -u host port       UDP: send each input line as a datagram and print the reply
 *   nc -l port            listen for one TCP connection and print everything it sends
 * Redirect to move files: nc 10.0.2.2 9000 < file      nc -l 9000 > file */
int main(int argc, char **argv) {
    int udp = 0, listen_mode = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-u")) udp = 1;
        else if (!strcmp(argv[i], "-l")) listen_mode = 1;
        else { dprintf(2, "nc: unknown option %s\n", argv[i]); return 1; }
    }
    char buf[2048];
    long n;

    if (listen_mode) {
        if (i >= argc) { dprintf(2, "usage: nc -l port\n"); return 1; }
        int ls = socket(SOCK_TCP);
        if (ls < 0 || bind(ls, atoi(argv[i])) < 0 || listen(ls, 1) < 0) { dprintf(2, "nc: cannot listen on %s\n", argv[i]); return 1; }
        unsigned pip, pport;
        int c = accept(ls, &pip, &pport);
        if (c < 0) { dprintf(2, "nc: accept failed\n"); return 1; }
        char a[16]; ip_to_str(pip, a);
        dprintf(2, "nc: connection from %s:%u\n", a, pport);
        unsigned long total = 0;
        while ((n = read(c, buf, sizeof buf)) > 0) { write_all(1, buf, n); total += n; }
        dprintf(2, "nc: received %lu bytes\n", total);
        return 0;
    }

    if (argc - i != 2) { dprintf(2, "usage: nc [-u] host port | nc -l port\n"); return 1; }
    unsigned ip = resolve(argv[i]);
    unsigned port = (unsigned)atoi(argv[i + 1]);
    if (!ip) { dprintf(2, "nc: cannot resolve %s\n", argv[i]); return 1; }

    if (udp) {
        int fd = socket(SOCK_UDP);
        static char line[1024];
        while ((n = read_line(0, line, sizeof line)) > 0) {
            sendto(fd, line, n, ip, port);
            long r = recvfrom(fd, buf, sizeof buf, 0, 0, 2000);
            if (r > 0) write_all(1, buf, r); else dprintf(2, "nc: no reply\n");
        }
        return 0;
    }

    int fd = socket(SOCK_TCP);
    if (fd < 0 || connect(fd, ip, port) < 0) { dprintf(2, "nc: cannot connect to %s:%u\n", argv[i], port); return 1; }
    unsigned long sent = 0, got = 0;
    while ((n = read(0, buf, sizeof buf)) > 0) {
        if (write_all(fd, buf, n) < 0) { dprintf(2, "nc: send failed\n"); return 1; }
        sent += n;
    }
    shutdown(fd);                                          /* tell the server we are done sending */
    while ((n = read(fd, buf, sizeof buf)) > 0) { write_all(1, buf, n); got += n; }
    dprintf(2, "nc: sent %lu bytes, received %lu bytes\n", sent, got);
    return 0;
}
