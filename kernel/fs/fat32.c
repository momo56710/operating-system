#include "vfs.h"
#include "blk.h"
#include "heap.h"
#include "string.h"
#include "kprintf.h"
#include "sync.h"

/* FAT32 on a raw volume (no partition table). Supports reading, writing, creating and deleting
 * files and directories, with long file names. One volume, one big lock. Timestamps are not kept.
 * Writes go through the write-through block cache, so the disk is always up to date. */

#define ATTR_RO 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME 0x08
#define ATTR_DIR 0x10
#define ATTR_LFN 0x0F
#define EOC 0x0FFFFFF8u
#define MAX_LFN_ENTRIES 20

static struct {
    int      mounted;
    uint32_t spc, reserved, nfats, fat_sectors, root_cluster, total_sectors, data_start, cluster_count;
    uint32_t cluster_bytes, entries_per_cluster, free_hint;
    struct mutex lock;
} fs;

/* Where one 32-byte directory entry lives on disk. */
struct loc { uint64_t lba; uint32_t off; };

struct fnode {
    uint32_t first;                 /* first cluster (0 for an empty file) */
    uint32_t size;
    int      is_dir;
    struct loc ent;                 /* where its short entry is (lba 0 for the root directory) */
    uint32_t hint_idx, hint_cluster;/* cluster-chain position of the last access, to keep sequential I/O linear */
};

static const struct vfs_ops fat_ops;

static inline uint64_t cluster_lba(uint32_t c) { return fs.data_start + (uint64_t)(c - 2) * fs.spc; }

/* ---- the FAT itself ---- */

static uint32_t fat_get(uint32_t c) {
    uint8_t sec[SECTOR_SIZE];
    if (blk_read(fs.reserved + (c * 4) / SECTOR_SIZE, 1, sec) < 0) return EOC;
    return *(uint32_t *)(sec + (c * 4) % SECTOR_SIZE) & 0x0FFFFFFF;
}

static int fat_set(uint32_t c, uint32_t v) {
    uint8_t sec[SECTOR_SIZE];
    uint64_t rel = (c * 4) / SECTOR_SIZE;
    if (blk_read(fs.reserved + rel, 1, sec) < 0) return -1;
    uint32_t *e = (uint32_t *)(sec + (c * 4) % SECTOR_SIZE);
    *e = (*e & 0xF0000000) | (v & 0x0FFFFFFF);
    for (uint32_t i = 0; i < fs.nfats; i++)                          /* keep every copy of the FAT identical */
        if (blk_write(fs.reserved + i * fs.fat_sectors + rel, 1, sec) < 0) return -1;
    return 0;
}

static int zero_cluster(uint32_t c) {
    uint8_t z[SECTOR_SIZE];
    memset(z, 0, sizeof z);
    for (uint32_t i = 0; i < fs.spc; i++)
        if (blk_write(cluster_lba(c) + i, 1, z) < 0) return -1;
    return 0;
}

/* Find a free cluster, mark it end-of-chain and zero it. Returns 0 if the disk is full. */
static uint32_t alloc_cluster(void) {
    uint8_t sec[SECTOR_SIZE];
    uint64_t loaded = ~0ULL;
    for (uint32_t n = 0; n < fs.cluster_count; n++) {
        uint32_t c = 2 + (fs.free_hint - 2 + n) % fs.cluster_count;
        uint64_t rel = (c * 4) / SECTOR_SIZE;
        if (rel != loaded) {                                         /* load the FAT sector holding c */
            if (blk_read(fs.reserved + rel, 1, sec) < 0) return 0;
            loaded = rel;
        }
        if ((*(uint32_t *)(sec + (c * 4) % SECTOR_SIZE) & 0x0FFFFFFF) != 0) continue;
        if (fat_set(c, 0x0FFFFFFF) < 0 || zero_cluster(c) < 0) return 0;
        fs.free_hint = c + 1 > fs.cluster_count + 1 ? 2 : c + 1;
        return c;
    }
    return 0;
}

