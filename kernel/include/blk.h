#pragma once
#include <stdint.h>
#include "abi.h"

/* Block device layer: 512-byte sectors, a write-through cache in front of the driver. */
#define SECTOR_SIZE 512

/* The disk, whichever kind the machine has: virtio-blk (QEMU) or AHCI/SATA (VirtualBox, VMware, real PCs). */
int      blkdev_init(void);                                       /* 0 and a disk is ready, -1 no disk */
uint64_t blkdev_sectors(void);
int      blkdev_rw(uint64_t sector, uint32_t count, void *buf, int write);   /* count <= 8; 0 ok */
const char *blkdev_name(void);

int      virtio_blk_init(void);                                   /* the drivers: 0 on success */
uint64_t virtio_blk_sectors(void);
int      virtio_blk_rw(uint64_t sector, uint32_t count, void *buf, int write);
int      ahci_init(void);
uint64_t ahci_sectors(void);
int      ahci_rw(uint64_t sector, uint32_t count, void *buf, int write);

int  blk_read(uint64_t sector, uint32_t count, void *buf);        /* through the cache; 0 ok, -1 error */
int  blk_write(uint64_t sector, uint32_t count, const void *buf);
void disk_stats(struct si_disk *d);
