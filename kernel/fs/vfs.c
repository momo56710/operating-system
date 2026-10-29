#include "vfs.h"
#include "heap.h"
#include "string.h"
#include "kprintf.h"

/* ---- vnodes: one per open lookup; the filesystem decides what `priv` means ---- */

struct vnode *vnode_new(int type, const struct vfs_ops *ops, void *priv, uint64_t size) {
    struct vnode *v = kmalloc(sizeof *v);
    if (!v) return 0;
    v->type = type;
    v->ops = ops;
    v->priv = priv;
    v->size = size;
    v->refs = 1;
    return v;
}

void vnode_get(struct vnode *v) { __atomic_fetch_add(&v->refs, 1, __ATOMIC_ACQ_REL); }

void vnode_put(struct vnode *v) {
    if (__atomic_sub_fetch(&v->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    if (v->ops->release) v->ops->release(v);
    kfree(v);
}

/* ---- mounts ---- */

#define MAX_MOUNTS 8
static struct { char path[64]; struct vnode *root; } mounts[MAX_MOUNTS];
static int nmounts;

void vfs_mount(const char *path, struct vnode *root) {
    if (nmounts == MAX_MOUNTS || strlen(path) >= sizeof mounts[0].path) { kprintf("vfs_mount: cannot mount %s\n", path); return; }
    int i = 0;
    while (path[i]) { mounts[nmounts].path[i] = path[i]; i++; }
    mounts[nmounts].path[i] = 0;
    mounts[nmounts].root = root;
    nmounts++;
}

static struct vnode *find_mount(const char *path) {
    for (int i = 0; i < nmounts; i++)
        if (!strcmp(mounts[i].path, path)) return mounts[i].root;
    return 0;
}

/* ---- paths ---- */

/* Make `path` absolute (relative paths start at `cwd`) and remove "." and ".." and repeated slashes. */
int vfs_normalize(const char *cwd, const char *path, char *out, size_t max) {
    char tmp[2 * PATH_MAX_LEN];
    const char *comps[48];
    int n = 0;
    size_t len = 0;

    if (path[0] != '/') {                          /* start from the working directory */
        size_t c = strlen(cwd);
        if (c >= sizeof tmp) return -1;
        memcpy(tmp, cwd, c);
        tmp[c++] = '/';
        len = c;
    }
    size_t p = strlen(path);
    if (len + p + 1 > sizeof tmp) return -1;
    memcpy(tmp + len, path, p + 1);

    for (char *s = tmp; *s; ) {
        while (*s == '/') *s++ = 0;
        if (!*s) break;
        char *start = s;
        while (*s && *s != '/') s++;
        if (*s) *s++ = 0;
        if (!strcmp(start, ".")) continue;
        if (!strcmp(start, "..")) { if (n) n--; continue; }
        if (n == 48) return -1;
        comps[n++] = start;
    }
    size_t o = 0;
    if (!n) { if (max < 2) return -1; out[0] = '/'; out[1] = 0; return 0; }
    for (int i = 0; i < n; i++) {
        size_t cl = strlen(comps[i]);
        if (o + 1 + cl + 1 > max) return -1;
        out[o++] = '/';
        memcpy(out + o, comps[i], cl);
        o += cl;
    }
    out[o] = 0;
    return 0;
}

struct vnode *vfs_lookup(const char *abs_path) {
    char path[PATH_MAX_LEN];
    if (vfs_normalize("/", abs_path, path, sizeof path) < 0) return 0;

    struct vnode *cur = find_mount("/");
    if (!cur) return 0;
    vnode_get(cur);

    char sofar[PATH_MAX_LEN];
    size_t sl = 0;
    sofar[0] = 0;
    for (char *s = path; *s; ) {
        while (*s == '/') s++;
        if (!*s) break;
        char *start = s;
        while (*s && *s != '/') s++;
        char comp[NAME_MAX_LEN];
        size_t cl = s - start;
        if (cl >= sizeof comp) { vnode_put(cur); return 0; }
        memcpy(comp, start, cl);
        comp[cl] = 0;

        sofar[sl++] = '/';
        memcpy(sofar + sl, comp, cl + 1);
        sl += cl;

        struct vnode *m = find_mount(sofar);       /* a mount point wins over whatever is underneath */
        if (m) { vnode_put(cur); cur = m; vnode_get(cur); continue; }

        struct vnode *next;
        if (cur->type != FT_DIR || !cur->ops->lookup || cur->ops->lookup(cur, comp, &next) < 0) {
            vnode_put(cur);
            return 0;
        }
        vnode_put(cur);
        cur = next;
    }
    return cur;
}

/* Split an absolute path into its directory (referenced) and last component. */
static struct vnode *parent_of(const char *abs_path, char *leaf) {
    char path[PATH_MAX_LEN];
    if (vfs_normalize("/", abs_path, path, sizeof path) < 0 || !strcmp(path, "/")) return 0;
    size_t n = strlen(path);
    while (path[n - 1] != '/') n--;
    size_t ll = strlen(path + n);
    if (ll >= NAME_MAX_LEN) return 0;
    memcpy(leaf, path + n, ll + 1);
    path[n > 1 ? n - 1 : 1] = 0;
    return vfs_lookup(path);
}

int vfs_mkdir(const char *abs_path) {
    char leaf[NAME_MAX_LEN];
    struct vnode *parent = parent_of(abs_path, leaf);
    if (!parent) return -1;
    struct vnode *out;
    int r = -1;
    if (parent->type == FT_DIR && parent->ops->create && parent->ops->create(parent, leaf, FT_DIR, &out) == 0) {
        vnode_put(out);
        r = 0;
    }
    vnode_put(parent);
    return r;
}

int vfs_unlink(const char *abs_path) {
    char leaf[NAME_MAX_LEN];
    struct vnode *parent = parent_of(abs_path, leaf);
    if (!parent) return -1;
    int r = parent->ops->unlink ? parent->ops->unlink(parent, leaf) : -1;
    vnode_put(parent);
    return r;
}

static uint64_t vsize(struct vnode *v) { return v->ops->getsize ? v->ops->getsize(v) : v->size; }

int vfs_stat(const char *abs_path, struct stat *st) {
    struct vnode *v = vfs_lookup(abs_path);
    if (!v) return -1;
    st->size = vsize(v);
    st->type = v->type;
    st->reserved = 0;
    vnode_put(v);
    return 0;
}

/* ---- open files ---- */

struct file *file_from_vnode(struct vnode *v, int flags) {
    struct file *f = kmalloc(sizeof *f);
    if (!f) { vnode_put(v); return 0; }
    f->vn = v;                                   /* takes over the caller's reference */
    f->off = 0;
    f->flags = flags;
    f->refs = 1;
    return f;
}

struct file *file_open(const char *abs_path, int flags) {
    struct vnode *v = vfs_lookup(abs_path);
    if (!v) {
        if (!(flags & O_CREAT)) return 0;
        char leaf[NAME_MAX_LEN];
        struct vnode *parent = parent_of(abs_path, leaf);
        if (!parent) return 0;
        int r = parent->type == FT_DIR && parent->ops->create ? parent->ops->create(parent, leaf, FT_FILE, &v) : -1;
        vnode_put(parent);
        if (r < 0) return 0;
    } else if ((flags & O_TRUNC) && v->type == FT_FILE && (flags & 3) != O_RDONLY) {
        if (!v->ops->truncate || v->ops->truncate(v, 0) < 0) { vnode_put(v); return 0; }
    }
    if (v->type == FT_DIR && (flags & 3) != O_RDONLY) { vnode_put(v); return 0; }
    return file_from_vnode(v, flags);
}

struct file *file_dup(struct file *f) {
    __atomic_fetch_add(&f->refs, 1, __ATOMIC_ACQ_REL);
    return f;
}

void file_close(struct file *f) {
    if (__atomic_sub_fetch(&f->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    if (f->vn->ops->close) f->vn->ops->close(f->vn, f->flags);
    vnode_put(f->vn);
    kfree(f);
}

int64_t file_read(struct file *f, void *buf, uint64_t n) {
    if ((f->flags & 3) == O_WRONLY || !f->vn->ops->read) return -1;
    int64_t r = f->vn->ops->read(f->vn, f->off, buf, n);
    if (r > 0) f->off += r;
    return r;
}

int64_t file_write(struct file *f, const void *buf, uint64_t n) {
    if ((f->flags & 3) == O_RDONLY || !f->vn->ops->write) return -1;
    if ((f->flags & O_APPEND) && f->vn->type == FT_FILE) f->off = vsize(f->vn);
    int64_t r = f->vn->ops->write(f->vn, f->off, buf, n);
    if (r > 0) f->off += r;
    return r;
}

uint64_t file_size(struct file *f) { return vsize(f->vn); }

int64_t file_seek(struct file *f, int64_t off, int whence) {
    if (f->vn->type != FT_FILE && f->vn->type != FT_DIR) return -1;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)f->off :
                   whence == SEEK_END ? (int64_t)vsize(f->vn) : -1;
    if (base < 0 || base + off < 0) return -1;
    f->off = (uint64_t)(base + off);
    return (int64_t)f->off;
}

int file_readdir(struct file *f, uint64_t index, struct dirent *out) {
    if (f->vn->type != FT_DIR || !f->vn->ops->readdir) return -1;
    return f->vn->ops->readdir(f->vn, index, out);
}

int64_t file_ioctl(struct file *f, uint64_t req, uint64_t arg) {
    return f->vn->ops->ioctl ? f->vn->ops->ioctl(f->vn, req, arg) : -1;
}
