#include "blk.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "kprintf.h"
#include "string.h"
#include "sync.h"

/* virtio-blk over the legacy (I/O port) interface: what QEMU's `-drive if=virtio` provides.
 * One virtqueue; requests are submitted and polled to completion, one at a time. */

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_BLK_LEGACY 0x1001      /* transitional virtio-blk */

#define REG_GUEST_FEATURES 0x04
#define REG_QUEUE_ADDR     0x08
#define REG_QUEUE_SIZE     0x0C
#define REG_QUEUE_SELECT   0x0E
#define REG_QUEUE_NOTIFY   0x10
#define REG_STATUS         0x12
#define REG_CONFIG         0x14       /* virtio-blk: u64 capacity in sectors */

#define ST_ACK 1
#define ST_DRIVER 2
#define ST_DRIVER_OK 4
#define ST_FAILED 0x80

#define DESC_NEXT 1
#define DESC_WRITE 2                  /* the device writes into this buffer */

struct vring_desc { uint64_t addr; uint32_t len; uint16_t flags, next; } __attribute__((packed));

static uint16_t io_base;
static uint16_t qsize;
static struct vring_desc *desc;
static volatile uint16_t *avail;      /* flags, idx, ring[] */
static volatile uint16_t *used;       /* flags, idx, then {u32 id, u32 len} entries */
static uint16_t avail_idx, last_used;
static uint64_t capacity;
static int ready;

static uint64_t buf_phys;             /* page 0: request header + status; pages 1-2: up to 8 sectors of data */
static uint8_t *buf_virt;
#define DATA_OFF 4096
static struct mutex lock;

uint64_t virtio_blk_sectors(void) { return ready ? capacity : 0; }

int virtio_blk_init(void) {
    struct pci_dev d;
    if (pci_find(VIRTIO_VENDOR, VIRTIO_BLK_LEGACY, &d) < 0) return -1;

    uint32_t bar0 = pci_read32(d.bus, d.slot, d.func, 0x10);
    if (!(bar0 & 1)) { kprintf("virtio-blk: BAR0 is not an I/O port range\n"); return -1; }
    io_base = bar0 & 0xFFFC;
    pci_write32(d.bus, d.slot, d.func, 0x04, pci_read32(d.bus, d.slot, d.func, 0x04) | 0x7);   /* I/O, memory, bus master */

    outb(io_base + REG_STATUS, 0);                              /* reset */
    outb(io_base + REG_STATUS, ST_ACK);
    outb(io_base + REG_STATUS, ST_ACK | ST_DRIVER);
    __asm__ volatile ("outl %0, %1" : : "a"((uint32_t)0), "Nd"((uint16_t)(io_base + REG_GUEST_FEATURES)));   /* accept no optional features */

    outw(io_base + REG_QUEUE_SELECT, 0);
    qsize = inw(io_base + REG_QUEUE_SIZE);
    if (!qsize) { kprintf("virtio-blk: queue 0 missing\n"); return -1; }

    /* Legacy layout: descriptors, then the available ring, then (page aligned) the used ring. */
    uint64_t desc_bytes = (uint64_t)qsize * 16, avail_bytes = 6 + 2 * (uint64_t)qsize;
    uint64_t used_off = (desc_bytes + avail_bytes + 4095) & ~4095ULL;
    uint64_t total = used_off + 6 + 8 * (uint64_t)qsize;
    uint64_t pages = (total + 4095) / 4096;
    uint64_t ring = pmm_alloc_contiguous(pages);
    if (!ring) { kprintf("virtio-blk: no memory for the ring\n"); return -1; }
    memset(phys_to_virt(ring), 0, pages * 4096);
    desc = phys_to_virt(ring);
    avail = (volatile uint16_t *)((uint8_t *)desc + desc_bytes);
    used = (volatile uint16_t *)((uint8_t *)desc + used_off);

    buf_phys = pmm_alloc_contiguous(3);
    if (!buf_phys) { kprintf("virtio-blk: no memory for buffers\n"); return -1; }
    buf_virt = phys_to_virt(buf_phys);

    __asm__ volatile ("outl %0, %1" : : "a"((uint32_t)(ring >> 12)), "Nd"((uint16_t)(io_base + REG_QUEUE_ADDR)));
    outb(io_base + REG_STATUS, ST_ACK | ST_DRIVER | ST_DRIVER_OK);

    uint32_t lo, hi;
    __asm__ volatile ("inl %1, %0" : "=a"(lo) : "Nd"((uint16_t)(io_base + REG_CONFIG)));
    __asm__ volatile ("inl %1, %0" : "=a"(hi) : "Nd"((uint16_t)(io_base + REG_CONFIG + 4)));
    capacity = ((uint64_t)hi << 32) | lo;
    mutex_init(&lock);
    ready = 1;
    kprintf("virtio-blk: %lu sectors (%lu MiB), queue size %u\n", capacity, capacity / 2048, qsize);
    return 0;
}

int virtio_blk_rw(uint64_t sector, uint32_t count, void *data, int write) {
    if (!ready || !count || count > 8 || sector + count > capacity) return -1;
    mutex_lock(&lock);

    uint32_t *hdr = (uint32_t *)buf_virt;                       /* struct virtio_blk_req header */
    hdr[0] = write ? 1 : 0;
    hdr[1] = 0;
    *(uint64_t *)(hdr + 2) = sector;
    volatile uint8_t *status = buf_virt + 16;
    *status = 0xFF;
    uint32_t bytes = count * SECTOR_SIZE;
    if (write) memcpy(buf_virt + DATA_OFF, data, bytes);

    desc[0] = (struct vring_desc){ buf_phys, 16, DESC_NEXT, 1 };
    desc[1] = (struct vring_desc){ buf_phys + DATA_OFF, bytes, DESC_NEXT | (write ? 0 : DESC_WRITE), 2 };
    desc[2] = (struct vring_desc){ buf_phys + 16, 1, DESC_WRITE, 0 };

    avail[2 + (avail_idx % qsize)] = 0;                         /* head of the chain */
    __sync_synchronize();
    avail[1] = ++avail_idx;
    __sync_synchronize();
    outw(io_base + REG_QUEUE_NOTIFY, 0);

    for (unsigned long spins = 0; used[1] == last_used; spins++) {
        if (spins > 200000000UL) { mutex_unlock(&lock); kprintf("virtio-blk: request timed out\n"); return -1; }
        __asm__ volatile ("pause");
    }
    last_used++;
    __sync_synchronize();

    int ok = *status == 0;
    if (ok && !write) memcpy(data, buf_virt + DATA_OFF, bytes);
    mutex_unlock(&lock);
    return ok ? 0 : -1;
}
