--@ title: Disk info
--@ about: block cache and disk counters
local d = leos.diskinfo()
if not d or not d.present then print("no disk attached") os.exit(0) end
print(("disk: %d sectors (%.1f MiB)"):format(d.sectors, d.sectors * 512 / 1048576))
print(("cache: %d hits, %d misses (%.0f%% hit rate)"):format(d.cache_hits, d.cache_misses, 100 * d.cache_hits / math.max(1, d.cache_hits + d.cache_misses)))
print(("driver: %d sectors read, %d written"):format(d.sector_reads, d.sector_writes))
