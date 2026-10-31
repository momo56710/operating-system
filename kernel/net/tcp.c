#include "net.h"
#include "task.h"
#include "heap.h"
#include "string.h"
#include "kprintf.h"

/* TCP: connections with the three-way handshake, sequence numbers, acknowledgements, a send and
 * a receive buffer, retransmission on timeout, a receive window, and orderly close.
 *
 * Deliberately simple: out-of-order segments are dropped (the sender retransmits them), there is
 * no congestion control, no zero-window probing, no selective acknowledgements. It is correct on
 * a clean network and recovers from loss by timeout.
 *
 * Everything here runs with net_lock held. Nothing sleeps while holding it. */

#define MSS 1460
#define SBUF 16384
#define RBUF 16384
#define RTO_INIT 400
#define RTO_MAX 4000
#define MAX_RETRIES 8
#define MAX_TCB 32
#define MAX_LISTENERS 8
#define TIME_WAIT_MS 2000

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

struct tcp_hdr {
    uint16_t sport, dport;
    uint32_t seq, ack;
    uint8_t  off, flags;
    uint16_t win, checksum, urg;
} __attribute__((packed));

enum { T_FREE, T_CLOSED, T_SYN_SENT, T_SYN_RCVD, T_ESTABLISHED, T_FIN_WAIT_1, T_FIN_WAIT_2,
       T_CLOSE_WAIT, T_CLOSING, T_LAST_ACK, T_TIME_WAIT };

struct tcb {
    int       state;
    struct sock *sock;                 /* the socket using it; 0 once the app closed it (then it cleans itself up) */
    struct sock *listener;             /* while in SYN_RCVD: who will accept it */
    uint32_t  lip, rip;
    uint16_t  lport, rport;
    uint32_t  iss, snd_una, snd_nxt, rcv_nxt, snd_wnd;
    uint8_t  *sbuf;  uint32_t slen, sent;      /* bytes waiting to be acknowledged or sent; of those, already sent */
    uint8_t  *rbuf;  uint32_t rhead, rlen;     /* received, not yet read */
    int       syn_unacked, fin_queued, fin_sent, fin_acked, rcv_fin, err;
    uint64_t  rto_at;  uint32_t rto;  int retries;
    uint64_t  tw_until;
};

static struct tcb tcbs[MAX_TCB];
static struct sock *listeners[MAX_LISTENERS];
static uint32_t isn_counter;

#define LOCK()   do { flags = spin_lock_irqsave(&net_lock); net_lock_depth++; } while (0)
#define UNLOCK() do { net_lock_depth--; spin_unlock_irqrestore(&net_lock, flags); } while (0)

/* ---- helpers ---- */

static void free_tcb(struct tcb *t) {
    if (t->sock && t->sock->tcb == t) t->sock->tcb = 0;
    if (t->sbuf) kfree(t->sbuf);
    if (t->rbuf) kfree(t->rbuf);
    memset(t, 0, sizeof *t);
}

static struct tcb *alloc_tcb(void) {
    for (int i = 0; i < MAX_TCB; i++) {
        struct tcb *t = &tcbs[i];
        if (t->state != T_FREE) continue;
        memset(t, 0, sizeof *t);
        t->sbuf = kmalloc(SBUF);
        t->rbuf = kmalloc(RBUF);
        if (!t->sbuf || !t->rbuf) { if (t->sbuf) kfree(t->sbuf); if (t->rbuf) kfree(t->rbuf); memset(t, 0, sizeof *t); return 0; }
        t->state = T_CLOSED;
        t->rto = RTO_INIT;
        return t;
    }
    return 0;
}