static void free_chain(uint32_t c) {
    while (c >= 2 && c < EOC) {
        uint32_t next = fat_get(c);
        fat_set(c, 0);
        c = next;
    }
}

/* The cluster holding byte `idx * cluster_bytes` of the chain starting at `first`. */
static uint32_t chain_at(uint32_t first, uint32_t idx, uint32_t *hint_idx, uint32_t *hint_cluster, int extend) {
    uint32_t c = first, i = 0;
    if (hint_idx && *hint_cluster && *hint_idx <= idx) { c = *hint_cluster; i = *hint_idx; }
    while (i < idx) {
        uint32_t next = fat_get(c);
        if (next < 2 || next >= EOC) {
            if (!extend) return 0;
            next = alloc_cluster();
            if (!next || fat_set(c, next) < 0) return 0;
        }
        c = next;
        i++;
    }
    if (hint_idx) { *hint_idx = i; *hint_cluster = c; }
    return c;
}

/* ---- directory entries ---- */

static int read_sector(uint64_t lba, void *buf) { return blk_read(lba, 1, buf); }

static int entry_get(struct loc l, uint8_t out[32]) {
    uint8_t sec[SECTOR_SIZE];
    if (read_sector(l.lba, sec) < 0) return -1;
    memcpy(out, sec + l.off, 32);
    return 0;
}

static int entry_put(struct loc l, const uint8_t in[32]) {
    uint8_t sec[SECTOR_SIZE];
    if (read_sector(l.lba, sec) < 0) return -1;
    memcpy(sec + l.off, in, 32);
    return blk_write(l.lba, 1, sec);
}

/* Location of entry number `idx` in the directory starting at cluster `first`; optionally grows the directory. */
static int entry_loc(uint32_t first, uint32_t idx, int extend, struct loc *out) {
    uint32_t c = chain_at(first, idx / fs.entries_per_cluster, 0, 0, extend);
    if (!c) return -1;
    uint32_t byte = (idx % fs.entries_per_cluster) * 32;
    out->lba = cluster_lba(c) + byte / SECTOR_SIZE;
    out->off = byte % SECTOR_SIZE;
    return 0;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
    }
    return !*a && !*b;
}

static uint8_t short_checksum(const uint8_t *name11) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i];
    return sum;
}

static void short_to_string(const uint8_t *e, char *out) {
    int n = 0;
    uint8_t ntres = e[12];
    for (int i = 0; i < 8 && e[i] != ' '; i++) {
        char c = (char)e[i];
        if (i == 0 && (uint8_t)c == 0x05) c = (char)0xE5;
        if ((ntres & 0x08) && c >= 'A' && c <= 'Z') c += 32;
        out[n++] = c;
    }
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) {
            char c = (char)e[i];
            if ((ntres & 0x10) && c >= 'A' && c <= 'Z') c += 32;
            out[n++] = c;
        }
    }
    out[n] = 0;
}

/* One real entry found by a directory scan. */
struct found {
    uint8_t    raw[32];
    char       name[256];          /* long name if there is one, else the short name */
    char       alias[16];          /* the 8.3 name */
    struct loc loc;                /* where the short entry lives */
    int        nlfn;
    struct loc lfn[MAX_LFN_ENTRIES];
    uint32_t   index;              /* position in the directory */
};

typedef int (*walk_fn)(struct found *f, void *ctx);   /* nonzero stops the walk */

