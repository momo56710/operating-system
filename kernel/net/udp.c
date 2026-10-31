#include "net.h"
#include "heap.h"
#include "string.h"

/* UDP: datagrams to and from sockets. Received datagrams wait in a per-socket queue. */

struct udp_hdr { uint16_t sport, dport, len, checksum; } __attribute__((packed));

#define DGRAM_QUEUE_MAX 32

spinlock_t net_lock;
int net_lock_depth;
static struct sock *udp_socks;            /* bound UDP sockets */

int udp_port_in_use(uint16_t port) {
    for (struct sock *s = udp_socks; s; s = s->next) if (s->lport == port) return 1;
    return 0;
}

/* called by sock.c with net_lock held */
void udp_bind_locked(struct sock *s) { s->next = udp_socks; udp_socks = s; }
void udp_unbind_locked(struct sock *s) {
    for (struct sock **pp = &udp_socks; *pp; pp = &(*pp)->next)
        if (*pp == s) { *pp = s->next; break; }
    while (s->dq_head) { struct dgram *d = s->dq_head; s->dq_head = d->next; kfree(d); }
    s->dq_tail = 0; s->dq_len = 0;
}

void udp_input(uint32_t src, uint32_t dst, const uint8_t *pkt, uint32_t len) {
    if (len < sizeof(struct udp_hdr)) return;
    const struct udp_hdr *h = (const struct udp_hdr *)pkt;
    uint16_t ulen = ntohs(h->len);
    if (ulen < sizeof *h || ulen > len) return;
    if (h->checksum && inet_checksum(pkt, ulen, pseudo_sum(src, dst, IPPROTO_UDP, ulen)) != 0) return;
    uint16_t sport = ntohs(h->sport), dport = ntohs(h->dport);
    uint16_t dlen = ulen - sizeof *h;

    uint64_t f = spin_lock_irqsave(&net_lock);
    net_lock_depth++;
    for (struct sock *s = udp_socks; s; s = s->next) {
        if (s->lport != dport) continue;
        if (s->connected && (s->rip != src || s->rport != sport)) continue;
        if (s->dq_len >= DGRAM_QUEUE_MAX) break;                         /* queue full: drop */
        struct dgram *d = kmalloc(sizeof *d + dlen);
        if (!d) break;
        d->next = 0; d->ip = src; d->port = sport; d->len = dlen;
        memcpy(d->data, pkt + sizeof *h, dlen);
        if (s->dq_tail) s->dq_tail->next = d; else s->dq_head = d;
        s->dq_tail = d;
        s->dq_len++;
        break;
    }
    net_lock_depth--;
    spin_unlock_irqrestore(&net_lock, f);
}

int udp_send(uint16_t sport, uint32_t dst, uint16_t dport, const void *data, uint32_t len) {
    if (len > 1472) return -1;
    uint8_t pkt[8 + 1472];
    struct udp_hdr *h = (struct udp_hdr *)pkt;
    h->sport = htons(sport); h->dport = htons(dport);
    h->len = htons(8 + len); h->checksum = 0;
    memcpy(pkt + 8, data, len);
    uint16_t c = inet_checksum(pkt, 8 + len, pseudo_sum(netif.ip, dst, IPPROTO_UDP, 8 + len));
    h->checksum = htons(c ? c : 0xFFFF);
    return ip_send(dst, IPPROTO_UDP, pkt, 8 + len);
}