/* Send one segment of this connection. */
static void xmit(struct tcb *t, uint8_t flags, uint32_t seq, const void *data, uint32_t len) {
    uint8_t pkt[24 + MSS];
    struct tcp_hdr *h = (struct tcp_hdr *)pkt;
    uint32_t hl = 20;
    h->sport = htons(t->lport); h->dport = htons(t->rport);
    h->seq = htonl(seq);
    h->ack = (flags & F_ACK) ? htonl(t->rcv_nxt) : 0;
    if (flags & F_SYN) { pkt[20] = 2; pkt[21] = 4; pkt[22] = MSS >> 8; pkt[23] = MSS & 255; hl = 24; }   /* MSS option */
    h->off = (uint8_t)((hl / 4) << 4);
    h->flags = flags;
    uint32_t window = RBUF - t->rlen;
    h->win = htons(window > 65535 ? 65535 : window);
    h->checksum = 0; h->urg = 0;
    if (len) memcpy(pkt + hl, data, len);
    h->checksum = htons(inet_checksum(pkt, hl + len, pseudo_sum(t->lip, t->rip, IPPROTO_TCP, hl + len)));
    ip_send(t->rip, IPPROTO_TCP, pkt, hl + len);
}

/* Answer a segment that belongs to no connection with a reset, so the other side stops. */
static void send_rst(uint32_t src, uint32_t dst, const struct tcp_hdr *in, uint32_t in_seq, uint32_t in_ack, uint32_t seg_len) {
    uint8_t pkt[20];
    struct tcp_hdr *h = (struct tcp_hdr *)pkt;
    h->sport = in->dport; h->dport = in->sport;
    if (in->flags & F_ACK) { h->seq = htonl(in_ack); h->ack = 0; h->flags = F_RST; }
    else { h->seq = 0; h->ack = htonl(in_seq + seg_len + ((in->flags & F_SYN) ? 1 : 0)); h->flags = F_RST | F_ACK; }
    h->off = 5 << 4; h->win = 0; h->checksum = 0; h->urg = 0;
    h->checksum = htons(inet_checksum(pkt, 20, pseudo_sum(dst, src, IPPROTO_TCP, 20)));
    ip_send(src, IPPROTO_TCP, pkt, 20);
}

/* Push out as much buffered data as the peer's window allows, then the FIN if the app has closed. */
static void output(struct tcb *t) {
    if (t->state != T_ESTABLISHED && t->state != T_CLOSE_WAIT && t->state != T_FIN_WAIT_1 &&
        t->state != T_LAST_ACK && t->state != T_CLOSING) return;
    for (;;) {
        uint32_t unsent = t->slen - t->sent;
        uint32_t wnd = t->snd_wnd < 6 * MSS ? t->snd_wnd : 6 * MSS;      /* a modest fixed cap instead of congestion control */
        uint32_t room = wnd > t->sent ? wnd - t->sent : 0;
        uint32_t n = unsent < MSS ? unsent : MSS;
        if (n > room) n = room;
        if (!n) break;
        xmit(t, F_ACK | F_PSH, t->snd_nxt, t->sbuf + t->sent, n);
        t->snd_nxt += n;
        t->sent += n;
        if (!t->rto_at) t->rto_at = uptime_ms() + t->rto;
    }
    if (t->fin_queued && !t->fin_sent && t->sent == t->slen) {
        xmit(t, F_FIN | F_ACK, t->snd_nxt, 0, 0);
        t->snd_nxt++;
        t->fin_sent = 1;
        if (!t->rto_at) t->rto_at = uptime_ms() + t->rto;
    }
}

static void process_ack(struct tcb *t, uint32_t ack, uint32_t win) {
    if ((int32_t)(ack - t->snd_una) > 0 && (int32_t)(ack - t->snd_nxt) <= 0) {
        uint32_t acked = ack - t->snd_una;
        uint32_t data = acked < t->sent ? acked : t->sent;
        if (t->fin_sent && acked > data) t->fin_acked = 1;               /* the FIN follows all the data */
        if (data) {
            memmove(t->sbuf, t->sbuf + data, t->slen - data);
            t->slen -= data;
            t->sent -= data;
        }
        t->snd_una = ack;
        t->retries = 0;
        t->rto = RTO_INIT;
        t->rto_at = (t->snd_una == t->snd_nxt) ? 0 : uptime_ms() + t->rto;
    }
    t->snd_wnd = win;
}

