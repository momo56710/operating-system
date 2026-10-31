#include "net.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

/* virtio-net over the legacy (I/O port) interface, which is what QEMU's `-device virtio-net-pci`
 * offers. Queue 0 receives, queue 1 transmits. Receive buffers are posted in advance and the
 * network thread polls for completed ones; transmit sends one frame at a time. */

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_NET_LEGACY 0x1000

#define REG_DEVICE_FEATURES 0x00
#define REG_GUEST_FEATURES  0x04
#define REG_QUEUE_ADDR      0x08
#define REG_QUEUE_SIZE      0x0C
#define REG_QUEUE_SELECT    0x0E
#define REG_QUEUE_NOTIFY    0x10
#define REG_STATUS          0x12
#define REG_CONFIG          0x14        /* MAC address, 6 bytes */

#define FEATURE_MAC (1u << 5)
#define ST_ACK 1
#define ST_DRIVER 2
#define ST_DRIVER_OK 4

#define DESC_WRITE 2
#define NET_HDR 10                       /* struct virtio_net_hdr without the merged-buffer field */
#define BUF_SIZE 2048
#define RX_BUFFERS 64

struct vring_desc { uint64_t addr; uint32_t len; uint16_t flags, next; } __attribute__((packed));
struct used_elem { uint32_t id, len; };

struct vq {
    uint16_t size;
    struct vring_desc *desc;
    volatile uint16_t *avail, *used;
    uint16_t avail_idx, last_used;
};

static uint16_t io_base;
static struct vq rx, tx;
static uint8_t *rx_pool, *tx_buf;
static uint64_t tx_phys;
static spinlock_t tx_lock;
static int ready;

static int vq_setup(struct vq *q, int index) {
    outw(io_base + REG_QUEUE_SELECT, index);
    q->size = inw(io_base + REG_QUEUE_SIZE);
    if (!q->size) return -1;
    uint64_t desc_bytes = (uint64_t)q->size * 16, avail_bytes = 6 + 2 * (uint64_t)q->size;
    uint64_t used_off = (desc_bytes + avail_bytes + 4095) & ~4095ULL;
    uint64_t pages = (used_off + 6 + 8 * (uint64_t)q->size + 4095) / 4096;
    uint64_t phys = pmm_alloc_contiguous(pages);
    if (!phys) return -1;
    memset(phys_to_virt(phys), 0, pages * 4096);
    q->desc = phys_to_virt(phys);
    q->avail = (volatile uint16_t *)((uint8_t *)q->desc + desc_bytes);
    q->used = (volatile uint16_t *)((uint8_t *)q->desc + used_off);
    __asm__ volatile ("outl %0, %1" : : "a"((uint32_t)(phys >> 12)), "Nd"((uint16_t)(io_base + REG_QUEUE_ADDR)));
    return 0;
}

int virtio_net_init(uint8_t mac[6]) {
    struct pci_dev d;
    if (pci_find(VIRTIO_VENDOR, VIRTIO_NET_LEGACY, &d) < 0) return -1;
    uint32_t bar0 = pci_read32(d.bus, d.slot, d.func, 0x10);
    if (!(bar0 & 1)) return -1;
    io_base = bar0 & 0xFFFC;
    pci_write32(d.bus, d.slot, d.func, 0x04, pci_read32(d.bus, d.slot, d.func, 0x04) | 0x7);

    outb(io_base + REG_STATUS, 0);
    outb(io_base + REG_STATUS, ST_ACK);
    outb(io_base + REG_STATUS, ST_ACK | ST_DRIVER);
    uint32_t features;
    __asm__ volatile ("inl %1, %0" : "=a"(features) : "Nd"((uint16_t)(io_base + REG_DEVICE_FEATURES)));
    __asm__ volatile ("outl %0, %1" : : "a"(features & FEATURE_MAC), "Nd"((uint16_t)(io_base + REG_GUEST_FEATURES)));

    if (vq_setup(&rx, 0) < 0 || vq_setup(&tx, 1) < 0) { kprintf("virtio-net: queue setup failed\n"); return -1; }

    uint64_t rx_phys = pmm_alloc_contiguous(RX_BUFFERS * BUF_SIZE / 4096);
    tx_phys = pmm_alloc_contiguous(1);
    if (!rx_phys || !tx_phys) { kprintf("virtio-net: out of memory\n"); return -1; }
    rx_pool = phys_to_virt(rx_phys);
    tx_buf = phys_to_virt(tx_phys);

    for (int i = 0; i < RX_BUFFERS; i++) {                       /* hand every receive buffer to the device */
        rx.desc[i] = (struct vring_desc){ rx_phys + (uint64_t)i * BUF_SIZE, BUF_SIZE, DESC_WRITE, 0 };
        rx.avail[2 + i] = i;
    }
    rx.avail_idx = RX_BUFFERS;
    __sync_synchronize();
    rx.avail[1] = rx.avail_idx;

    outb(io_base + REG_STATUS, ST_ACK | ST_DRIVER | ST_DRIVER_OK);
    outw(io_base + REG_QUEUE_NOTIFY, 0);

    for (int i = 0; i < 6; i++) mac[i] = inb(io_base + REG_CONFIG + i);
    ready = 1;
    kprintf("virtio-net: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

int virtio_net_send(const void *frame, uint32_t len) {
    if (!ready || len > BUF_SIZE - NET_HDR) return -1;
    uint32_t real = len;
    if (len < 60) len = 60;                                      /* minimum Ethernet frame; the tail is zero padding */
    uint64_t f = spin_lock_irqsave(&tx_lock);
    memset(tx_buf, 0, NET_HDR + len);
    memcpy(tx_buf + NET_HDR, frame, real);
    tx.desc[0] = (struct vring_desc){ tx_phys, NET_HDR + len, 0, 0 };
    tx.avail[2 + (tx.avail_idx % tx.size)] = 0;
    __sync_synchronize();
    tx.avail[1] = ++tx.avail_idx;
    __sync_synchronize();
    outw(io_base + REG_QUEUE_NOTIFY, 1);
    int ok = 0;
    for (unsigned long spins = 0; spins < 50000000UL; spins++) {
        if (tx.used[1] != tx.last_used) { tx.last_used++; ok = 1; break; }
        __asm__ volatile ("pause");
    }
    spin_unlock_irqrestore(&tx_lock, f);
    return ok ? 0 : -1;
}

int virtio_net_poll(void (*cb)(const uint8_t *frame, uint32_t len)) {
    if (!ready) return 0;
    int n = 0;
    while (rx.used[1] != rx.last_used) {
        __sync_synchronize();
        struct used_elem *e = (struct used_elem *)((uint8_t *)rx.used + 4) + (rx.last_used % rx.size);
        uint32_t id = e->id, len = e->len;
        rx.last_used++;
        if (id < RX_BUFFERS && len > NET_HDR) cb(rx_pool + (uint64_t)id * BUF_SIZE + NET_HDR, len - NET_HDR);
        rx.avail[2 + (rx.avail_idx % rx.size)] = id;             /* give the buffer back */
        rx.avail_idx++;
        n++;
    }
    if (n) {
        __sync_synchronize();
        rx.avail[1] = rx.avail_idx;
        __sync_synchronize();
        outw(io_base + REG_QUEUE_NOTIFY, 0);
    }
    return n;
}
