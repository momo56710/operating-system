#include "blk.h"
#include "kprintf.h"

/* Picks whichever disk driver finds hardware: virtio-blk first (QEMU), then AHCI (SATA: VirtualBox,
 * VMware, real machines). The rest of the kernel only sees blkdev_*. */

static int (*rw_fn)(uint64_t, uint32_t, void *, int);
static uint64_t (*sectors_fn)(void);
static const char *name;

int blkdev_init(void) {
    if (virtio_blk_init() == 0) { rw_fn = virtio_blk_rw; sectors_fn = virtio_blk_sectors; name = "virtio-blk"; return 0; }
    if (ahci_init() == 0)       { rw_fn = ahci_rw;       sectors_fn = ahci_sectors;       name = "AHCI/SATA";   return 0; }
    return -1;
}

uint64_t blkdev_sectors(void) { return sectors_fn ? sectors_fn() : 0; }
int blkdev_rw(uint64_t sector, uint32_t count, void *buf, int write) { return rw_fn ? rw_fn(sector, count, buf, write) : -1; }
const char *blkdev_name(void) { return name ? name : "none"; }
