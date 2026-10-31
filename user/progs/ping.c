#include "libc.h"

/* ping [-c count] address */
int main(int argc, char **argv) {
    int count = 4, i = 1;
    if (argc > 2 && !strcmp(argv[1], "-c")) { count = atoi(argv[2]); i = 3; }
    if (i >= argc) { dprintf(2, "usage: ping [-c count] address\n"); return 1; }
    unsigned ip;
    if (!parse_ip(argv[i], &ip)) { dprintf(2, "ping: %s is not an IP address (names come with DNS)\n", argv[i]); return 1; }

    printf("PING %s: 32 data bytes\n", argv[i]);
    int got = 0, total = 0, min = 1 << 30, max = 0;
    for (int seq = 1; seq <= count; seq++) {
        struct ping_req p = { ip, (unsigned)seq, 1000, -1 };
        if (netctl(NET_PING, &p) == 0) {
            printf("32 bytes from %s: seq=%d time=%d ms\n", argv[i], seq, p.rtt_ms);
            got++; total += p.rtt_ms;
            if (p.rtt_ms < min) min = p.rtt_ms;
            if (p.rtt_ms > max) max = p.rtt_ms;
        } else {
            printf("seq=%d: no reply (timed out)\n", seq);
        }
        if (seq < count) sleep_ms(500);
    }
    printf("--- %s ping statistics ---\n%d sent, %d received, %d%% loss\n", argv[i], count, got, (count - got) * 100 / count);
    if (got) printf("round trip min/avg/max = %d/%d/%d ms\n", min, total / got, max);
    return got ? 0 : 1;
}