static void to_closed(struct tcb *t, int err) {
    t->state = T_CLOSED;
    t->err = err;
    t->rto_at = 0;
    if (!t->sock) free_tcb(t);                                           /* nobody is looking at it any more */
}

/* ---- segment arrival for an existing connection ---- */

static void segment(struct tcb *t, uint8_t flags, uint32_t seq, uint32_t ack, uint32_t win, const uint8_t *data, uint32_t dlen) {
    uint64_t now = uptime_ms();

    if (t->state == T_SYN_SENT) {
        if ((flags & F_ACK) && ack != t->iss + 1) { if (!(flags & F_RST)) { struct tcp_hdr in = { htons(t->rport), htons(t->lport), 0, 0, 0, flags, 0, 0, 0 }; send_rst(t->rip, t->lip, &in, seq, ack, 0); } return; }
        if (flags & F_RST) { if (flags & F_ACK) to_closed(t, 1); return; }               /* connection refused */
        if ((flags & F_SYN) && (flags & F_ACK)) {
            t->rcv_nxt = seq + 1;
            t->snd_una = ack;
            t->syn_unacked = 0;
            t->snd_wnd = win;
            t->state = T_ESTABLISHED;
            t->rto_at = 0; t->retries = 0; t->rto = RTO_INIT;
            xmit(t, F_ACK, t->snd_nxt, 0, 0);
        }
        return;
    }

    if (t->state == T_SYN_RCVD) {
        if (flags & F_RST) { free_tcb(t); return; }
        if (!(flags & F_ACK) || ack != t->iss + 1) return;
        struct sock *l = t->listener;
        if (!l || !l->listening || l->npending >= 8) {                    /* nobody to accept it */
            struct tcp_hdr in = { htons(t->rport), htons(t->lport), 0, 0, 0, flags, 0, 0, 0 };
            send_rst(t->rip, t->lip, &in, seq, ack, 0);
            free_tcb(t);
            return;
        }
        t->snd_una = ack;
        t->syn_unacked = 0;
        t->snd_wnd = win;
        t->state = T_ESTABLISHED;
        t->rto_at = 0; t->retries = 0; t->rto = RTO_INIT;
        l->pending[l->npending++] = t;
        t->listener = 0;
        t->sock = l;                                                      /* owned by the listener until accept() */
        /* fall through: this segment may carry data as well */
    }

    if (t->state == T_TIME_WAIT) {
        if (flags & F_FIN) { xmit(t, F_ACK, t->snd_nxt, 0, 0); t->tw_until = now + TIME_WAIT_MS; }
        return;
    }
    if (t->state == T_CLOSED) return;

    if (flags & F_RST) { if (seq == t->rcv_nxt) to_closed(t, 1); return; }
    if (flags & F_SYN) return;
    if (!(flags & F_ACK)) return;

    process_ack(t, ack, win);
    if (t->fin_acked) {                                                    /* our FIN was acknowledged */
        if (t->state == T_FIN_WAIT_1) t->state = T_FIN_WAIT_2;
        else if (t->state == T_CLOSING) { t->state = T_TIME_WAIT; t->tw_until = now + TIME_WAIT_MS; }
        else if (t->state == T_LAST_ACK) { to_closed(t, 0); if (t->state != T_FREE && !t->sock) free_tcb(t); return; }
    }

    int need_ack = 0, took_all = 1;
    uint32_t orig_seq = seq, orig_len = dlen;
    if (dlen && (t->state == T_ESTABLISHED || t->state == T_FIN_WAIT_1 || t->state == T_FIN_WAIT_2)) {
        if ((int32_t)(seq - t->rcv_nxt) < 0) {                             /* the start is data we already have */
            uint32_t skip = t->rcv_nxt - seq;
            if (skip >= dlen) { need_ack = 1; dlen = 0; }
            else { data += skip; seq += skip; dlen -= skip; }
        }
        if (dlen && seq == t->rcv_nxt) {
            uint32_t room = RBUF - t->rlen, n = dlen < room ? dlen : room;
            if (n) {
                uint32_t tail = (t->rhead + t->rlen) % RBUF, first = RBUF - tail < n ? RBUF - tail : n;
                memcpy(t->rbuf + tail, data, first);
                if (n > first) memcpy(t->rbuf, data + first, n - first);
                t->rlen += n;
                t->rcv_nxt += n;
            }
            took_all = n == dlen;
            need_ack = 1;
        } else if (dlen) {
            need_ack = 1;                                                  /* a gap: a duplicate ACK makes the sender resend */
            took_all = 0;
        }
    } else if (dlen) {
        need_ack = 1;
    }

    if ((flags & F_FIN) && took_all && !t->rcv_fin && orig_seq + orig_len == t->rcv_nxt) {
        t->rcv_fin = 1;
        t->rcv_nxt++;
        need_ack = 1;
        if (t->state == T_ESTABLISHED) t->state = T_CLOSE_WAIT;
        else if (t->state == T_FIN_WAIT_1) t->state = t->fin_acked ? T_TIME_WAIT : T_CLOSING;
        else if (t->state == T_FIN_WAIT_2) t->state = T_TIME_WAIT;
        if (t->state == T_TIME_WAIT) t->tw_until = now + TIME_WAIT_MS;
    }
    if (need_ack) xmit(t, F_ACK, t->snd_nxt, 0, 0);
    output(t);
}

