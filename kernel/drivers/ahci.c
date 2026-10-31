#include "blk.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "sync.h"

/* AHCI: the standard SATA controller (VirtualBox, VMware, nearly every PC). One disk, one command slot,
 * polled to completion. Requests of up to 8 sectors go through a bounce buffer.
 *
 * Memory laid out for the controller (all physically contiguous, 2 pages):
 *   page 0: command list (32 headers, 1 KiB)  |  received-FIS area (256 B)  |  command table (header + 1 PRD)
 *   page 1: the data bounce buffer (4 KiB = 8 sectors) */

#define HBA_CAP  0x00
#define HBA_GHC  0x04
#define HBA_IS   0x08
#define HBA_PI   0x0C

#define P_CLB    (0x00 / 4)
#define P_CLBU   (0x04 / 4)
#define P_FB     (0x08 / 4)
#define P_FBU    (0x0C / 4)
#define P_IS     (0x10 / 4)
#define P_IE     (0x14 / 4)
#define P_CMD    (0x18 / 4)
#define P_TFD    (0x20 / 4)
#define P_SIG    (0x24 / 4)
#define P_SSTS   (0x28 / 4)
#define P_SERR   (0x30 / 4)
#define P_CI     (0x38 / 4)

#define CMD_ST   (1u << 0)
#define CMD_SUD  (1u << 1)
#define CMD_POD  (1u << 2)
#define CMD_FRE  (1u << 4)
#define CMD_FR   (1u << 14)
#define CMD_CR   (1u << 15)
#define TFD_BSY  0x80
#define TFD_DRQ  0x08
#define TFD_ERR  0x01
#define IS_TFES  (1u << 30)

#define SIG_ATA  0x00000101u

#define ATA_IDENTIFY  0xEC
#define ATA_READ_EXT  0x25
#define ATA_WRITE_EXT 0x35

static volatile uint32_t *hba;
static volatile uint32_t *port;
static uint64_t dma_phys;
static uint8_t *dma;
static uint64_t sectors;
static int ready;
static struct mutex lock;

#define CMD_TABLE_OFF 1280          /* 128-byte aligned, after the list (1 KiB) and the FIS area (256 B) */
#define BOUNCE_OFF    4096

static int wait_clear(volatile uint32_t *reg, uint32_t mask) {
    for (unsigned long i = 0; i < 50000000UL; i++) if (!(*reg & mask)) return 0;
    return -1;
}

static void stop_engine(void) {
    port[P_CMD] &= ~CMD_ST;
    wait_clear(&port[P_CMD], CMD_CR);
    port[P_CMD] &= ~CMD_FRE;
    wait_clear(&port[P_CMD], CMD_FR);
}

static void start_engine(void) {
    wait_clear(&port[P_CMD], CMD_CR);
    port[P_CMD] |= CMD_FRE;
    port[P_CMD] |= CMD_ST;
}

/* Run one command in slot 0. fis points at the 20-byte host-to-device FIS; `bytes` of the bounce buffer
 * are transferred (0 for none); `write` is the data direction. */
static int issue(const uint8_t fis[20], uint32_t bytes, int write) {
    if (wait_clear(&port[P_TFD], TFD_BSY | TFD_DRQ) < 0) return -1;

    uint8_t *table = dma + CMD_TABLE_OFF;
    memset(table, 0, 128 + 16);
    memcpy(table, fis, 20);                                      /* the command FIS */
    uint32_t *prd = (uint32_t *)(table + 128);                   /* one physical region descriptor */
    if (bytes) {
        prd[0] = (uint32_t)(dma_phys + BOUNCE_OFF);
        prd[1] = (uint32_t)((dma_phys + BOUNCE_OFF) >> 32);
        prd[3] = bytes - 1;
    }

    uint32_t *hdr = (uint32_t *)dma;                             /* command header 0 */
    hdr[0] = 5 | (write ? (1u << 6) : 0) | ((bytes ? 1u : 0) << 16);   /* FIS is 5 dwords; PRDT length */
    hdr[1] = 0;
    hdr[2] = (uint32_t)(dma_phys + CMD_TABLE_OFF);
    hdr[3] = (uint32_t)((dma_phys + CMD_TABLE_OFF) >> 32);

    port[P_IS] = 0xFFFFFFFF;
    port[P_SERR] = 0xFFFFFFFF;
    __sync_synchronize();
    port[P_CI] = 1;                                              /* go */

    for (unsigned long i = 0; i < 200000000UL; i++) {
        if (!(port[P_CI] & 1)) {
            if (port[P_IS] & IS_TFES) return -1;                 /* the drive reported an error */
            return 0;
        }
        if (port[P_IS] & IS_TFES) return -1;
        __asm__ volatile ("pause");
    }
    return -1;
}

