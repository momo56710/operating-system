#!/usr/bin/env python3
"""A small FAT32 consistency checker (there is no fsck.fat here).

usage: fatcheck.py IMAGE [--ls]

Checks: boot sector fields, that all FAT copies are identical, every directory entry (long-name
checksums, sizes against chain lengths, "." and ".." targets), that no cluster belongs to two
files, that chains end properly and do not loop, and that no allocated cluster is unreachable.
Exits 1 if anything is wrong.
"""
import struct, sys

def main(path, show):
    d = open(path, "rb").read()
    u16 = lambda o: struct.unpack_from("<H", d, o)[0]
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    errors = []
    err = lambda m: errors.append(m)

    bps, spc, reserved, nfats = u16(11), d[13], u16(14), d[16]
    total = u32(32) or u16(19)
    fatsz, root = u32(36), u32(44)
    if bps != 512 or d[510:512] != b"\x55\xaa": err("bad boot sector")
    data_start = reserved + nfats * fatsz
    clusters = (total - data_start) // spc
    fats = [d[(reserved + i * fatsz) * bps:(reserved + (i + 1) * fatsz) * bps] for i in range(nfats)]
    if any(f != fats[0] for f in fats[1:]): err("FAT copies differ")
    fat = struct.unpack("<%dI" % (clusters + 2), fats[0][:(clusters + 2) * 4])
    nxt = lambda c: fat[c] & 0x0FFFFFFF
    cbytes = spc * bps

    owner = {}
    def chain(first, who):
        out, c, seen = [], first, set()
        while 2 <= c < 0x0FFFFFF8:
            if c in seen: err(f"{who}: loop in cluster chain at {c}"); break
            seen.add(c)
            if c in owner: err(f"{who}: cluster {c} is also used by {owner[c]}")
            owner[c] = who
            out.append(c)
            c = nxt(c)
            if c == 0: err(f"{who}: chain runs into a free cluster"); break
        if first and not out: err(f"{who}: empty chain for first cluster {first}")
        return out

    def read_chain(cl): return b"".join(d[(data_start + (c - 2) * spc) * bps:(data_start + (c - 1) * spc) * bps] for c in cl)
    def csum(n):
        s = 0
        for b in n: s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
        return s

    files = dirs = 0
    def walk(first, path, parent):
        nonlocal files, dirs
        cl = chain(first, path or "/")
        raw = read_chain(cl)
        lfn, lfn_sum, seqs = {}, None, []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0: break
            if e[0] == 0xE5: lfn, seqs = {}, []; continue
            if e[11] == 0x0F:
                if e[0] & 0x40: lfn, lfn_sum, seqs = {}, e[13], []
                n = e[0] & 0x1F; seqs.append(n)
                lfn[n] = b"".join(e[o:o + 2] for o in (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30))
                continue
            if e[11] & 0x08: continue
            short = e[0:11]
            name = (short[:8].decode("latin1").rstrip() + ("." + short[8:].decode("latin1").rstrip() if short[8:] != b"   " else ""))
            if lfn:
                if lfn_sum != csum(short): err(f"{path}/{name}: long-name checksum mismatch")
                chars = b"".join(lfn[k] for k in sorted(lfn)).decode("utf-16-le")
                name = chars.split("\0")[0]
                if sorted(lfn) != list(range(1, len(lfn) + 1)): err(f"{path}/{name}: long-name sequence broken")
            lfn, seqs = {}, []
            first_c = (u16_(e, 20) << 16) | u16_(e, 26)
            size = struct.unpack_from("<I", e, 28)[0]
            full = path + "/" + name
            if name in (".", ".."):
                want = first if name == "." else (0 if parent in (root, 0) else parent)
                if name == ".." and parent == root: want = 0
                if first_c != want: err(f"{path}: '{name}' points to {first_c}, expected {want}")
                continue
            if e[11] & 0x10:
                dirs += 1
                if show: print(f"  dir   {full}")
                walk(first_c, full, first)
            else:
                files += 1
                cl2 = chain(first_c, full) if first_c else []
                need = (size + cbytes - 1) // cbytes
                if len(cl2) != need: err(f"{full}: size {size} needs {need} clusters but the chain has {len(cl2)}")
                if show: print(f"  file  {full}  ({size} bytes)")

    def u16_(e, o): return struct.unpack_from("<H", e, o)[0]
    walk(root, "", root)

    allocated = {c for c in range(2, clusters + 2) if nxt(c) != 0}
    lost = allocated - set(owner)
    if lost: err(f"{len(lost)} allocated clusters are not reachable from any file (e.g. {sorted(lost)[:5]})")
    print(f"{path}: {files} files, {dirs} directories, {len(owner)} clusters in use, {clusters - len(allocated)} free")
    for m in errors: print("  ERROR:", m)
    print("  consistent" if not errors else f"  {len(errors)} problem(s)")
    return 1 if errors else 0

if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit(__doc__)
    sys.exit(main(sys.argv[1], "--ls" in sys.argv))
