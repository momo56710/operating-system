#!/usr/bin/env python3
"""Build a FAT32 disk image from a directory tree (there is no mkfs.fat on this machine).

usage: mkfat32.py OUTPUT.img SIZE_MIB SOURCE_DIR

Writes a raw FAT32 volume with no partition table: boot sector, FSInfo, backup copies, two FATs,
and the contents of SOURCE_DIR. Names that are not plain 8.3 get long-file-name entries and a
generated ~1 alias, as other tools do. 512-byte clusters keep a small image above the 65525
clusters that make a volume FAT32.
"""
import os, struct, sys

SECTOR = 512
SPC = 1
RESERVED = 32
NFATS = 2
EOC = 0x0FFFFFFF
DATE = ((2026 - 1980) << 9) | (10 << 5) | 28

def build(out_path, size_mib, src):
    total = size_mib * 1024 * 1024 // SECTOR
    fat_sectors = 1
    while True:
        clusters = (total - RESERVED - NFATS * fat_sectors) // SPC
        need = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if need <= fat_sectors: break
        fat_sectors = need
    data_start = RESERVED + NFATS * fat_sectors
    assert clusters >= 65525, "too few clusters for FAT32"
    img = bytearray(total * SECTOR)
    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0x0FFFFFF8, EOC
    next_free = [2]

    def alloc(n):
        out = []
        for _ in range(max(n, 1)):
            c = next_free[0]; next_free[0] += 1
            assert c < clusters + 2, "image is full"
            out.append(c)
        for a, b in zip(out, out[1:]): fat[a] = b
        fat[out[-1]] = EOC
        return out

    def write_chain(chain, data):
        for i, c in enumerate(chain):
            off = (data_start + (c - 2) * SPC) * SECTOR
            piece = data[i * SECTOR:(i + 1) * SECTOR]
            img[off:off + len(piece)] = piece

    def checksum(name11):
        s = 0
        for b in name11: s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
        return s

    def short_entry(name11, attr, ntres, cluster, size):
        e = bytearray(32)
        e[0:11] = name11
        e[11] = attr; e[12] = ntres
        struct.pack_into("<HHH", e, 16, DATE, DATE, cluster >> 16)       # created, accessed, cluster high
        struct.pack_into("<HHHI", e, 22, 0, DATE, cluster & 0xFFFF, size)  # time, date, cluster low, size
        return bytes(e)

    SHORT_OK = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")

    def fits(name):
        base, dot, ext = name.rpartition(".") if "." in name else (name, "", "")
        if not base or len(base) > 8 or len(ext) > 3 or (dot and not ext): return None
        if name.count(".") > 1: return None
        for part in (base, ext):
            if part != part.upper() and part != part.lower(): return None
            if any(ch.upper() not in SHORT_OK for ch in part): return None
        n11 = (base.upper().ljust(8) + ext.upper().ljust(3)).encode("ascii")
        nt = (0x08 if base != base.upper() else 0) | (0x10 if ext and ext != ext.upper() else 0)
        return n11, nt

    def alias(name, used):
        base, _, ext = name.rpartition(".") if "." in name else (name, "", "")
        b = "".join(c for c in base.upper() if c in SHORT_OK)[:6] or "_"
        e = "".join(c for c in ext.upper() if c in SHORT_OK)[:3]
        for n in range(1, 10):
            cand = (b + "~" + str(n)).ljust(8) + e.ljust(3)
            if cand not in used: return cand.encode("ascii")
        raise RuntimeError("no alias for " + name)

    def lfn_entries(name, csum):
        units = [ord(c) for c in name] + [0]
        units += [0xFFFF] * ((-len(units)) % 13)
        count = len(units) // 13
        offs = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
        entries = []
        for k in range(count, 0, -1):
            e = bytearray(32)
            e[0] = (0x40 if k == count else 0) | k
            e[11] = 0x0F; e[13] = csum
            for i, o in enumerate(offs):
                struct.pack_into("<H", e, o, units[(k - 1) * 13 + i])
            entries.append(bytes(e))
        return entries

    def make_dir(path, parent_cluster, own_cluster=None):
        names = sorted(os.listdir(path), key=str.lower)
        entries, used = [], set()
        children = []
        # allocate this directory's first cluster up front so "." can point at it
        entries_bytes = []
        for name in names:
            full = os.path.join(path, name)
            is_dir = os.path.isdir(full)
            f = fits(name)
            if f: n11, nt = f; lfn = []
            else:
                n11 = alias(name, used); nt = 0; lfn = lfn_entries(name, checksum(n11))
            used.add(n11.decode("ascii"))
            if is_dir:
                cluster_holder = alloc(1)               # reserve the child's directory cluster now
                child_first = cluster_holder[0]
                children.append((full, child_first, cluster_holder))
                entries_bytes += lfn + [short_entry(n11, 0x10, nt, child_first, 0)]
            else:
                data = open(full, "rb").read()
                chain = alloc((len(data) + SECTOR - 1) // SECTOR) if data else []
                first = chain[0] if chain else 0
                if chain: write_chain(chain, data)
                entries_bytes += lfn + [short_entry(n11, 0x20, nt, first, len(data))]
        return entries_bytes, children

    # root directory
    root_chain = alloc(1)
    assert root_chain == [2]

    def fill(dir_path, chain, parent_first, is_root):
        entries, children = make_dir(dir_path, parent_first)
        head = []
        if not is_root:
            head.append(short_entry(b".          ", 0x10, 0, chain[0], 0))
            head.append(short_entry(b"..         ", 0x10, 0, 0 if parent_first == 2 else parent_first, 0))
        blob = b"".join(head + entries)
        need = max(1, (len(blob) + SECTOR - 1) // SECTOR)
        while len(chain) < need:                         # grow a directory that outgrew its first cluster
            extra = alloc(1)[0]
            fat[chain[-1]] = extra
            chain.append(extra)
        write_chain(chain, blob)
        for full, first, holder in children:
            fill(full, holder, chain[0], False)

    fill(src, root_chain, 0, True)

    # boot sector
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90"; bs[3:11] = b"LEOS    "
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, SPC, RESERVED, NFATS, 0, 0, 0xF8, 0, 32, 64, 0, total)
    struct.pack_into("<IHHI", bs, 36, fat_sectors, 0, 0, 2)
    struct.pack_into("<HH", bs, 48, 1, 6)
    bs[64] = 0x80; bs[66] = 0x29
    struct.pack_into("<I", bs, 67, 0x4C654F53)
    bs[71:82] = b"LEOS DISK  "; bs[82:90] = b"FAT32   "
    bs[510:512] = b"\x55\xAA"
    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<III", fsinfo, 484, 0x61417272, clusters - (next_free[0] - 2), next_free[0])
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    for base in (0, 6):                                  # primary and backup copies
        img[base * SECTOR:(base + 1) * SECTOR] = bs
        img[(base + 1) * SECTOR:(base + 2) * SECTOR] = fsinfo

    fat_bytes = struct.pack("<%dI" % len(fat), *fat) + b"\0" * (fat_sectors * SECTOR - len(fat) * 4)
    for i in range(NFATS):
        off = (RESERVED + i * fat_sectors) * SECTOR
        img[off:off + len(fat_bytes)] = fat_bytes

    with open(out_path, "wb") as f: f.write(img)
    print(f"{out_path}: {size_mib} MiB FAT32, {clusters} clusters of {SPC * SECTOR} bytes, {next_free[0] - 2} used")

if __name__ == "__main__":
    if len(sys.argv) != 4: sys.exit(__doc__)
    build(sys.argv[1], int(sys.argv[2]), sys.argv[3])
