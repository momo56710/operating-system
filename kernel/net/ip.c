#include "net.h"
#include "kprintf.h"
#include "string.h"

/* IPv4: sending (no fragmentation) and receiving (fragments are dropped). */

struct ip_hdr {
    uint8_t  ver_ihl, tos;
    uint16_t total_len, id, frag;
    uint8_t  ttl, proto;
    uint16_t checksum;
    uint32_t src, dst;
} __attribute__((packed));

uint16_t inet_checksum(const void *data, uint32_t len, uint32_t sum) {
    const uint8_t *p = data;
    while (len > 1) { sum += (uint32_t)(p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* The pseudo-header that TCP and UDP checksums also cover, as a partial sum. */
uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t proto, uint16_t len) {
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + len;
}

int ip_send(uint32_t dst, uint8_t proto, const void *payload, uint32_t len) {
    if (!netif.up || len > 1480) return -1;
    static uint16_t ident;
    uint8_t pkt[1500];
    struct ip_hdr *h = (struct ip_hdr *)pkt;
    h->ver_ihl = 0x45; h->tos = 0;
    h->total_len = htons(20 + len);
    h->id = htons(++ident);
    h->frag = htons(0x4000);                                     /* don't fragment */
    h->ttl = 64; h->proto = proto; h->checksum = 0;
    h->src = htonl(netif.ip); h->dst = htonl(dst);
    h->checksum = htons(inet_checksum(h, 20, 0));
    memcpy(pkt + 20, payload, len);

    uint32_t next_hop = ((dst & netif.mask) == (netif.ip & netif.mask) || dst == 0xFFFFFFFF) ? dst : netif.gw;
    uint8_t mac[6];
    if (arp_lookup(next_hop, mac) < 0) {
        if (net_in_rx_thread() || net_lock_depth > 0) {          /* cannot wait here: ask, and let the retransmission cope */
            arp_request(next_hop);
            return -1;
        }
        if (arp_resolve(next_hop, mac) < 0) return -1;
    }
    eth_send(mac, ETH_IP, pkt, 20 + len);
    return 0;
}

void ip_input(const uint8_t *pkt, uint32_t len, const uint8_t *src_mac) {
    if (len < 20) return;
    const struct ip_hdr *h = (const struct ip_hdr *)pkt;
    uint32_t ihl = (h->ver_ihl & 0xF) * 4, total = ntohs(h->total_len);
    if ((h->ver_ihl >> 4) != 4 || ihl < 20 || total < ihl || total > len) { netif.rx_dropped++; return; }
    if (inet_checksum(pkt, ihl, 0) != 0) { netif.rx_dropped++; return; }
    if (ntohs(h->frag) & 0x3FFF) { netif.rx_dropped++; return; }            /* a fragment: not supported */
    uint32_t src = ntohl(h->src), dst = ntohl(h->dst);
    if (dst != netif.ip && dst != 0xFFFFFFFF && dst != (netif.ip | ~netif.mask)) return;
    if ((src & netif.mask) == (netif.ip & netif.mask)) arp_learn(src, src_mac);   /* so replies need no ARP round trip */

    const uint8_t *payload = pkt + ihl;
    uint32_t plen = total - ihl;
    switch (h->proto) {
    case IPPROTO_ICMP: icmp_input(src, payload, plen); break;
    case IPPROTO_UDP:  udp_input(src, dst, payload, plen); break;
    case IPPROTO_TCP:  tcp_input(src, dst, payload, plen); break;
    default: netif.rx_dropped++;
    }
}
