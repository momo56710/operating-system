#include "net.h"
#include "vfs.h"
#include "heap.h"
#include "task.h"
#include "string.h"

/* Sockets as files: socket() gives a descriptor that read, write, close, dup2 and fork all understand. */

static const struct vfs_ops sock_ops;

static struct sock *file_sock(struct file *f) {
    return f->vn->type == FT_SOCK ? f->vn->priv : 0;
}

int sock_is_socket(struct file *f) { return f->vn->type == FT_SOCK; }

struct file *sock_create(int type) {
    if (type != SOCK_UDP && type != SOCK_TCP) return 0;
    struct sock *s = kmalloc(sizeof *s);
    if (!s) return 0;
    memset(s, 0, sizeof *s);
    s->type = type;
    struct vnode *v = vnode_new(FT_SOCK, &sock_ops, s, 0);
    if (!v) { kfree(s); return 0; }
    struct file *f = file_from_vnode(v, O_RDWR);
    return f;
}

static uint16_t alloc_port(void) {
    static uint16_t next = 49152;
    for (int tries = 0; tries < 16384; tries++) {
        uint16_t p = next++;
        if (next < 49152) next = 49152;
        if (!udp_port_in_use(p) && !tcp_port_in_use(p)) return p;
    }
    return 0;
}

static int bind_to(struct sock *s, uint16_t port) {
    uint64_t flags;
    if (s->lport) return -1;
    if (!port) port = alloc_port();
    if (!port) return -1;
    flags = spin_lock_irqsave(&net_lock);
    if (s->type == SOCK_UDP ? udp_port_in_use(port) : tcp_port_in_use(port)) { spin_unlock_irqrestore(&net_lock, flags); return -1; }
    s->lport = port;
    if (s->type == SOCK_UDP) udp_bind_locked(s);
    spin_unlock_irqrestore(&net_lock, flags);
    return 0;
}

int sock_bind(struct file *f, uint16_t port) {
    struct sock *s = file_sock(f);
    return s ? bind_to(s, port) : -1;
}

int sock_connect(struct file *f, uint32_t ip, uint16_t port, int timeout_ms) {
    struct sock *s = file_sock(f);
    if (!s || s->connected) return -1;
    if (!s->lport && bind_to(s, 0) < 0) return -1;
    if (s->type == SOCK_UDP) { s->rip = ip; s->rport = port; s->connected = 1; return 0; }

    uint8_t mac[6];
    uint32_t hop = (ip & netif.mask) == (netif.ip & netif.mask) ? ip : netif.gw;
    if (arp_resolve(hop, mac) < 0) return -1;                  /* learn the MAC first: the TCP code must never sleep */
    struct tcb *t = tcp_open(s, ip, port);
    if (!t) return -1;
    if (tcp_wait_established(t, timeout_ms > 0 ? timeout_ms : 5000) < 0) { tcp_close(t); return -1; }
    s->rip = ip; s->rport = port; s->connected = 1;
    return 0;
}

int sock_listen(struct file *f, int backlog) {
    struct sock *s = file_sock(f);
    if (!s || s->type != SOCK_TCP || !s->lport || s->listening) return -1;
    s->backlog = backlog < 1 ? 1 : backlog > 8 ? 8 : backlog;
    tcp_listen(s);
    return 0;
}

struct file *sock_accept(struct file *f, uint32_t *ip, uint16_t *port, int timeout_ms) {
    struct sock *s = file_sock(f);
    if (!s || !s->listening) return 0;
    uint64_t start = uptime_ms();
    for (;;) {
        uint64_t flags = spin_lock_irqsave(&net_lock);
        net_lock_depth++;
        struct sock *ns = 0;
        struct tcb *t = 0;
        if (s->npending) {
            t = s->pending[0];
            memmove(&s->pending[0], &s->pending[1], (s->npending - 1) * sizeof s->pending[0]);
            s->npending--;
            ns = kmalloc(sizeof *ns);
            if (ns) {
                memset(ns, 0, sizeof *ns);
                ns->type = SOCK_TCP; ns->lport = s->lport; ns->connected = 1; ns->tcb = t;
                tcp_peer(t, &ns->rip, &ns->rport);
            }
        }
        net_lock_depth--;
        spin_unlock_irqrestore(&net_lock, flags);
        if (t) {
            if (!ns) { tcp_close(t); return 0; }
            tcp_adopt(t, ns);
            *ip = ns->rip; *port = ns->rport;
            struct vnode *v = vnode_new(FT_SOCK, &sock_ops, ns, 0);
            struct file *nf = v ? file_from_vnode(v, O_RDWR) : 0;
            if (!nf) { tcp_close(t); if (!v) kfree(ns); return 0; }
            return nf;
        }
        if (timeout_ms > 0 && (int)(uptime_ms() - start) >= timeout_ms) return 0;
        sleep_ms(1);
    }
}

