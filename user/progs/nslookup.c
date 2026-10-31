#include "libc.h"

/* nslookup name [server [port]]: look a host name up with a DNS query over UDP. */
int main(int argc, char **argv) {
    if (argc < 2) { dprintf(2, "usage: nslookup name [server [port]]\n"); return 1; }
    struct net_info n;
    netctl(NET_INFO, &n);
    unsigned server = n.dns, port = 53;
    if (argc > 2 && !parse_ip(argv[2], &server)) { dprintf(2, "nslookup: bad server address %s\n", argv[2]); return 1; }
    if (argc > 3) port = (unsigned)atoi(argv[3]);
    char s[16], a[16];
    ip_to_str(server, s);
    printf("Server:  %s#%u\n", s, port);
    unsigned ip = dns_lookup(argv[1], server, port);
    if (!ip) { printf("can't resolve %s (no answer)\n", argv[1]); return 1; }
    ip_to_str(ip, a);
    printf("Name:    %s\nAddress: %s\n", argv[1], a);
    return 0;
}
