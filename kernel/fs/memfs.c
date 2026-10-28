#include "vfs.h"
#include "heap.h"
#include "string.h"

/* A filesystem that lives in memory. The boot ramdisk uses it read-only (file data points
 * straight into the tar archive); /tmp uses it writable (file data is allocated). */

#define MAX_FILE (64ULL << 20)

struct mnode {
    int            type;
    char           name[NAME_MAX_LEN];
    uint64_t       size, cap;
    uint8_t       *data;
    int            owns;          /* data was allocated here (else it points into the archive) */
    int            ro;
    int            refs;          /* the tree itself plus every vnode on it */
    struct mnode  *parent, *child, *sibling;
};

static const struct vfs_ops memfs_ops;

static struct mnode *mnode_new(int type, const char *name, int ro) {
    struct mnode *m = kmalloc(sizeof *m);
    if (!m) return 0;
    memset(m, 0, sizeof *m);
    m->type = type;
    m->ro = ro;
    m->refs = 1;
    for (int i = 0; i < NAME_MAX_LEN - 1 && name[i]; i++) m->name[i] = name[i];
    return m;
}

static void mnode_put(struct mnode *m) {
    if (__atomic_sub_fetch(&m->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    if (m->owns && m->data) kfree(m->data);
    kfree(m);
}

static struct vnode *mn_vnode(struct mnode *m) {
    __atomic_fetch_add(&m->refs, 1, __ATOMIC_ACQ_REL);
    struct vnode *v = vnode_new(m->type, &memfs_ops, m, m->size);
    if (!v) mnode_put(m);
    return v;
}

static struct mnode *child_named(struct mnode *dir, const char *name) {
    for (struct mnode *c = dir->child; c; c = c->sibling)
        if (!strcmp(c->name, name)) return c;
    return 0;
}

static struct mnode *add_child(struct mnode *dir, const char *name, int type) {
    struct mnode *m = mnode_new(type, name, dir->ro);
    if (!m) return 0;
    m->parent = dir;
    struct mnode **pp = &dir->child;
    while (*pp) pp = &(*pp)->sibling;                 /* keep creation order */
    *pp = m;
    return m;
}

static int mem_lookup(struct vnode *dir, const char *name, struct vnode **out) {
    struct mnode *c = child_named(dir->priv, name);
    if (!c) return -1;
    *out = mn_vnode(c);
    return *out ? 0 : -1;
}

static int mem_readdir(struct vnode *dir, uint64_t index, struct dirent *out) {
    struct mnode *c = ((struct mnode *)dir->priv)->child;
    while (c && index--) c = c->sibling;
    if (!c) return 0;
    memset(out, 0, sizeof *out);
    memcpy(out->name, c->name, NAME_MAX_LEN);
    out->type = c->type;
    out->size = c->size;
    return 1;
}

static int64_t mem_read(struct vnode *v, uint64_t off, void *buf, uint64_t n) {
    struct mnode *m = v->priv;
    if (off >= m->size) return 0;
    if (n > m->size - off) n = m->size - off;
    memcpy(buf, m->data + off, n);
    return (int64_t)n;
}

static int ensure_cap(struct mnode *m, uint64_t need) {
    if (need > MAX_FILE) return -1;
    if (m->owns && m->cap >= need) return 0;
    uint64_t cap = m->cap * 2 > need ? m->cap * 2 : need;
    if (cap < 256) cap = 256;
    uint8_t *d = kmalloc(cap);
    if (!d) return -1;
    if (m->size) memcpy(d, m->data, m->size);          /* also the copy-on-write step for archive data */
    if (m->owns && m->data) kfree(m->data);
    m->data = d;
    m->cap = cap;
    m->owns = 1;
    return 0;
}

static int64_t mem_write(struct vnode *v, uint64_t off, const void *buf, uint64_t n) {
    struct mnode *m = v->priv;
    if (m->ro || m->type != FT_FILE) return -1;
    if (off + n < off || ensure_cap(m, off + n) < 0) return -1;
    if (off > m->size) memset(m->data + m->size, 0, off - m->size);
    memcpy(m->data + off, buf, n);
    if (off + n > m->size) m->size = off + n;
    v->size = m->size;
    return (int64_t)n;
}

static int mem_truncate(struct vnode *v, uint64_t size) {
    struct mnode *m = v->priv;
    if (m->ro || m->type != FT_FILE) return -1;
    if (size > m->size) {
        if (ensure_cap(m, size) < 0) return -1;
        memset(m->data + m->size, 0, size - m->size);
    }
    m->size = size;
    v->size = size;
    return 0;
}

static int mem_create(struct vnode *dir, const char *name, int type, struct vnode **out) {
    struct mnode *d = dir->priv;
    if (d->ro || child_named(d, name) || strlen(name) >= NAME_MAX_LEN) return -1;
    struct mnode *m = add_child(d, name, type);
    if (!m) return -1;
    *out = mn_vnode(m);
    return *out ? 0 : -1;
}

static int mem_unlink(struct vnode *dir, const char *name) {
    struct mnode *d = dir->priv;
    if (d->ro) return -1;
    for (struct mnode **pp = &d->child; *pp; pp = &(*pp)->sibling) {
        struct mnode *c = *pp;
        if (strcmp(c->name, name)) continue;
        if (c->type == FT_DIR && c->child) return -1;      /* only empty directories */
        *pp = c->sibling;
        c->parent = 0;
        mnode_put(c);                                      /* open files keep the data alive */
        return 0;
    }
    return -1;
}

static uint64_t mem_getsize(struct vnode *v) { return ((struct mnode *)v->priv)->size; }
static void mem_release(struct vnode *v) { mnode_put(v->priv); }

static const struct vfs_ops memfs_ops = {
    .read = mem_read, .write = mem_write, .lookup = mem_lookup, .readdir = mem_readdir,
    .create = mem_create, .unlink = mem_unlink, .truncate = mem_truncate,
    .getsize = mem_getsize, .release = mem_release,
};

struct vnode *memfs_new(int readonly) {
    struct mnode *root = mnode_new(FT_DIR, "", readonly);
    if (!root) return 0;
    struct vnode *v = vnode_new(FT_DIR, &memfs_ops, root, 0);   /* the vnode owns the tree's initial reference */
    return v;
}

/* Used by the tar loader: create `path` (and any missing directories) even in a read-only tree. */
int memfs_add(struct vnode *root, const char *path, int type, const void *data, uint64_t size) {
    struct mnode *cur = root->priv;
    char comp[NAME_MAX_LEN];
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        size_t n = 0;
        while (path[n] && path[n] != '/') n++;
        if (n >= NAME_MAX_LEN) return -1;
        memcpy(comp, path, n);
        comp[n] = 0;
        path += n;
        while (*path == '/') path++;
        int last = !*path;
        struct mnode *next = child_named(cur, comp);
        if (!next) {
            next = add_child(cur, comp, last ? type : FT_DIR);
            if (!next) return -1;
        }
        if (last && type == FT_FILE) {
            next->data = (uint8_t *)data;
            next->size = size;
            next->owns = 0;
        }
        cur = next;
    }
    return 0;
}