static void lfn_collect(const uint8_t *e, char *name, int *have) {
    int seq = e[0] & 0x1F;
    static const int offs[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    if (seq < 1 || seq > 20) { *have = 0; return; }
    for (int i = 0; i < 13; i++) {
        uint16_t ch = e[offs[i]] | (e[offs[i] + 1] << 8);
        int pos = (seq - 1) * 13 + i;
        if (ch == 0 || ch == 0xFFFF) { if (ch == 0 && !name[pos]) name[pos] = 0; continue; }
        name[pos] = ch < 128 ? (char)ch : '?';
    }
}

static int dir_walk(uint32_t first, walk_fn fn, void *ctx) {
    uint8_t *cbuf = kmalloc(fs.cluster_bytes);
    if (!cbuf) return -1;
    struct found *f = kmalloc(sizeof *f);
    if (!f) { kfree(cbuf); return -1; }

    int result = 0, stop = 0;
    uint32_t c = first, index = 0;
    char lfn[256];
    int have_lfn = 0, nlfn = 0;
    uint8_t lfn_sum = 0;
    struct loc lfn_locs[MAX_LFN_ENTRIES];

    while (c >= 2 && c < EOC && !stop) {
        if (blk_read(cluster_lba(c), fs.spc, cbuf) < 0) { result = -1; break; }
        for (uint32_t i = 0; i < fs.entries_per_cluster; i++, index++) {
            uint8_t *e = cbuf + i * 32;
            struct loc here = { cluster_lba(c) + (i * 32) / SECTOR_SIZE, (i * 32) % SECTOR_SIZE };
            if (e[0] == 0x00) { stop = 1; break; }                       /* end of directory */
            if (e[0] == 0xE5) { have_lfn = 0; nlfn = 0; continue; }
            if (e[11] == ATTR_LFN) {
                if (e[0] & 0x40) { memset(lfn, 0, sizeof lfn); have_lfn = 1; nlfn = 0; lfn_sum = e[13]; }
                if (have_lfn && nlfn < MAX_LFN_ENTRIES) { lfn_locs[nlfn++] = here; lfn_collect(e, lfn, &have_lfn); }
                continue;
            }
            if (e[11] & ATTR_VOLUME) { have_lfn = 0; nlfn = 0; continue; }

            memcpy(f->raw, e, 32);
            short_to_string(e, f->alias);
            if (have_lfn && short_checksum(e) == lfn_sum && lfn[0]) memcpy(f->name, lfn, sizeof f->name);
            else memcpy(f->name, f->alias, strlen(f->alias) + 1);
            f->loc = here;
            f->nlfn = (have_lfn && short_checksum(e) == lfn_sum) ? nlfn : 0;
            memcpy(f->lfn, lfn_locs, sizeof(struct loc) * f->nlfn);
            f->index = index;
            have_lfn = 0; nlfn = 0;
            if (!strcmp(f->alias, ".") || !strcmp(f->alias, "..")) continue;
            if (fn(f, ctx)) { stop = 1; break; }
        }
        if (!stop) c = fat_get(c);
    }
    kfree(f);
    kfree(cbuf);
    return result;
}

/* ---- vnodes ---- */

static struct vnode *make_vnode(const uint8_t *e, struct loc loc) {
    struct fnode *n = kmalloc(sizeof *n);
    if (!n) return 0;
    memset(n, 0, sizeof *n);
    n->first = ((uint32_t)(e[20] | (e[21] << 8)) << 16) | (e[26] | (e[27] << 8));
    n->size = *(uint32_t *)(e + 28);
    n->is_dir = (e[11] & ATTR_DIR) != 0;
    n->ent = loc;
    struct vnode *v = vnode_new(n->is_dir ? FT_DIR : FT_FILE, &fat_ops, n, n->size);
    if (!v) kfree(n);
    return v;
}

static void fat_release(struct vnode *v) { kfree(v->priv); }

/* Write the cached size and first cluster back into the file's directory entry. */
static int update_entry(struct fnode *n) {
    if (!n->ent.lba) return 0;
    uint8_t e[32];
    if (entry_get(n->ent, e) < 0) return -1;
    e[20] = (n->first >> 16) & 0xFF; e[21] = (n->first >> 24) & 0xFF;
    e[26] = n->first & 0xFF;         e[27] = (n->first >> 8) & 0xFF;
    *(uint32_t *)(e + 28) = n->is_dir ? 0 : n->size;
    return entry_put(n->ent, e);
}

struct lookup_ctx { const char *name; struct found *out; int hit; };

static int lookup_cb(struct found *f, void *c) {
    struct lookup_ctx *l = c;
    if (ieq(f->name, l->name) || ieq(f->alias, l->name)) { *l->out = *f; l->hit = 1; return 1; }
    return 0;
}

static int fat_lookup(struct vnode *dir, const char *name, struct vnode **out) {
    struct fnode *d = dir->priv;
    struct found *f = kmalloc(sizeof *f);
    if (!f) return -1;
    struct lookup_ctx l = { name, f, 0 };
    mutex_lock(&fs.lock);
    dir_walk(d->first, lookup_cb, &l);
    mutex_unlock(&fs.lock);
    *out = l.hit ? make_vnode(f->raw, f->loc) : 0;
    kfree(f);
    return *out ? 0 : -1;
}

struct readdir_ctx { uint64_t want, seen; struct dirent *out; int hit; };

static int readdir_cb(struct found *f, void *c) {
    struct readdir_ctx *r = c;
    if (r->seen++ != r->want) return 0;
    memset(r->out, 0, sizeof *r->out);
    size_t n = strlen(f->name);
    memcpy(r->out->name, f->name, n < NAME_MAX_LEN ? n + 1 : NAME_MAX_LEN - 1);
    r->out->name[NAME_MAX_LEN - 1] = 0;
    r->out->type = (f->raw[11] & ATTR_DIR) ? FT_DIR : FT_FILE;
    r->out->size = *(uint32_t *)(f->raw + 28);
    r->hit = 1;
    return 1;
}

static int fat_readdir(struct vnode *dir, uint64_t index, struct dirent *out) {
    struct fnode *d = dir->priv;
    struct readdir_ctx r = { index, 0, out, 0 };
    mutex_lock(&fs.lock);
    dir_walk(d->first, readdir_cb, &r);
    mutex_unlock(&fs.lock);
    return r.hit;
}

/* ---- file data ---- */

static int64_t fat_read(struct vnode *v, uint64_t off, void *buf, uint64_t n) {
    struct fnode *f = v->priv;
    if (f->is_dir || off >= f->size) return 0;
    if (n > f->size - off) n = f->size - off;
    uint8_t *out = buf;
    uint64_t done = 0;
    mutex_lock(&fs.lock);
    while (done < n) {
        uint64_t pos = off + done;
        uint32_t c = chain_at(f->first, pos / fs.cluster_bytes, &f->hint_idx, &f->hint_cluster, 0);
        if (!c) break;
        uint32_t in_cluster = pos % fs.cluster_bytes;
        uint64_t lba = cluster_lba(c) + in_cluster / SECTOR_SIZE;
        uint32_t sec_off = in_cluster % SECTOR_SIZE;
        uint64_t left = n - done;
        if (!sec_off && left >= SECTOR_SIZE) {                       /* whole sectors straight into the caller's buffer */
            uint32_t secs = (uint32_t)(left / SECTOR_SIZE);
            uint32_t room = fs.spc - in_cluster / SECTOR_SIZE;
            if (secs > room) secs = room;
            if (blk_read(lba, secs, out + done) < 0) break;
            done += (uint64_t)secs * SECTOR_SIZE;
        } else {
            uint8_t sec[SECTOR_SIZE];
            if (blk_read(lba, 1, sec) < 0) break;
            uint64_t chunk = SECTOR_SIZE - sec_off < left ? SECTOR_SIZE - sec_off : left;
            memcpy(out + done, sec + sec_off, chunk);
            done += chunk;
        }
    }
    mutex_unlock(&fs.lock);
    return (int64_t)done;
}

/* Write `n` bytes at `off`, growing the file's cluster chain as needed. Caller holds the lock. */
static int64_t write_locked(struct fnode *f, uint64_t off, const uint8_t *buf, uint64_t n) {
    uint64_t done = 0;
    while (done < n) {
        uint64_t pos = off + done;
        if (!f->first) {                                             /* first data of an empty file */
            f->first = alloc_cluster();
            if (!f->first) break;
            f->hint_idx = 0; f->hint_cluster = 0;
        }
        uint32_t c = chain_at(f->first, pos / fs.cluster_bytes, &f->hint_idx, &f->hint_cluster, 1);
        if (!c) break;                                               /* disk full */
        uint32_t in_cluster = pos % fs.cluster_bytes;
        uint64_t lba = cluster_lba(c) + in_cluster / SECTOR_SIZE;
        uint32_t sec_off = in_cluster % SECTOR_SIZE;
        uint64_t left = n - done;
        if (!sec_off && left >= SECTOR_SIZE) {
            uint32_t secs = (uint32_t)(left / SECTOR_SIZE);
            uint32_t room = fs.spc - in_cluster / SECTOR_SIZE;
            if (secs > room) secs = room;
            if (blk_write(lba, secs, buf + done) < 0) break;
            done += (uint64_t)secs * SECTOR_SIZE;
        } else {
            uint8_t sec[SECTOR_SIZE];
            if (blk_read(lba, 1, sec) < 0) break;                    /* read-modify-write a partial sector */
            uint64_t chunk = SECTOR_SIZE - sec_off < left ? SECTOR_SIZE - sec_off : left;
            memcpy(sec + sec_off, buf + done, chunk);
            if (blk_write(lba, 1, sec) < 0) break;
            done += chunk;
        }
    }
    return (int64_t)done;
}

static int64_t fat_write(struct vnode *v, uint64_t off, const void *buf, uint64_t n) {
    struct fnode *f = v->priv;
    if (f->is_dir || off + n > 0xFFFFFFFFULL) return -1;
    mutex_lock(&fs.lock);
    if (off > f->size) {                                             /* writing past the end: the gap reads as zeros */
        static const uint8_t zeros[SECTOR_SIZE];
        for (uint64_t p = f->size; p < off; ) {
            uint64_t chunk = off - p < SECTOR_SIZE ? off - p : SECTOR_SIZE;
            if (write_locked(f, p, zeros, chunk) != (int64_t)chunk) break;
            p += chunk;
            f->size = (uint32_t)p;
        }
    }
    int64_t w = write_locked(f, off, buf, n);
    if (w > 0 && off + w > f->size) f->size = (uint32_t)(off + w);
    update_entry(f);
    v->size = f->size;
    mutex_unlock(&fs.lock);
    return w == 0 && n ? -1 : w;
}

static int fat_truncate(struct vnode *v, uint64_t size) {
    struct fnode *f = v->priv;
    if (f->is_dir) return -1;
    mutex_lock(&fs.lock);
    int r = 0;
    if (size == 0) {
        free_chain(f->first);
        f->first = 0;
        f->size = 0;
    } else if (size < f->size) {
        uint32_t keep = (uint32_t)((size + fs.cluster_bytes - 1) / fs.cluster_bytes);
        uint32_t last = chain_at(f->first, keep - 1, 0, 0, 0);
        if (last) { uint32_t rest = fat_get(last); fat_set(last, 0x0FFFFFFF); free_chain(rest); }
        f->size = (uint32_t)size;
    } else if (size > f->size) {
        r = -1;                                                      /* growing is done by writing */
    }
    f->hint_idx = 0; f->hint_cluster = 0;
    if (!r) { update_entry(f); v->size = f->size; }
    mutex_unlock(&fs.lock);
    return r;
}

/* ---- creating and deleting ---- */

static int valid_short_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("!#$%&'()-@^_`{}~", c) != 0;
}

