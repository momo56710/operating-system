#include "net.h"

/* Picks whichever network driver finds a card. Polled, so no interrupts are involved. */

static int (*send_fn)(const void *, uint32_t);
static int (*poll_fn)(void (*)(const uint8_t *, uint32_t));
static const char *name;

int nic_init(uint8_t mac[6]) {
    if (virtio_net_init(mac) == 0) { send_fn = virtio_net_send; poll_fn = virtio_net_poll; name = "virtio-net"; return 0; }
    if (e1000_init(mac) == 0)      { send_fn = e1000_send;      poll_fn = e1000_poll;      name = "Intel e1000"; return 0; }
    return -1;
}

int nic_send(const void *frame, uint32_t len) { return send_fn ? send_fn(frame, len) : -1; }
int nic_poll(void (*cb)(const uint8_t *, uint32_t)) { return poll_fn ? poll_fn(cb) : 0; }
const char *nic_name(void) { return name ? name : "none"; }
