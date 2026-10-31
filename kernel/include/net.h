#pragma once
#include <stdint.h>
#include <stddef.h>
#include "abi.h"

/* IPv4 addresses are kept in host byte order (10.0.2.15 is 0x0A00020F) and converted at the wire. */
#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
static inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }

struct netif {
    uint8_t  mac[6];
    uint32_t ip, mask, gw, dns;
    int      up;
    uint64_t rx_frames, tx_frames, rx_bytes, tx_bytes, rx_dropped;
};
extern struct netif netif;

/* driver */
int  virtio_net_init(uint8_t mac[6]);                                 /* 0 ok, -1 no network card */
int  virtio_net_send(const void *frame, uint32_t len);                /* 0 ok */
int  virtio_net_poll(void (*cb)(const uint8_t *frame, uint32_t len)); /* frames handed to cb; returns how many */

/* stack */
#define ETH_IP  0x0800
#define ETH_ARP 0x0806
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

void     net_init(void);                                               /* bring the network up, if there is a card */
int      net_in_rx_thread(void);

void     eth_send(const uint8_t dst[6], uint16_t type, const void *payload, uint32_t len);
void     arp_input(const uint8_t *pkt, uint32_t len);
void     arp_request(uint32_t ip);                                     /* send a request, do not wait */
int      arp_lookup(uint32_t ip, uint8_t mac[6]);                      /* cache only; 0 if known */
int      arp_resolve(uint32_t ip, uint8_t mac[6]);                     /* may ask and wait; not from the receive thread */
void     arp_learn(uint32_t ip, const uint8_t mac[6]);

int      ip_send(uint32_t dst, uint8_t proto, const void *payload, uint32_t len);   /* 0 ok */
void     ip_input(const uint8_t *pkt, uint32_t len, const uint8_t *src_mac);
uint16_t inet_checksum(const void *data, uint32_t len, uint32_t initial);
uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t proto, uint16_t len);

void     icmp_input(uint32_t src, const uint8_t *pkt, uint32_t len);
int      icmp_ping(uint32_t dst, uint16_t seq, int timeout_ms);        /* round trip in ms, or -1 */

void     udp_input(uint32_t src, uint32_t dst, const uint8_t *pkt, uint32_t len);
void     tcp_input(uint32_t src, uint32_t dst, const uint8_t *pkt, uint32_t len);
void     tcp_tick(void);                                               /* timers: retransmission, TIME_WAIT */

#include "spinlock.h"
extern spinlock_t net_lock;            /* protects sockets and TCP state; never sleep while holding it */
extern int net_lock_depth;

struct dgram { struct dgram *next; uint32_t ip; uint16_t port, len; uint8_t data[]; };
struct tcb;

struct sock {
    int      type;                     /* SOCK_UDP or SOCK_TCP */
    uint16_t lport;
    uint32_t rip;                      /* connected peer, if any */
    uint16_t rport;
    int      connected;
    struct dgram *dq_head, *dq_tail;   /* UDP: received datagrams */
    int      dq_len;
    struct tcb *tcb;                   /* TCP: the connection */
    int      listening, backlog;
    struct tcb *pending[8];            /* TCP listener: established connections waiting for accept() */
    int      npending;
    struct sock *next;                 /* chain of bound UDP sockets */
};

/* TCP engine (tcp.c) */
struct tcb *tcp_open(struct sock *s, uint32_t ip, uint16_t port);          /* send SYN; 0 if no resources */
int         tcp_wait_established(struct tcb *t, int timeout_ms);          /* 0 ok, -1 refused/timed out */
int64_t     tcp_read(struct tcb *t, void *buf, uint64_t n, int timeout_ms); /* bytes, 0 = end of stream, -1 error/timeout */
int64_t     tcp_write(struct tcb *t, const void *buf, uint64_t n);
void        tcp_close(struct tcb *t);                                      /* app is done with the connection */
void        tcp_shutdown_write(struct tcb *t);
void        tcp_adopt(struct tcb *t, struct sock *s);
void        tcp_listen(struct sock *s);
void        tcp_unlisten(struct sock *s);
void        tcp_peer(struct tcb *t, uint32_t *ip, uint16_t *port);
int         tcp_usable(struct tcb *t);                                     /* still connected */
int         tcp_port_in_use(uint16_t port);
int         udp_port_in_use(uint16_t port);

/* sockets (VFS glue lives in sock.c) */
struct file;
struct file *sock_create(int type);
int          sock_bind(struct file *f, uint16_t port);
int          sock_connect(struct file *f, uint32_t ip, uint16_t port, int timeout_ms);
int          sock_listen(struct file *f, int backlog);
struct file *sock_accept(struct file *f, uint32_t *ip, uint16_t *port, int timeout_ms);
int64_t      sock_sendto(struct file *f, const void *buf, uint64_t len, uint32_t ip, uint16_t port);
int64_t      sock_recvfrom(struct file *f, void *buf, uint64_t len, uint32_t *ip, uint16_t *port, int timeout_ms);
int          sock_is_socket(struct file *f);
int          sock_shutdown(struct file *f);

void udp_bind_locked(struct sock *s);
void udp_unbind_locked(struct sock *s);
int  udp_send(uint16_t sport, uint32_t dst, uint16_t dport, const void *data, uint32_t len);