/* Is `name` exactly representable as an 8.3 name (all-upper or all-lower)? If so fill in `s` and the case flags. */
static int fits_8_3(const char *name, uint8_t s[11], uint8_t *ntres) {
    memset(s, ' ', 11);
    size_t len = strlen(name);
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : len, ext = dot ? len - base - 1 : 0;
    if (!base || base > 8 || ext > 3 || (dot && !ext)) return 0;
    int lower_b = 0, upper_b = 0, lower_e = 0, upper_e = 0;
    for (size_t i = 0; i < len; i++) {
        if (dot && name + i == dot) continue;
        char c = name[i], u = (c >= 'a' && c <= 'z') ? c - 32 : c;
        int in_ext = dot && name + i > dot;
        if (c >= 'a' && c <= 'z') { if (in_ext) lower_e = 1; else lower_b = 1; }
        if (c >= 'A' && c <= 'Z') { if (in_ext) upper_e = 1; else upper_b = 1; }
        if (!valid_short_char(u)) return 0;
        if (in_ext) s[8 + (i - base - 1)] = u; else s[i] = u;
    }
    if ((lower_b && upper_b) || (lower_e && upper_e)) return 0;       /* mixed case needs a long name */
    *ntres = (lower_b ? 0x08 : 0) | (lower_e ? 0x10 : 0);
    return 1;
}

