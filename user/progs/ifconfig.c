#include "libc.h"

static void show_ip(const char *label, uint32_t ip) {
    printf("  %-8s %u.%u.%u.%u\n", label, ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "dns")) {                       /* ifconfig dns 1.1.1.1 */
        unsigned ip;
        if (!parse_ip(argv[2], &ip)) { dprintf(2, "ifconfig: %s is not an IP address\n", argv[2]); return 1; }
        return netctl(NET_SETDNS, &ip) < 0;
    }
    if (argc != 1) { dprintf(2, "usage: ifconfig            show the network settings\n       ifconfig dns IP     choose the DNS server\n"); return 1; }
    struct net_info n;
    if (netctl(NET_INFO, &n) < 0 || !n.up) { printf("no network interface\n"); return 1; }
    printf("eth0   MAC %02x:%02x:%02x:%02x:%02x:%02x\n", n.mac[0], n.mac[1], n.mac[2], n.mac[3], n.mac[4], n.mac[5]);
    show_ip("address", n.ip);
    show_ip("netmask", n.mask);
    show_ip("gateway", n.gw);
    show_ip("dns", n.dns);
    printf("  received %lu frames (%lu bytes), sent %lu frames (%lu bytes), %lu dropped\n",
           n.rx_frames, n.rx_bytes, n.tx_frames, n.tx_bytes, n.rx_dropped);
    return 0;
}
