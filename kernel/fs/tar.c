#include "vfs.h"
#include "string.h"
#include "kprintf.h"

/* Unpack a ustar archive into a memfs tree. File contents are not copied: they point into the archive. */

struct tar_header {
    char name[100], mode[8], uid[8], gid[8], size[12], mtime[12], chksum[8];
    char typeflag, linkname[100], magic[6], version[2], uname[32], gname[32], devmajor[8], devminor[8];
    char prefix[155];
} __attribute__((packed));

static uint64_t octal(const char *s, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (s[i] - '0');
    return v;
}

void tar_mount(struct vnode *root, const void *tar, uint64_t size) {
    const uint8_t *base = tar;
    int files = 0, dirs = 0;
    for (uint64_t off = 0; off + 512 <= size; ) {
        const struct tar_header *h = (const struct tar_header *)(base + off);
        if (!h->name[0]) break;                                   /* end-of-archive marker */
        uint64_t fsize = octal(h->size, 12);

        char path[PATH_MAX_LEN];
        size_t pl = 0;
        if (!memcmp(h->magic, "ustar", 5) && h->prefix[0]) {
            for (int i = 0; i < 155 && h->prefix[i] && pl < sizeof path - 2; i++) path[pl++] = h->prefix[i];
            path[pl++] = '/';
        }
        for (int i = 0; i < 100 && h->name[i] && pl < sizeof path - 1; i++) path[pl++] = h->name[i];
        path[pl] = 0;
        while (pl && path[pl - 1] == '/') path[--pl] = 0;

        const char *p = path;
        while (p[0] == '.' && p[1] == '/') p += 2;
        if (!strcmp(p, ".")) p = "";

        if (*p) {
            if (h->typeflag == '5') { memfs_add(root, p, FT_DIR, 0, 0); dirs++; }
            else if (h->typeflag == '0' || h->typeflag == 0) {
                if (off + 512 + fsize > size) { kprintf("tar: truncated archive at %s\n", p); break; }
                memfs_add(root, p, FT_FILE, base + off + 512, fsize);
                files++;
            }
        }
        off += 512 + ((fsize + 511) & ~511ULL);
    }
    kprintf("ramdisk: %d files, %d directories\n", files, dirs);
}
