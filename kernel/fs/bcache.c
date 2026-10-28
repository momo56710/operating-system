#include "blk.h"
#include "string.h"
#include "spinlock.h"
#include "kprintf.h"

/* A direct-mapped, write-through cache of 512-byte sectors in front of the disk driver.
 * Reads that hit never touch the device; writes update the cache and go straight to disk,
 * so nothing is lost if the machine stops. */

#define CACHE_SLOTS 1024        /* 512 KiB */

static struct { uint64_t sector; uint8_t valid; uint8_t data[SECTOR_SIZE]; } cache[CACHE_SLOTS];
static spinlock_t cache_lock;
static uint64_t hits, misses, reads, writes;

static inline unsigned slot_of(uint64_t sector) { return sector % CACHE_SLOTS; }

static int cached(uint64_t sector) {
    unsigned s = slot_of(sector);
    return cache[s].valid && cache[s].sector == sector;
}

int blk_read(uint64_t sector, uint32_t count, void *buf) {
    uint8_t *out = buf;
    for (uint32_t i = 0; i < count; ) {
        uint64_t sec = sector + i;
        uint64_t f = spin_lock_irqsave(&cache_lock);
        if (cached(sec)) {
            memcpy(out + i * SECTOR_SIZE, cache[slot_of(sec)].data, SECTOR_SIZE);
            hits++;
            spin_unlock_irqrestore(&cache_lock, f);
            i++;
            continue;
        }
        spin_unlock_irqrestore(&cache_lock, f);

        /* A run of misses: fetch up to 8 sectors with one request. */
        uint32_t run = 1;
        f = spin_lock_irqsave(&cache_lock);
        while (run < 8 && i + run < count && !cached(sec + run)) run++;
        spin_unlock_irqrestore(&cache_lock, f);

        uint8_t tmp[8 * SECTOR_SIZE];
        if (virtio_blk_rw(sec, run, tmp, 0) < 0) return -1;
        f = spin_lock_irqsave(&cache_lock);
        for (uint32_t k = 0; k < run; k++) {
            unsigned s = slot_of(sec + k);
            memcpy(cache[s].data, tmp + k * SECTOR_SIZE, SECTOR_SIZE);
            cache[s].sector = sec + k;
            cache[s].valid = 1;
        }
        misses += run;
        reads += run;
        spin_unlock_irqrestore(&cache_lock, f);
        memcpy(out + i * SECTOR_SIZE, tmp, run * SECTOR_SIZE);
        i += run;
    }
    return 0;
}

int blk_write(uint64_t sector, uint32_t count, const void *buf) {
    const uint8_t *in = buf;
    for (uint32_t i = 0; i < count; ) {
        uint32_t run = count - i < 8 ? count - i : 8;
        if (virtio_blk_rw(sector + i, run, (void *)(in + i * SECTOR_SIZE), 1) < 0) return -1;
        uint64_t f = spin_lock_irqsave(&cache_lock);
        for (uint32_t k = 0; k < run; k++) {
            unsigned s = slot_of(sector + i + k);
            memcpy(cache[s].data, in + (i + k) * SECTOR_SIZE, SECTOR_SIZE);
            cache[s].sector = sector + i + k;
            cache[s].valid = 1;
        }
        writes += run;
        spin_unlock_irqrestore(&cache_lock, f);
        i += run;
    }
    return 0;
}

void disk_stats(struct si_disk *d) {
    d->sectors = virtio_blk_sectors();
    d->present = d->sectors != 0;
    d->cache_hits = hits;
    d->cache_misses = misses;
    d->sector_reads = reads;
    d->sector_writes = writes;
}