/* Make an 8.3 alias ("NOTES_~1.TXT") for a long name; `n` is the numeric tail. */
static void make_alias(const char *name, int n, uint8_t s[11]) {
    memset(s, ' ', 11);
    const char *dot = strrchr(name, '.');
    size_t len = strlen(name), base_end = dot ? (size_t)(dot - name) : len, b = 0;
    for (size_t i = 0; i < base_end && b < 6; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (valid_short_char(c)) s[b++] = c;
    }
    if (!b) s[b++] = '_';
    s[b++] = '~';
    s[b] = '0' + n;
    if (dot) {
        size_t e = 0;
        for (const char *p = dot + 1; *p && e < 3; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 32;
            if (valid_short_char(c)) s[8 + e++] = c;
        }
    }
}

static int alias_taken(uint32_t dir_first, const uint8_t s[11]) {
    char str[16];
    uint8_t tmp[32];
    memset(tmp, 0, sizeof tmp);
    memcpy(tmp, s, 11);
    short_to_string(tmp, str);
    struct found *f = kmalloc(sizeof *f);
    if (!f) return 1;
    struct lookup_ctx l = { str, f, 0 };
    dir_walk(dir_first, lookup_cb, &l);
    kfree(f);
    return l.hit;
}

/* `need` consecutive free directory slots, growing the directory if required. */
static int dir_alloc_slots(uint32_t first, int need, struct loc *out) {
    uint32_t idx = 0, run = 0, start = 0;
    for (;; idx++) {
        struct loc l;
        uint8_t e[32];
        if (entry_loc(first, idx, 0, &l) < 0) { if (!run) start = idx; break; }   /* ran off the end: the rest is new space */
        if (entry_get(l, e) < 0) return -1;
        if (e[0] == 0x00) { if (!run) start = idx; break; }                         /* everything after is free */
        if (e[0] == 0xE5) { if (!run) start = idx; run++; if ((int)run == need) break; }
        else run = 0;
    }
    for (int i = 0; i < need; i++)
        if (entry_loc(first, start + i, 1, &out[i]) < 0) return -1;
    return 0;
}