static int64_t do_send(struct sock *s, const void *buf, uint64_t len, uint32_t ip, uint16_t port) {
    if (s->type == SOCK_TCP) return s->tcb ? tcp_write(s->tcb, buf, len) : -1;
    if (!ip) { if (!s->connected) return -1; ip = s->rip; port = s->rport; }
    if (!s->lport && bind_to(s, 0) < 0) return -1;
    return udp_send(s->lport, ip, port, buf, (uint32_t)len) == 0 ? (int64_t)len : -1;
}

static int64_t do_recv(struct sock *s, void *buf, uint64_t len, uint32_t *ip, uint16_t *port, int timeout_ms) {
    if (s->type == SOCK_TCP) {
        if (ip) *ip = s->rip;
        if (port) *port = s->rport;
        return s->tcb ? tcp_read(s->tcb, buf, len, timeout_ms) : -1;
    }
    uint64_t start = uptime_ms();
    for (;;) {
        uint64_t flags = spin_lock_irqsave(&net_lock);
        struct dgram *d = s->dq_head;
        if (d) {
            s->dq_head = d->next;
            if (!s->dq_head) s->dq_tail = 0;
            s->dq_len--;
        }
        spin_unlock_irqrestore(&net_lock, flags);
        if (d) {
            uint64_t n = len < d->len ? len : d->len;
            memcpy(buf, d->data, n);
            if (ip) *ip = d->ip;
            if (port) *port = d->port;
            kfree(d);
            return (int64_t)n;
        }
        if (timeout_ms > 0 && (int)(uptime_ms() - start) >= timeout_ms) return -1;
        sleep_ms(1);
    }
}

int64_t sock_sendto(struct file *f, const void *buf, uint64_t len, uint32_t ip, uint16_t port) {
    struct sock *s = file_sock(f);
    return s ? do_send(s, buf, len, ip, port) : -1;
}

int64_t sock_recvfrom(struct file *f, void *buf, uint64_t len, uint32_t *ip, uint16_t *port, int timeout_ms) {
    struct sock *s = file_sock(f);
    return s ? do_recv(s, buf, len, ip, port, timeout_ms) : -1;
}

int sock_shutdown(struct file *f) {
    struct sock *s = file_sock(f);
    if (!s || s->type != SOCK_TCP || !s->tcb) return -1;
    tcp_shutdown_write(s->tcb);
    return 0;
}

/* ---- as a file ---- */

static int64_t sk_read(struct vnode *v, uint64_t off, void *buf, uint64_t n) {
    (void)off;
    return do_recv(v->priv, buf, n, 0, 0, 0);
}

static int64_t sk_write(struct vnode *v, uint64_t off, const void *buf, uint64_t n) {
    (void)off;
    return do_send(v->priv, buf, n, 0, 0);
}

static void sk_close(struct vnode *v, int flags) {              /* the last descriptor was closed */
    (void)flags;
    struct sock *s = v->priv;
    if (s->type == SOCK_UDP) {
        uint64_t f = spin_lock_irqsave(&net_lock);
        udp_unbind_locked(s);
        spin_unlock_irqrestore(&net_lock, f);
        return;
    }
    if (s->listening) {
        tcp_unlisten(s);
        for (;;) {                                               /* refuse connections nobody accepted */
            uint64_t f = spin_lock_irqsave(&net_lock);
            struct tcb *t = s->npending ? s->pending[--s->npending] : 0;
            spin_unlock_irqrestore(&net_lock, f);
            if (!t) break;
            tcp_close(t);
        }
    }
    if (s->tcb) { struct tcb *t = s->tcb; s->tcb = 0; tcp_close(t); }
}

static void sk_release(struct vnode *v) { kfree(v->priv); }

static const struct vfs_ops sock_ops = { .read = sk_read, .write = sk_write, .close = sk_close, .release = sk_release };
