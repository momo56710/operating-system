#include "net.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

/* Intel 8254x "e1000" Gigabit Ethernet: the card VirtualBox ("Intel PRO/1000 MT Desktop"), VMware and
 * QEMU all offer. Registers are memory mapped; frames move through two rings of descriptors in RAM.
 * Polled, no interrupts. */

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define RX_COUNT 32
#define TX_COUNT 8
#define BUF_SIZE 2048

struct rx_desc { uint64_t addr; uint16_t length, checksum; uint8_t status, errors; uint16_t special; } __attribute__((packed));
struct tx_desc { uint64_t addr; uint16_t length; uint8_t cso, cmd, status, css; uint16_t special; } __attribute__((packed));

static volatile uint32_t *regs;
static volatile struct rx_desc *rx;
static volatile struct tx_desc *tx;
static uint8_t *rx_bufs, *tx_bufs;
static uint64_t rx_bufs_phys, tx_bufs_phys;
static unsigned rx_cur, tx_cur;
static spinlock_t tx_lock;
static int ready;

static uint32_t rd(uint32_t off) { return regs[off / 4]; }
static void wr(uint32_t off, uint32_t v) { regs[off / 4] = v; }

static const uint16_t known_devices[] = {          /* the 8254x family and close relatives with the same legacy descriptors */
    0x1000, 0x1001, 0x1004, 0x1008, 0x100C, 0x100D, 0x100E, 0x100F, 0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016,
    0x1017, 0x1018, 0x1019, 0x101A, 0x101D, 0x1026, 0x1027, 0x1028, 0x1075, 0x1076, 0x1077, 0x1078, 0x1079, 0x107A,
    0x107B, 0x107C, 0x10B9 };

static int eeprom_word(int i) {
    wr(REG_EERD, 1 | ((uint32_t)i << 8));
    for (int n = 0; n < 100000; n++) {
        uint32_t v = rd(REG_EERD);
        if (v & (1 << 4)) return (int)(v >> 16);
    }
    return -1;
}