static void h2d(uint8_t *fis, uint8_t cmd, uint64_t lba, uint32_t count) {
    memset(fis, 0, 20);
    fis[0] = 0x27;                                               /* register FIS, host to device */
    fis[1] = 0x80;                                               /* this carries a command */
    fis[2] = cmd;
    fis[4] = lba & 0xFF; fis[5] = (lba >> 8) & 0xFF; fis[6] = (lba >> 16) & 0xFF;
    fis[7] = 0x40;                                               /* LBA mode */
    fis[8] = (lba >> 24) & 0xFF; fis[9] = (lba >> 32) & 0xFF; fis[10] = (lba >> 40) & 0xFF;
    fis[12] = count & 0xFF; fis[13] = (count >> 8) & 0xFF;
}

int ahci_init(void) {
    struct pci_dev d;
    if (pci_find_class(0x01, 0x06, 0x01, &d) < 0) return -1;     /* mass storage, SATA, AHCI */
    pci_enable(&d, 1, 0);
    uint32_t bar5 = pci_read32(d.bus, d.slot, d.func, 0x24);
    if (bar5 & 1) { kprintf("ahci: BAR5 is not memory\n"); return -1; }
    hba = vmm_map_mmio(bar5 & ~0xFULL, 0x2000);

    hba[HBA_GHC / 4] |= 1u << 31;                                /* AHCI enable */
    hba[HBA_IS / 4] = 0xFFFFFFFF;
    uint32_t implemented = hba[HBA_PI / 4];

    int found = -1;
    for (int i = 0; i < 32; i++) {
        if (!(implemented & (1u << i))) continue;
        volatile uint32_t *p = hba + (0x100 + 0x80 * i) / 4;
        uint32_t ssts = p[P_SSTS];
        if ((ssts & 0xF) == 3 && ((ssts >> 8) & 0xF) == 1 && p[P_SIG] == SIG_ATA) { found = i; port = p; break; }
    }
    if (found < 0) { kprintf("ahci: controller found, no SATA disk attached\n"); return -1; }

    dma_phys = pmm_alloc_contiguous(2);
    if (!dma_phys) { kprintf("ahci: out of memory\n"); return -1; }
    dma = phys_to_virt(dma_phys);
    memset(dma, 0, 8192);

    stop_engine();
    port[P_CLB] = (uint32_t)dma_phys;
    port[P_CLBU] = (uint32_t)(dma_phys >> 32);
    port[P_FB] = (uint32_t)(dma_phys + 1024);
    port[P_FBU] = (uint32_t)((dma_phys + 1024) >> 32);
    port[P_SERR] = 0xFFFFFFFF;
    port[P_IS] = 0xFFFFFFFF;
    port[P_IE] = 0;                                              /* polled: no interrupts */
    if (hba[HBA_CAP / 4] & (1u << 27)) port[P_CMD] |= CMD_POD | CMD_SUD;
    start_engine();

    mutex_init(&lock);
    uint8_t fis[20];
    h2d(fis, ATA_IDENTIFY, 0, 0);
    fis[7] = 0;
    if (issue(fis, 512, 0) < 0) { kprintf("ahci: IDENTIFY failed\n"); return -1; }
    uint16_t *id = (uint16_t *)(dma + BOUNCE_OFF);
    if (id[83] & (1 << 10)) sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) | ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    else                    sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    if (!sectors) { kprintf("ahci: drive reports no sectors\n"); return -1; }
    ready = 1;
    kprintf("ahci: SATA disk on port %d, %lu sectors (%lu MiB)\n", found, sectors, sectors / 2048);
    return 0;
}

uint64_t ahci_sectors(void) { return ready ? sectors : 0; }

int ahci_rw(uint64_t sector, uint32_t count, void *buf, int write) {
    if (!ready || !count || count > 8 || sector + count > sectors) return -1;
    mutex_lock(&lock);
    uint32_t bytes = count * 512;
    if (write) memcpy(dma + BOUNCE_OFF, buf, bytes);
    uint8_t fis[20];
    h2d(fis, write ? ATA_WRITE_EXT : ATA_READ_EXT, sector, count);
    int r = issue(fis, bytes, write);
    if (r == 0 && !write) memcpy(buf, dma + BOUNCE_OFF, bytes);
    mutex_unlock(&lock);
    return r;
}
