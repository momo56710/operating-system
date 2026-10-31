#include "net.h"
#include "task.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

/* ARP: finding the Ethernet address that belongs to an IP address on our network. */

struct arp_pkt {
    uint16_t htype, ptype;
    uint8_t  hlen, plen;
    uint16_t op;
    uint8_t  sha[6], spa[4], tha[6], tpa[4];
} __attribute__((packed));

#define ARP_ENTRIES 16
static struct { uint32_t ip; uint8_t mac[6]; int valid; } cache[ARP_ENTRIES];
static int next_slot;
static spinlock_t arp_lock;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void arp_learn(uint32_t ip, const uint8_t mac[6]) {
    uint64_t f = spin_lock_irqsave(&arp_lock);
    int slot = -1;
    for (int i = 0; i < ARP_ENTRIES; i++) if (cache[i].valid && cache[i].ip == ip) slot = i;
    if (slot < 0) { slot = next_slot; next_slot = (next_slot + 1) % ARP_ENTRIES; }
    cache[slot].ip = ip;
    memcpy(cache[slot].mac, mac, 6);
    cache[slot].valid = 1;
    spin_unlock_irqrestore(&arp_lock, f);
}

int arp_lookup(uint32_t ip, uint8_t mac[6]) {
    if (ip == 0xFFFFFFFF) { memcpy(mac, BROADCAST, 6); return 0; }
    int r = -1;
    uint64_t f = spin_lock_irqsave(&arp_lock);
    for (int i = 0; i < ARP_ENTRIES; i++)
        if (cache[i].valid && cache[i].ip == ip) { memcpy(mac, cache[i].mac, 6); r = 0; break; }
    spin_unlock_irqrestore(&arp_lock, f);
    return r;
}

static void send_arp(uint16_t op, const uint8_t *dst_mac, uint32_t target_ip, const uint8_t *target_mac) {
    struct arp_pkt p;
    p.htype = htons(1); p.ptype = htons(ETH_IP); p.hlen = 6; p.plen = 4; p.op = htons(op);
    memcpy(p.sha, netif.mac, 6);
    uint32_t me = htonl(netif.ip), t = htonl(target_ip);
    memcpy(p.spa, &me, 4);
    memcpy(p.tha, target_mac, 6);
    memcpy(p.tpa, &t, 4);
    eth_send(dst_mac, ETH_ARP, &p, sizeof p);
}

void arp_input(const uint8_t *pkt, uint32_t len) {
    if (len < sizeof(struct arp_pkt)) return;
    const struct arp_pkt *p = (const struct arp_pkt *)pkt;
    if (ntohs(p->htype) != 1 || ntohs(p->ptype) != ETH_IP || p->hlen != 6 || p->plen != 4) return;
    uint32_t sender, target;
    memcpy(&sender, p->spa, 4); memcpy(&target, p->tpa, 4);
    sender = ntohl(sender); target = ntohl(target);
    arp_learn(sender, p->sha);                                   /* anything we hear teaches us the sender */
    if (ntohs(p->op) == 1 && target == netif.ip)                 /* "who has our address?" */
        send_arp(2, p->sha, sender, p->sha);
}

void arp_request(uint32_t ip) {
    static const uint8_t zero[6];
    send_arp(1, BROADCAST, ip, zero);
}

/* Ask for `ip`'s Ethernet address and wait for the answer. Not for the receive thread, which has to keep running. */
int arp_resolve(uint32_t ip, uint8_t mac[6]) {
    if (arp_lookup(ip, mac) == 0) return 0;
    static const uint8_t zero[6];
    for (int attempt = 0; attempt < 3; attempt++) {
        send_arp(1, BROADCAST, ip, zero);
        for (int waited = 0; waited < 300; waited += 2) {
            sleep_ms(2);
            if (arp_lookup(ip, mac) == 0) return 0;
        }
    }
    return -1;
}