static void set_dates(uint8_t *e) {
    uint16_t date = ((2026 - 1980) << 9) | (10 << 5) | 28;
    *(uint16_t *)(e + 16) = date;
    *(uint16_t *)(e + 18) = date;
    *(uint16_t *)(e + 24) = date;
}

static int fat_create(struct vnode *dir, const char *name, int type, struct vnode **out) {
    struct fnode *d = dir->priv;
    size_t len = strlen(name);
    if (!len || len > 255 || strchr(name, '/') || !d->is_dir) return -1;

    mutex_lock(&fs.lock);
    int result = -1;
    struct found *ex = kmalloc(sizeof *ex);
    if (!ex) { mutex_unlock(&fs.lock); return -1; }
    struct lookup_ctx l = { name, ex, 0 };
    dir_walk(d->first, lookup_cb, &l);
    kfree(ex);
    if (l.hit) { mutex_unlock(&fs.lock); return -1; }

    uint8_t sname[11], ntres = 0;
    int nlfn = 0;
    if (!fits_8_3(name, sname, &ntres)) {
        int n = 1;
        for (; n <= 9; n++) { make_alias(name, n, sname); if (!alias_taken(d->first, sname)) break; }
        if (n > 9) { mutex_unlock(&fs.lock); return -1; }
        nlfn = (int)((len + 12) / 13);
    }

    struct loc slots[MAX_LFN_ENTRIES + 1];
    if (nlfn > MAX_LFN_ENTRIES || dir_alloc_slots(d->first, nlfn + 1, slots) < 0) { mutex_unlock(&fs.lock); return -1; }

    uint8_t sum = short_checksum(sname);
    static const int offs[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    for (int k = nlfn; k >= 1; k--) {                               /* long-name entries come first, last piece first */
        uint8_t e[32];
        memset(e, 0, sizeof e);
        e[0] = (k == nlfn ? 0x40 : 0) | k;
        e[11] = ATTR_LFN;
        e[13] = sum;
        for (int i = 0; i < 13; i++) {
            size_t pos = (size_t)(k - 1) * 13 + i;
            uint16_t ch = pos < len ? (uint8_t)name[pos] : pos == len ? 0x0000 : 0xFFFF;
            e[offs[i]] = ch & 0xFF;
            e[offs[i] + 1] = ch >> 8;
        }
        if (entry_put(slots[nlfn - k], e) < 0) goto done;
    }

    uint8_t e[32];
    memset(e, 0, sizeof e);
    memcpy(e, sname, 11);
    e[11] = type == FT_DIR ? ATTR_DIR : 0x20;
    e[12] = ntres;
    set_dates(e);
    if (type == FT_DIR) {
        uint32_t c = alloc_cluster();
        if (!c) goto done;
        e[20] = (c >> 16) & 0xFF; e[21] = (c >> 24) & 0xFF; e[26] = c & 0xFF; e[27] = (c >> 8) & 0xFF;
        uint8_t dot[32] = { 0 };
        memset(dot, ' ', 11); dot[0] = '.'; dot[11] = ATTR_DIR; set_dates(dot);
        dot[20] = e[20]; dot[21] = e[21]; dot[26] = e[26]; dot[27] = e[27];
        uint8_t dotdot[32] = { 0 };
        memset(dotdot, ' ', 11); dotdot[0] = '.'; dotdot[1] = '.'; dotdot[11] = ATTR_DIR; set_dates(dotdot);
        uint32_t parent = d->first == fs.root_cluster ? 0 : d->first;      /* ".." of a top-level directory is 0 */
        dotdot[20] = (parent >> 16) & 0xFF; dotdot[21] = (parent >> 24) & 0xFF;
        dotdot[26] = parent & 0xFF; dotdot[27] = (parent >> 8) & 0xFF;
        struct loc l0, l1;
        if (entry_loc(c, 0, 0, &l0) < 0 || entry_loc(c, 1, 0, &l1) < 0 || entry_put(l0, dot) < 0 || entry_put(l1, dotdot) < 0) goto done;
    }
    if (entry_put(slots[nlfn], e) < 0) goto done;
    *out = make_vnode(e, slots[nlfn]);
    result = *out ? 0 : -1;
done:
    mutex_unlock(&fs.lock);
    return result;
}

struct count_ctx { int n; };
static int count_cb(struct found *f, void *c) { (void)f; ((struct count_ctx *)c)->n++; return 1; }

static int fat_unlink(struct vnode *dir, const char *name) {
    struct fnode *d = dir->priv;
    struct found *f = kmalloc(sizeof *f);
    if (!f) return -1;
    struct lookup_ctx l = { name, f, 0 };
    mutex_lock(&fs.lock);
    dir_walk(d->first, lookup_cb, &l);
    int r = -1;
    if (l.hit) {
        uint32_t first = ((uint32_t)(f->raw[20] | (f->raw[21] << 8)) << 16) | (f->raw[26] | (f->raw[27] << 8));
        int is_dir = (f->raw[11] & ATTR_DIR) != 0;
        struct count_ctx cc = { 0 };
        if (is_dir) dir_walk(first, count_cb, &cc);
        if (!cc.n) {                                                 /* directories must be empty */
            free_chain(first);
            r = 0;
            for (int i = 0; i < f->nlfn; i++) {
                uint8_t e[32];
                if (entry_get(f->lfn[i], e) == 0) { e[0] = 0xE5; entry_put(f->lfn[i], e); }
            }
            uint8_t e[32];
            if (entry_get(f->loc, e) < 0) r = -1; else { e[0] = 0xE5; if (entry_put(f->loc, e) < 0) r = -1; }
        }
    }
    mutex_unlock(&fs.lock);
    kfree(f);
    return r;
}

static uint64_t fat_getsize(struct vnode *v) { return ((struct fnode *)v->priv)->size; }

static const struct vfs_ops fat_ops = {
    .read = fat_read, .write = fat_write, .lookup = fat_lookup, .readdir = fat_readdir,
    .create = fat_create, .unlink = fat_unlink, .truncate = fat_truncate,
    .getsize = fat_getsize, .release = fat_release,
};

/* Mount the FAT32 volume on the first disk. Returns its root directory, or 0 if there is no usable volume. */
struct vnode *fat32_mount(void) {
    if (!virtio_blk_sectors()) return 0;
    uint8_t b[SECTOR_SIZE];
    if (blk_read(0, 1, b) < 0) return 0;
    if (b[510] != 0x55 || b[511] != 0xAA) { kprintf("fat32: no boot sector signature\n"); return 0; }
    uint16_t bps = *(uint16_t *)(b + 11);
    uint16_t fatsz16 = *(uint16_t *)(b + 22);
    uint32_t fatsz32 = *(uint32_t *)(b + 36);
    if (bps != SECTOR_SIZE || fatsz16 != 0 || !fatsz32 || !b[13]) { kprintf("fat32: not a FAT32 volume\n"); return 0; }

    fs.spc = b[13];
    fs.reserved = *(uint16_t *)(b + 14);
    fs.nfats = b[16];
    fs.fat_sectors = fatsz32;
    fs.root_cluster = *(uint32_t *)(b + 44);
    fs.total_sectors = *(uint16_t *)(b + 19) ? *(uint16_t *)(b + 19) : *(uint32_t *)(b + 32);
    fs.data_start = fs.reserved + fs.nfats * fs.fat_sectors;
    if (fs.total_sectors <= fs.data_start) { kprintf("fat32: bad geometry\n"); return 0; }
    fs.cluster_count = (fs.total_sectors - fs.data_start) / fs.spc;
    fs.cluster_bytes = fs.spc * SECTOR_SIZE;
    fs.entries_per_cluster = fs.cluster_bytes / 32;
    fs.free_hint = 2;
    mutex_init(&fs.lock);
    fs.mounted = 1;

    struct fnode *root = kmalloc(sizeof *root);
    if (!root) return 0;
    memset(root, 0, sizeof *root);
    root->first = fs.root_cluster;
    root->is_dir = 1;
    kprintf("fat32: %u clusters of %u bytes, root at cluster %u\n", fs.cluster_count, fs.cluster_bytes, fs.root_cluster);
    return vnode_new(FT_DIR, &fat_ops, root, 0);
}