/* ---- from the IP layer ---- */

static struct sock *find_listener(uint16_t port) {
    for (int i = 0; i < MAX_LISTENERS; i++)
        if (listeners[i] && listeners[i]->listening && listeners[i]->lport == port) return listeners[i];
    return 0;
}

void tcp_input(uint32_t src, uint32_t dst, const uint8_t *pkt, uint32_t len) {
    if (len < sizeof(struct tcp_hdr)) return;
    const struct tcp_hdr *h = (const struct tcp_hdr *)pkt;
    uint32_t hl = (h->off >> 4) * 4;
    if (hl < 20 || hl > len) return;
    if (inet_checksum(pkt, len, pseudo_sum(src, dst, IPPROTO_TCP, len)) != 0) return;
    uint16_t sport = ntohs(h->sport), dport = ntohs(h->dport);
    uint32_t seq = ntohl(h->seq), ack = ntohl(h->ack), dlen = len - hl;
    uint64_t flags;

    LOCK();
    struct tcb *t = 0;
    for (int i = 0; i < MAX_TCB; i++) {
        struct tcb *c = &tcbs[i];
        if (c->state > T_CLOSED && c->lip == dst && c->lport == dport && c->rip == src && c->rport == sport) { t = c; break; }
    }
    if (t) {
        segment(t, h->flags, seq, ack, ntohs(h->win), pkt + hl, dlen);
    } else if ((h->flags & F_SYN) && !(h->flags & F_ACK)) {
        struct sock *l = find_listener(dport);
        int backlog_used = 0;
        for (int i = 0; l && i < MAX_TCB; i++) if (tcbs[i].state == T_SYN_RCVD && tcbs[i].listener == l) backlog_used++;
        struct tcb *c = (l && l->npending + backlog_used < l->backlog) ? alloc_tcb() : 0;
        if (c) {
            c->lip = dst; c->rip = src; c->lport = dport; c->rport = sport;
            c->rcv_nxt = seq + 1;
            c->iss = (uint32_t)uptime_ms() * 250000u + (isn_counter += 64000);
            c->snd_una = c->iss; c->snd_nxt = c->iss + 1; c->syn_unacked = 1;
            c->snd_wnd = ntohs(h->win);
            c->listener = l;
            c->state = T_SYN_RCVD;
            c->rto_at = uptime_ms() + c->rto;
            xmit(c, F_SYN | F_ACK, c->iss, 0, 0);
        } else {
            send_rst(src, dst, h, seq, ack, dlen);
        }
    } else if (!(h->flags & F_RST)) {
        send_rst(src, dst, h, seq, ack, dlen);
    }
    UNLOCK();
}

