#include "libc.h"

/* echoserv [-u] port [count]: an echo server. TCP: accepts `count` connections (default 1) and sends back
 * everything each one writes. UDP: answers `count` datagrams. */
int main(int argc, char **argv) {
    int udp = 0, i = 1;
    if (i < argc && !strcmp(argv[i], "-u")) { udp = 1; i++; }
    if (i >= argc) { dprintf(2, "usage: echoserv [-u] port [count]\n"); return 1; }
    unsigned port = (unsigned)atoi(argv[i]);
    int count = i + 1 < argc ? atoi(argv[i + 1]) : 1;
    char buf[2048];

    if (udp) {
        int s = socket(SOCK_UDP);
        if (s < 0 || bind(s, port) < 0) { printf("echoserv: cannot bind port %u\n", port); return 1; }
        printf("echoserv: UDP port %u, waiting for %d datagram(s)\n", port, count);
        while (count--) {
            unsigned ip, p;
            long n = recvfrom(s, buf, sizeof buf, &ip, &p, 0);
            char a[16]; ip_to_str(ip, a);
            printf("echoserv: %ld bytes from %s:%u\n", n, a, p);
            if (n > 0) sendto(s, buf, n, ip, p);
        }
        return 0;
    }

    int ls = socket(SOCK_TCP);
    if (ls < 0 || bind(ls, port) < 0 || listen(ls, 4) < 0) { printf("echoserv: cannot listen on port %u\n", port); return 1; }
    printf("echoserv: TCP port %u, waiting for %d connection(s)\n", port, count);
    while (count--) {
        unsigned ip, p;
        int c = accept(ls, &ip, &p);
        if (c < 0) { printf("echoserv: accept failed\n"); return 1; }
        char a[16]; ip_to_str(ip, a);
        printf("echoserv: connection from %s:%u\n", a, p);
        unsigned long total = 0;
        long n;
        while ((n = read(c, buf, sizeof buf)) > 0) { write_all(c, buf, n); total += n; }
        printf("echoserv: echoed %lu bytes, closing\n", total);
        close(c);
    }
    return 0;
}
