#include "net.h"
#include "task.h"
#include "string.h"
#include "spinlock.h"

/* ICMP: answers pings, and sends them for the ping program. */

struct icmp_echo { uint8_t type, code; uint16_t checksum, id, seq; } __attribute__((packed));

#define PING_ID 0x4C45
static volatile int waiting_seq = -1;      /* the sequence number a ping() call is waiting for */
static volatile int got_reply;
static volatile uint64_t reply_at;

void icmp_input(uint32_t src, const uint8_t *pkt, uint32_t len) {
    if (len < sizeof(struct icmp_echo) || inet_checksum(pkt, len, 0) != 0) return;
    const struct icmp_echo *e = (const struct icmp_echo *)pkt;
    if (e->type == 8) {                                          /* echo request: answer it */
        uint8_t reply[1480];
        if (len > sizeof reply) return;
        memcpy(reply, pkt, len);
        struct icmp_echo *r = (struct icmp_echo *)reply;
        r->type = 0;
        r->checksum = 0;
        r->checksum = htons(inet_checksum(reply, len, 0));
        ip_send(src, IPPROTO_ICMP, reply, len);
    } else if (e->type == 0 && ntohs(e->id) == PING_ID && (int)ntohs(e->seq) == waiting_seq) {
        reply_at = uptime_ms();
        got_reply = 1;
    }
}

int icmp_ping(uint32_t dst, uint16_t seq, int timeout_ms) {
    uint8_t pkt[8 + 32];
    struct icmp_echo *e = (struct icmp_echo *)pkt;
    e->type = 8; e->code = 0; e->checksum = 0;
    e->id = htons(PING_ID); e->seq = htons(seq);
    for (int i = 0; i < 32; i++) pkt[8 + i] = 'a' + i % 26;
    e->checksum = htons(inet_checksum(pkt, sizeof pkt, 0));

    got_reply = 0;
    waiting_seq = seq;
    uint64_t start = uptime_ms();
    if (ip_send(dst, IPPROTO_ICMP, pkt, sizeof pkt) < 0) { waiting_seq = -1; return -1; }
    while (!got_reply && (int)(uptime_ms() - start) < timeout_ms) sleep_ms(1);
    waiting_seq = -1;
    return got_reply ? (int)(reply_at - start) : -1;
}