/* ---- timers ---- */

void tcp_tick(void) {
    uint64_t flags;
    uint64_t now = uptime_ms();
    LOCK();
    for (int i = 0; i < MAX_TCB; i++) {
        struct tcb *t = &tcbs[i];
        if (t->state == T_FREE) continue;
        if (t->state == T_TIME_WAIT && now >= t->tw_until) { if (t->sock) t->state = T_CLOSED; else free_tcb(t); continue; }
        if (t->rto_at && now >= t->rto_at) {
            if (++t->retries > MAX_RETRIES) { to_closed(t, 1); continue; }  /* give up: the peer is gone */
            t->rto = t->rto * 2 > RTO_MAX ? RTO_MAX : t->rto * 2;
            t->rto_at = now + t->rto;
            if (t->state == T_SYN_SENT) xmit(t, F_SYN, t->iss, 0, 0);
            else if (t->state == T_SYN_RCVD) xmit(t, F_SYN | F_ACK, t->iss, 0, 0);
            else if (t->sent) {                                             /* resend everything unacknowledged */
                for (uint32_t off = 0; off < t->sent; off += MSS)
                    xmit(t, F_ACK | F_PSH, t->snd_una + off, t->sbuf + off, t->sent - off < MSS ? t->sent - off : MSS);
            }
            else if (t->fin_sent && !t->fin_acked) xmit(t, F_FIN | F_ACK, t->snd_nxt - 1, 0, 0);
        } else if (t->slen > t->sent || (t->fin_queued && !t->fin_sent)) {
            output(t);                                                      /* the window may have opened */
        }
    }
    UNLOCK();
}

/* ---- what sockets use ---- */

struct tcb *tcp_open(struct sock *s, uint32_t ip, uint16_t port) {
    uint64_t flags;
    LOCK();
    struct tcb *t = alloc_tcb();
    if (t) {
        t->sock = s;
        s->tcb = t;
        t->lip = netif.ip; t->rip = ip; t->lport = s->lport; t->rport = port;
        t->iss = (uint32_t)uptime_ms() * 250000u + (isn_counter += 64000);
        t->snd_una = t->iss; t->snd_nxt = t->iss + 1; t->syn_unacked = 1;
        t->state = T_SYN_SENT;
        t->rto_at = uptime_ms() + t->rto;
        xmit(t, F_SYN, t->iss, 0, 0);
    }
    UNLOCK();
    return t;
}

int tcp_wait_established(struct tcb *t, int timeout_ms) {
    uint64_t flags, start = uptime_ms();
    for (;;) {
        LOCK();
        int st = t->state;
        UNLOCK();
        if (st == T_ESTABLISHED || st == T_CLOSE_WAIT) return 0;
        if (st == T_CLOSED || st == T_FREE) return -1;
        if ((int)(uptime_ms() - start) >= timeout_ms) return -1;
        sleep_ms(1);
    }
}

int tcp_usable(struct tcb *t) {
    return t && (t->state == T_ESTABLISHED || t->state == T_CLOSE_WAIT);
}

void tcp_peer(struct tcb *t, uint32_t *ip, uint16_t *port) { *ip = t->rip; *port = t->rport; }

