#include "vfs.h"
#include "sync.h"
#include "heap.h"
#include "string.h"
#include "abi.h"

#define PIPE_BUF_SIZE 4096

struct pipe {
    spinlock_t   lock;
    uint8_t      buf[PIPE_BUF_SIZE];
    uint32_t     head, count;
    int          readers, writers;
    struct waitq rq, wq;               /* tasks waiting for data / for space */
};

static const struct vfs_ops pipe_ops;

static int64_t pipe_read(struct vnode *v, uint64_t off, void *dst, uint64_t n) {
    (void)off;
    struct pipe *p = v->priv;
    if (!n) return 0;
    for (;;) {
        uint64_t f = spin_lock_irqsave(&p->lock);
        if (p->count) {
            uint64_t got = n < p->count ? n : p->count;
            for (uint64_t i = 0; i < got; i++)
                ((uint8_t *)dst)[i] = p->buf[(p->head + i) % PIPE_BUF_SIZE];
            p->head = (p->head + got) % PIPE_BUF_SIZE;
            p->count -= got;
            waitq_wake_all(&p->wq);
            spin_unlock_irqrestore(&p->lock, f);
            return (int64_t)got;
        }
        if (!p->writers) { spin_unlock_irqrestore(&p->lock, f); return 0; }    /* every writer closed: end of file */
        waitq_block(&p->rq, &p->lock, f);
    }
}

static int64_t pipe_write(struct vnode *v, uint64_t off, const void *src, uint64_t n) {
    (void)off;
    struct pipe *p = v->priv;
    uint64_t done = 0;
    while (done < n) {
        uint64_t f = spin_lock_irqsave(&p->lock);
        if (!p->readers) { spin_unlock_irqrestore(&p->lock, f); return done ? (int64_t)done : -1; }   /* nobody will read */
        if (p->count < PIPE_BUF_SIZE) {
            uint64_t room = PIPE_BUF_SIZE - p->count, chunk = n - done < room ? n - done : room;
            for (uint64_t i = 0; i < chunk; i++)
                p->buf[(p->head + p->count + i) % PIPE_BUF_SIZE] = ((const uint8_t *)src)[done + i];
            p->count += chunk;
            done += chunk;
            waitq_wake_all(&p->rq);
            spin_unlock_irqrestore(&p->lock, f);
            continue;
        }
        waitq_block(&p->wq, &p->lock, f);
    }
    return (int64_t)done;
}

static void pipe_close(struct vnode *v, int flags) {      /* one end was closed */
    struct pipe *p = v->priv;
    uint64_t f = spin_lock_irqsave(&p->lock);
    if ((flags & 3) == O_WRONLY) p->writers--; else p->readers--;
    waitq_wake_all(&p->rq);                               /* readers must notice EOF, writers a broken pipe */
    waitq_wake_all(&p->wq);
    spin_unlock_irqrestore(&p->lock, f);
}

static void pipe_release(struct vnode *v) { kfree(v->priv); }

static int64_t pipe_ioctl(struct vnode *v, uint64_t req, uint64_t arg) {
    (void)arg;
    if (req != PIPE_AVAIL) return -1;
    struct pipe *p = v->priv;
    uint64_t f = spin_lock_irqsave(&p->lock);
    int64_t r = p->count ? (int64_t)p->count : (p->writers ? 0 : -2);
    spin_unlock_irqrestore(&p->lock, f);
    return r;
}

static const struct vfs_ops pipe_ops = { .read = pipe_read, .write = pipe_write, .close = pipe_close, .release = pipe_release, .ioctl = pipe_ioctl };

int pipe_create(struct file **rd, struct file **wr) {
    struct pipe *p = kmalloc(sizeof *p);
    if (!p) return -1;
    memset(p, 0, sizeof *p);
    p->readers = p->writers = 1;
    struct vnode *v = vnode_new(FT_PIPE, &pipe_ops, p, 0);
    if (!v) { kfree(p); return -1; }
    vnode_get(v);                                         /* one reference per end */
    *rd = file_from_vnode(v, O_RDONLY);
    *wr = file_from_vnode(v, O_WRONLY);
    return (*rd && *wr) ? 0 : -1;
}
