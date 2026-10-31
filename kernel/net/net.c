#include "net.h"
#include "task.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"
#include "heap.h"

/* The Ethernet layer and the thread that drives the whole stack. Frames are polled from the card
 * by one kernel thread, which also runs the TCP timers. */

struct netif netif;
static struct task *rx_task;

struct eth_hdr { uint8_t dst[6], src[6]; uint16_t type; } __attribute__((packed));

int net_in_rx_thread(void) { return current_task() == rx_task; }

#ifdef NET_LOSS_TEST
/* Test switch: drop about 5% of IP frames in each direction, to exercise TCP's recovery. */
static uint32_t loss_state = 12345;
static int lose(void) { loss_state = loss_state * 1103515245u + 12345u; return ((loss_state >> 16) % 100) < 5; }
uint64_t net_lost;
#endif

void eth_send(const uint8_t dst[6], uint16_t type, const void *payload, uint32_t len) {
#ifdef NET_LOSS_TEST
    if (type == ETH_IP && lose()) { net_lost++; return; }
#endif
    uint8_t frame[1514];
    if (len > sizeof frame - sizeof(struct eth_hdr)) return;
    struct eth_hdr *h = (struct eth_hdr *)frame;
    memcpy(h->dst, dst, 6);
    memcpy(h->src, netif.mac, 6);
    h->type = htons(type);
    memcpy(frame + sizeof *h, payload, len);
    if (nic_send(frame, sizeof *h + len) == 0) {
        netif.tx_frames++;
        netif.tx_bytes += sizeof *h + len;
    }
}

static void rx_frame(const uint8_t *frame, uint32_t len) {
    if (len < sizeof(struct eth_hdr)) { netif.rx_dropped++; return; }
    const struct eth_hdr *h = (const struct eth_hdr *)frame;
    netif.rx_frames++;
    netif.rx_bytes += len;
    uint16_t type = ntohs(h->type);
#ifdef NET_LOSS_TEST
    if (type == ETH_IP && lose()) { net_lost++; return; }
#endif
    if (type == ETH_ARP) arp_input(frame + sizeof *h, len - sizeof *h);
    else if (type == ETH_IP) ip_input(frame + sizeof *h, len - sizeof *h, h->src);
    else netif.rx_dropped++;
}

static void net_thread(void *arg) {
    (void)arg;
    uint64_t last_tick = 0;
    for (;;) {
        int n = nic_poll(rx_frame);
        uint64_t now = uptime_ms();
        if (now - last_tick >= 10) { tcp_tick(); last_tick = now; }
        if (!n) sleep_ms(1);                                    /* nothing waiting: polling costs a millisecond of latency */
    }
}

void net_init(void) {
    if (nic_init(netif.mac) < 0) { kprintf("net: no network card\n"); return; }
    netif.ip   = IP4(10, 0, 2, 15);                             /* QEMU's user-mode network: static configuration */
    netif.mask = IP4(255, 255, 255, 0);
    netif.gw   = IP4(10, 0, 2, 2);
    netif.dns  = IP4(10, 0, 2, 3);
    netif.up = 1;
    rx_task = task_create("net", net_thread, 0);
    kprintf("net: %s, 10.0.2.15/24, gateway 10.0.2.2, dns 10.0.2.3\n", nic_name());
}