int64_t tcp_read(struct tcb *t, void *buf, uint64_t n, int timeout_ms) {
    uint64_t flags, start = uptime_ms();
    if (!n) return 0;
    for (;;) {
        LOCK();
        if (t->rlen) {
            uint64_t got = n < t->rlen ? n : t->rlen, first = RBUF - t->rhead < got ? RBUF - t->rhead : got;
            uint32_t was_free = RBUF - t->rlen;
            memcpy(buf, t->rbuf + t->rhead, first);
            if (got > first) memcpy((uint8_t *)buf + first, t->rbuf, got - first);
            t->rhead = (t->rhead + got) % RBUF;
            t->rlen -= got;
            if (was_free < MSS && RBUF - t->rlen >= MSS && tcp_usable(t)) xmit(t, F_ACK, t->snd_nxt, 0, 0);   /* the window reopened */
            UNLOCK();
            return (int64_t)got;
        }
        int eof = t->rcv_fin || t->state == T_CLOSED || t->state == T_FREE || t->state == T_TIME_WAIT;
        int err = t->err;
        UNLOCK();
        if (eof) return err && !t->rcv_fin ? -1 : 0;
        if (timeout_ms > 0 && (int)(uptime_ms() - start) >= timeout_ms) return -1;
        sleep_ms(1);
    }
}

int64_t tcp_write(struct tcb *t, const void *buf, uint64_t n) {
    uint64_t flags, done = 0;
    while (done < n) {
        LOCK();
        if (t->state != T_ESTABLISHED && t->state != T_CLOSE_WAIT) { UNLOCK(); return done ? (int64_t)done : -1; }
        uint32_t space = SBUF - t->slen;
        if (space) {
            uint32_t c = n - done < space ? (uint32_t)(n - done) : space;
            memcpy(t->sbuf + t->slen, (const uint8_t *)buf + done, c);
            t->slen += c;
            done += c;
            output(t);
            UNLOCK();
        } else {
            UNLOCK();
            sleep_ms(1);                                                    /* wait for acknowledgements to free space */
        }
    }
    return (int64_t)done;
}

void tcp_close(struct tcb *t) {
    uint64_t flags;
    LOCK();
    t->sock = 0;                                                            /* from now on the stack owns it */
    switch (t->state) {
    case T_ESTABLISHED: case T_SYN_RCVD: t->fin_queued = 1; t->state = T_FIN_WAIT_1; output(t); break;
    case T_CLOSE_WAIT:                  t->fin_queued = 1; t->state = T_LAST_ACK;   output(t); break;
    case T_FIN_WAIT_1: case T_FIN_WAIT_2: case T_CLOSING: case T_LAST_ACK: case T_TIME_WAIT: break;   /* already closing */
    default: free_tcb(t);
    }
    UNLOCK();
}

void tcp_listen(struct sock *s) {
    uint64_t flags;
    LOCK();
    for (int i = 0; i < MAX_LISTENERS; i++) if (!listeners[i]) { listeners[i] = s; break; }
    s->listening = 1;
    UNLOCK();
}

void tcp_unlisten(struct sock *s) {
    uint64_t flags;
    LOCK();
    for (int i = 0; i < MAX_LISTENERS; i++) if (listeners[i] == s) listeners[i] = 0;
    s->listening = 0;
    for (int i = 0; i < MAX_TCB; i++) if (tcbs[i].listener == s) { tcbs[i].listener = 0; }
    UNLOCK();
}

/* A connection taken from a listener now belongs to its own socket. */
void tcp_adopt(struct tcb *t, struct sock *s) {
    uint64_t flags;
    LOCK();
    t->sock = s;
    s->tcb = t;
    UNLOCK();
}

/* The app will send no more, but keeps reading (shutdown). */
void tcp_shutdown_write(struct tcb *t) {
    uint64_t flags;
    LOCK();
    if (t->state == T_ESTABLISHED)      { t->fin_queued = 1; t->state = T_FIN_WAIT_1; output(t); }
    else if (t->state == T_CLOSE_WAIT)  { t->fin_queued = 1; t->state = T_LAST_ACK;   output(t); }
    UNLOCK();
}

int tcp_port_in_use(uint16_t port) {
    for (int i = 0; i < MAX_LISTENERS; i++) if (listeners[i] && listeners[i]->lport == port) return 1;
    for (int i = 0; i < MAX_TCB; i++) if (tcbs[i].state > T_CLOSED && tcbs[i].lport == port) return 1;
    return 0;
}
