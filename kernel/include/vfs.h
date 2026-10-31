#pragma once
#include <stdint.h>
#include <stddef.h>
#include "abi.h"

struct vnode;

struct vfs_ops {
    int64_t (*read)(struct vnode *v, uint64_t off, void *buf, uint64_t n);          /* bytes read; 0 = end */
    int64_t (*write)(struct vnode *v, uint64_t off, const void *buf, uint64_t n);   /* bytes written or -1 */
    int     (*lookup)(struct vnode *dir, const char *name, struct vnode **out);     /* 0 and *out (referenced), or -1 */
    int     (*readdir)(struct vnode *dir, uint64_t index, struct dirent *out);      /* 1 got one, 0 end */
    int     (*create)(struct vnode *dir, const char *name, int type, struct vnode **out);
    int     (*unlink)(struct vnode *dir, const char *name);
    int     (*truncate)(struct vnode *v, uint64_t size);
    uint64_t (*getsize)(struct vnode *v);                                            /* optional; else vnode->size */
    void    (*release)(struct vnode *v);                                             /* last reference dropped */
    void    (*close)(struct vnode *v, int flags);                                    /* one open file on it closed */
    int64_t (*ioctl)(struct vnode *v, uint64_t req, uint64_t arg);                   /* device control; -1 if unsupported */
};

struct vnode {
    int                   type;      /* FT_* */
    uint64_t              size;
    const struct vfs_ops *ops;
    void                 *priv;
    int                   refs;
};

struct file {
    struct vnode *vn;
    uint64_t      off;
    int           flags;
    int           refs;              /* descriptors sharing this open file (dup, fork) */
};

#define FD_MAX  64
#define CWD_MAX 128

struct vnode *vnode_new(int type, const struct vfs_ops *ops, void *priv, uint64_t size);
void          vnode_get(struct vnode *v);
void          vnode_put(struct vnode *v);

void  vfs_mount(const char *path, struct vnode *root);
int   vfs_normalize(const char *cwd, const char *path, char *out, size_t max);   /* absolute, no . or .. */
struct vnode *vfs_lookup(const char *abs_path);                                   /* referenced, or 0 */
int   vfs_mkdir(const char *abs_path);
int   vfs_unlink(const char *abs_path);
int   vfs_stat(const char *abs_path, struct stat *st);

struct file *file_open(const char *abs_path, int flags);
struct file *file_from_vnode(struct vnode *v, int flags);
struct file *file_dup(struct file *f);
void         file_close(struct file *f);
int64_t      file_read(struct file *f, void *buf, uint64_t n);
int64_t      file_write(struct file *f, const void *buf, uint64_t n);
int64_t      file_seek(struct file *f, int64_t off, int whence);
int          file_readdir(struct file *f, uint64_t index, struct dirent *out);
uint64_t     file_size(struct file *f);

/* in-memory filesystem: the ramdisk (read-only) and /tmp (writable) */
struct vnode *memfs_new(int readonly);
int           memfs_add(struct vnode *root, const char *path, int type, const void *data, uint64_t size);   /* for the tar loader */
void          tar_mount(struct vnode *root, const void *tar, uint64_t size);

struct vnode *devfs_new(void);
struct vnode *fat32_mount(void);       /* the volume on the virtio disk, or 0 */
int           pipe_create(struct file **rd, struct file **wr);
int64_t       file_ioctl(struct file *f, uint64_t req, uint64_t arg);
void          console_set_cooked(void);   /* leave raw mode (called when a process exits) */
