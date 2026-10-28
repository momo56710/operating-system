#include "vfs.h"
#include "keyboard.h"
#include "kprintf.h"
#include "heap.h"
#include "string.h"

/* /dev: console (keyboard in, screen + serial out), null, zero. */

enum { DEV_CONSOLE, DEV_NULL, DEV_ZERO, DEV_COUNT };
static const char *dev_names[DEV_COUNT] = { "console", "null", "zero" };

static const struct vfs_ops dev_ops, devdir_ops;

/* ---- console: a tiny line discipline, so programs get whole edited lines ---- */

static char line[256];
static unsigned line_len, line_pos;

static int64_t console_read(uint64_t n, void *dst) {
    if (!n) return 0;
    if (line_pos == line_len) {                         /* need a fresh line */
        line_len = line_pos = 0;
        for (;;) {
            char c = kbd_getc();
            if (c == 4) {                               /* Ctrl-D */
                if (!line_len) return 0;                /* on an empty line it means end of input */
                break;
            }
            if (c == '\n') { kputc('\n'); line[line_len++] = '\n'; break; }
            if (c == '\b') { if (line_len) { line_len--; kputc('\b'); kputc(' '); kputc('\b'); } continue; }
            if (c >= ' ' && c < 127 && line_len < sizeof line - 1) { line[line_len++] = c; kputc(c); }
        }
    }
    uint64_t got = line_len - line_pos < n ? line_len - line_pos : n;
    memcpy(dst, line + line_pos, got);
    line_pos += got;
    return (int64_t)got;
}

/* "ESC [ 2 J" clears the screen; everything else is shown as is. */
static int64_t console_write(const char *buf, uint64_t n) {
    uint64_t start = 0;
    for (uint64_t i = 0; i + 4 <= n; i++) {
        if (buf[i] == 0x1b && buf[i + 1] == '[' && buf[i + 2] == '2' && buf[i + 3] == 'J') {
            kwrite(buf + start, i - start);
            kclear();
            i += 3;
            start = i + 1;
        }
    }
    kwrite(buf + start, n - start);
    return (int64_t)n;
}

static int64_t dev_read(struct vnode *v, uint64_t off, void *buf, uint64_t n) {
    (void)off;
    switch ((long)v->priv) {
    case DEV_CONSOLE: return console_read(n, buf);
    case DEV_ZERO: memset(buf, 0, n); return (int64_t)n;
    default: return 0;                                  /* /dev/null reads as empty */
    }
}

static int64_t dev_write(struct vnode *v, uint64_t off, const void *buf, uint64_t n) {
    (void)off;
    if ((long)v->priv == DEV_CONSOLE) return console_write(buf, n);
    return (int64_t)n;                                  /* null and zero swallow everything */
}

static const struct vfs_ops dev_ops = { .read = dev_read, .write = dev_write };

/* ---- the directory ---- */

static int dir_lookup(struct vnode *dir, const char *name, struct vnode **out) {
    (void)dir;
    for (long i = 0; i < DEV_COUNT; i++)
        if (!strcmp(name, dev_names[i])) { *out = vnode_new(FT_CHAR, &dev_ops, (void *)i, 0); return *out ? 0 : -1; }
    return -1;
}

static int dir_readdir(struct vnode *dir, uint64_t index, struct dirent *out) {
    (void)dir;
    if (index >= DEV_COUNT) return 0;
    memset(out, 0, sizeof *out);
    memcpy(out->name, dev_names[index], strlen(dev_names[index]) + 1);
    out->type = FT_CHAR;
    return 1;
}

static const struct vfs_ops devdir_ops = { .lookup = dir_lookup, .readdir = dir_readdir };

struct vnode *devfs_new(void) {
    return vnode_new(FT_DIR, &devdir_ops, 0, 0);
}
