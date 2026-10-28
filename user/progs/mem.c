#include "libc.h"

int main(void) {
    struct si_mem m;
    struct si_disk d;
    if (sysinfo(SI_MEM, 0, &m) < 0) return 1;
    printf("memory: %lu KiB total, %lu KiB free, %lu KiB used\n", m.total_kb, m.free_kb, m.total_kb - m.free_kb);
    printf("kernel heap: %lu B in use, %lu B free\n", m.heap_used, m.heap_free);
    printf("uptime: %lu.%03lu s\n", m.uptime_ms / 1000, m.uptime_ms % 1000);
    if (sysinfo(SI_DISK, 0, &d) == 0 && d.present)
        printf("disk: %lu sectors (%lu MiB); cache %lu hits, %lu misses; %lu sectors read, %lu written\n",
               d.sectors, d.sectors / 2048, d.cache_hits, d.cache_misses, d.sector_reads, d.sector_writes);
    else
        printf("disk: none\n");
    return 0;
}
