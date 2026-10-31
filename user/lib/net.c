#include "libc.h"

/* Network helpers for programs: dotted-quad parsing and, later, DNS and sockets. */

/* "10.0.2.2" -> host-order address; returns 0 if the text is not a dotted quad. */
int parse_ip(const char *s, unsigned *ip) {
    unsigned v = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return 0;
        unsigned n = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (*s++ - '0'); if (n > 255) return 0; }
        v = (v << 8) | n;
        if (part < 3) { if (*s++ != '.') return 0; }
    }
    if (*s) return 0;
    *ip = v;
    return 1;
}

void ip_to_str(unsigned ip, char *out) {
    snprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

/* Skip a (possibly compressed) domain name; returns the offset after it, or -1. */
static int skip_name(const unsigned char *m, int len, int off) {
    while (off < len) {
        unsigned char c = m[off];
        if (c == 0) return off + 1;
        if ((c & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : -1;
        off += 1 + c;
    }
    return -1;
}

unsigned dns_lookup(const char *name, unsigned server, unsigned port) {
    unsigned char q[300];
    int n = 0;
    static unsigned short next_id = 0x4c45;
    unsigned short id = next_id++;
    q[n++] = id >> 8; q[n++] = id & 255;
    q[n++] = 0x01; q[n++] = 0x00;                       /* flags: recursion desired */
    q[n++] = 0; q[n++] = 1;                             /* one question */
    for (int i = 0; i < 6; i++) q[n++] = 0;
    for (const char *p = name; *p; ) {                  /* the name as length-prefixed labels */
        const char *dot = strchr(p, '.');
        int l = dot ? (int)(dot - p) : (int)strlen(p);
        if (l < 1 || l > 63 || n + l + 6 > (int)sizeof q) return 0;
        q[n++] = (unsigned char)l;
        memcpy(q + n, p, l); n += l;
        p += l + (dot ? 1 : 0);
    }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 1;                             /* type A */
    q[n++] = 0; q[n++] = 1;                             /* class IN */

    int fd = socket(SOCK_UDP);
    if (fd < 0) return 0;
    unsigned result = 0;
    for (int attempt = 0; attempt < 2 && !result; attempt++) {
        sendto(fd, q, n, server, port);
        unsigned char a[512];
        long r = recvfrom(fd, a, sizeof a, 0, 0, 2500);
        if (r < 12 || a[0] != (id >> 8) || a[1] != (id & 255) || !(a[2] & 0x80) || (a[3] & 15) != 0) continue;
        int qd = (a[4] << 8) | a[5], an = (a[6] << 8) | a[7], off = 12;
        for (int i = 0; i < qd && off >= 0; i++) { off = skip_name(a, (int)r, off); if (off >= 0) off += 4; }
        for (int i = 0; i < an && off >= 0 && off + 10 <= r; i++) {
            off = skip_name(a, (int)r, off);
            if (off < 0 || off + 10 > r) break;
            int type = (a[off] << 8) | a[off + 1], rdlen = (a[off + 8] << 8) | a[off + 9];
            off += 10;
            if (type == 1 && rdlen == 4 && off + 4 <= r) {
                result = ((unsigned)a[off] << 24) | (a[off + 1] << 16) | (a[off + 2] << 8) | a[off + 3];
                break;
            }
            off += rdlen;                               /* e.g. a CNAME: keep looking */
        }
    }
    close(fd);
    return result;
}

unsigned resolve(const char *name) {
    unsigned ip;
    if (parse_ip(name, &ip)) return ip;
    struct net_info n;
    if (netctl(NET_INFO, &n) < 0 || !n.up) return 0;
    return dns_lookup(name, n.dns, 53);
}