int e1000_init(uint8_t mac[6]) {
    struct pci_dev d;
    int found = 0;
    for (unsigned i = 0; i < sizeof known_devices / sizeof *known_devices && !found; i++)
        if (pci_find(0x8086, known_devices[i], &d) == 0) found = 1;
    if (!found) return -1;

    pci_enable(&d, 1, 0);
    uint32_t bar0 = pci_read32(d.bus, d.slot, d.func, 0x10);
    if (bar0 & 1) { kprintf("e1000: BAR0 is not memory\n"); return -1; }
    regs = vmm_map_mmio(bar0 & ~0xFULL, 0x20000);

    wr(REG_IMC, 0xFFFFFFFF);                                   /* no interrupts */
    wr(REG_CTRL, rd(REG_CTRL) | (1u << 26));                   /* reset */
    for (int i = 0; i < 100000 && (rd(REG_CTRL) & (1u << 26)); i++) ;
    wr(REG_IMC, 0xFFFFFFFF);
    rd(REG_ICR);
    wr(REG_CTRL, (rd(REG_CTRL) | (1u << 6) | (1u << 5)) & ~((1u << 3) | (1u << 31) | (1u << 30)));   /* link up, auto speed */

    for (int i = 0; i < 128; i++) wr(REG_MTA + i * 4, 0);      /* no multicast */

    uint32_t ral = rd(REG_RAL), rah = rd(REG_RAH);             /* the address the card was given... */
    if (rah & (1u << 31)) {
        for (int i = 0; i < 4; i++) mac[i] = (uint8_t)(ral >> (8 * i));
        mac[4] = (uint8_t)rah; mac[5] = (uint8_t)(rah >> 8);
    } else {                                                   /* ...or read it from the EEPROM */
        for (int i = 0; i < 3; i++) {
            int w = eeprom_word(i);
            if (w < 0) { kprintf("e1000: cannot read the MAC address\n"); return -1; }
            mac[2 * i] = (uint8_t)w; mac[2 * i + 1] = (uint8_t)(w >> 8);
        }
        wr(REG_RAL, mac[0] | (mac[1] << 8) | (mac[2] << 16) | ((uint32_t)mac[3] << 24));
        wr(REG_RAH, mac[4] | (mac[5] << 8) | (1u << 31));
    }

    uint64_t ring = pmm_alloc_contiguous(1);
    rx_bufs_phys = pmm_alloc_contiguous(RX_COUNT * BUF_SIZE / 4096);
    tx_bufs_phys = pmm_alloc_contiguous(TX_COUNT * BUF_SIZE / 4096);
    if (!ring || !rx_bufs_phys || !tx_bufs_phys) { kprintf("e1000: out of memory\n"); return -1; }
    memset(phys_to_virt(ring), 0, 4096);
    rx = (volatile struct rx_desc *)phys_to_virt(ring);
    tx = (volatile struct tx_desc *)((uint8_t *)phys_to_virt(ring) + 2048);
    rx_bufs = phys_to_virt(rx_bufs_phys);
    tx_bufs = phys_to_virt(tx_bufs_phys);

    for (int i = 0; i < RX_COUNT; i++) rx[i].addr = rx_bufs_phys + (uint64_t)i * BUF_SIZE;
    wr(REG_RDBAL, (uint32_t)ring);
    wr(REG_RDBAH, (uint32_t)(ring >> 32));
    wr(REG_RDLEN, RX_COUNT * sizeof(struct rx_desc));
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_COUNT - 1);
    wr(REG_RCTL, (1u << 1) | (1u << 15) | (1u << 26));         /* enable, accept broadcast, strip the CRC, 2 KiB buffers */

    wr(REG_TDBAL, (uint32_t)(ring + 2048));
    wr(REG_TDBAH, (uint32_t)((ring + 2048) >> 32));
    wr(REG_TDLEN, TX_COUNT * sizeof(struct tx_desc));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TIPG, 0x0060200A);
    wr(REG_TCTL, (1u << 1) | (1u << 3) | (0x0F << 4) | (0x40 << 12));   /* enable, pad short frames */

    ready = 1;
    kprintf("e1000: %02x:%02x:%02x:%02x:%02x:%02x (device %04x)\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], d.device);
    return 0;
}

int e1000_send(const void *frame, uint32_t len) {
    if (!ready || len > BUF_SIZE) return -1;
    if (len < 60) len = 60;
    uint64_t f = spin_lock_irqsave(&tx_lock);
    unsigned cur = tx_cur;
    memset(tx_bufs + (uint64_t)cur * BUF_SIZE, 0, len);
    memcpy(tx_bufs + (uint64_t)cur * BUF_SIZE, frame, len);
    tx[cur].addr = tx_bufs_phys + (uint64_t)cur * BUF_SIZE;
    tx[cur].length = (uint16_t)len;
    tx[cur].cso = 0;
    tx[cur].cmd = 0x0B;                                        /* end of packet, insert FCS, report status */
    tx[cur].status = 0;
    tx_cur = (cur + 1) % TX_COUNT;
    __sync_synchronize();
    wr(REG_TDT, tx_cur);
    int ok = 0;
    for (unsigned long n = 0; n < 50000000UL; n++) {
        if (tx[cur].status & 1) { ok = 1; break; }
        __asm__ volatile ("pause");
    }
    spin_unlock_irqrestore(&tx_lock, f);
    return ok ? 0 : -1;
}

int e1000_poll(void (*cb)(const uint8_t *frame, uint32_t len)) {
    if (!ready) return 0;
    int n = 0;
    for (;;) {
        volatile struct rx_desc *dsc = &rx[rx_cur];
        if (!(dsc->status & 1)) break;                         /* not filled yet */
        __sync_synchronize();
        if ((dsc->status & 2) && !dsc->errors && dsc->length > 0)
            cb(rx_bufs + (uint64_t)rx_cur * BUF_SIZE, dsc->length);
        dsc->status = 0;
        wr(REG_RDT, rx_cur);                                   /* give the descriptor back */
        rx_cur = (rx_cur + 1) % RX_COUNT;
        n++;
    }
    return n;
}
