#pragma once
#include <stdint.h>
#include "abi.h"

/* Block device layer: 512-byte sectors, a write-through cache in front of the driver. */
#define SECTOR_SIZE 512

int      virtio_blk_init(void);                                   /* 0 and a disk is ready, -1 no disk */
uint64_t virtio_blk_sectors(void);
int      virtio_blk_rw(uint64_t sector, uint32_t count, void *buf, int write);   /* count <= 8; 0 ok */

int  blk_read(uint64_t sector, uint32_t count, void *buf);        /* through the cache; 0 ok, -1 error */
int  blk_write(uint64_t sector, uint32_t count, const void *buf);
void disk_stats(struct si_disk *d);
