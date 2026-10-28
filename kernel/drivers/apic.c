#include "apic.h"
#include "acpi.h"
#include "vmm.h"
#include "io.h"
#include "kprintf.h"
#include "panic.h"
#include "interrupt.h"

/* Local APIC registers (byte offsets from the MMIO base). */
#define LAPIC_ID        0x020
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

#define LVT_MASKED      (1u << 16)
#define LVT_PERIODIC    (1u << 17)

struct madt {
    struct acpi_sdt hdr;
    uint32_t lapic_addr;
    uint32_t flags;
    uint8_t  entries[];
} __attribute__((packed));

struct ioapic {
    volatile uint32_t *regs;
    uint32_t gsi_base, gsi_count;
};

struct iso {                       /* interrupt source override: ISA IRQ -> GSI */
    uint8_t  irq;
    uint32_t gsi;
    uint16_t flags;
};

static volatile uint32_t *lapic;
static struct ioapic ioapics[4];
static int nioapics;
static struct iso isos[16];
static int nisos;
static uint32_t bsp_id;
static uint32_t timer_ticks_per_ms;

static inline uint32_t lapic_read(uint32_t reg) { return lapic[reg / 4]; }
static inline void lapic_write(uint32_t reg, uint32_t v) { lapic[reg / 4] = v; }

static uint32_t ioapic_read(struct ioapic *io, uint32_t reg) { io->regs[0] = reg; return io->regs[4]; }
static void ioapic_write(struct ioapic *io, uint32_t reg, uint32_t v) { io->regs[0] = reg; io->regs[4] = v; }

/* Device memory is not part of the direct map, so map it uncached on demand. */
static volatile uint32_t *map_mmio(uint64_t phys) {
    uint64_t virt = phys + hhdm_offset;
    if (!vmm_translate(virt) && vmm_map(virt, phys, VMM_WRITE | VMM_NX | VMM_PCD) < 0)
        panic("cannot map MMIO page at %lx", phys);
    return (volatile uint32_t *)virt;
}

void apic_init(void) {
    uint64_t lapic_phys = rdmsr(0x1B) & 0xFFFFF000ULL;
    uint64_t ioapic_phys[4] = {0xFEC00000}, gsi_base[4] = {0};
    nioapics = 0;

    struct madt *m = (struct madt *)acpi_find("APIC");
    if (m) {
        lapic_phys = m->lapic_addr;
        uint8_t *p = m->entries, *end = (uint8_t *)m + m->hdr.length;
        while (p + 2 <= end && p[1] >= 2) {
            switch (p[0]) {
            case 1:   /* I/O APIC: id, reserved, addr(4), gsi base(4) */
                if (nioapics < 4) {
                    ioapic_phys[nioapics] = *(uint32_t *)(p + 4);
                    gsi_base[nioapics]    = *(uint32_t *)(p + 8);
                    nioapics++;
                }
                break;
            case 2:   /* interrupt source override: bus, source, gsi(4), flags(2) */
                if (nisos < 16) {
                    isos[nisos].irq   = p[3];
                    isos[nisos].gsi   = *(uint32_t *)(p + 4);
                    isos[nisos].flags = *(uint16_t *)(p + 8);
                    nisos++;
                }
                break;
            case 5:   /* local APIC address override */
                lapic_phys = *(uint64_t *)(p + 4);
                break;
            }
            p += p[1];
        }
    } else {
        kprintf("ACPI: no MADT, assuming one I/O APIC at 0xFEC00000\n");
    }
    if (!nioapics) nioapics = 1;                 /* default location, GSI base 0 */

    lapic = map_mmio(lapic_phys);
    lapic_write(LAPIC_SVR, 0x100 | VEC_SPURIOUS);           /* software enable */
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);               /* the legacy PIC is not used */
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);
    bsp_id = lapic_read(LAPIC_ID) >> 24;

    for (int i = 0; i < nioapics; i++) {
        ioapics[i].regs = map_mmio(ioapic_phys[i]);
        ioapics[i].gsi_base = gsi_base[i];
        ioapics[i].gsi_count = ((ioapic_read(&ioapics[i], 1) >> 16) & 0xFF) + 1;
        for (uint32_t g = 0; g < ioapics[i].gsi_count; g++)
            ioapic_write(&ioapics[i], 0x10 + 2 * g, LVT_MASKED);   /* everything masked until asked for */
    }
    kprintf("APIC: LAPIC at %lx (id %u), %d I/O APIC(s), %d override(s)\n",
            lapic_phys, bsp_id, nioapics, nisos);
}

void irq_enable(int irq) {
    uint32_t gsi = irq, pol_low = 0, level = 0;
    for (int i = 0; i < nisos; i++) {
        if (isos[i].irq != irq) continue;
        gsi = isos[i].gsi;
        if ((isos[i].flags & 3) == 3) pol_low = 1;
        if (((isos[i].flags >> 2) & 3) == 3) level = 1;
    }
    for (int i = 0; i < nioapics; i++) {
        struct ioapic *io = &ioapics[i];
        if (gsi < io->gsi_base || gsi >= io->gsi_base + io->gsi_count) continue;
        uint32_t pin = gsi - io->gsi_base;
        ioapic_write(io, 0x11 + 2 * pin, bsp_id << 24);
        ioapic_write(io, 0x10 + 2 * pin, (IRQ_BASE + irq) | (pol_low << 13) | (level << 15));
        return;
    }
    kprintf("irq_enable: no I/O APIC handles IRQ %d (GSI %u)\n", irq, gsi);
}

void irq_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

/* Count LAPIC timer ticks across a PIT-timed 50 ms window. */
void lapic_timer_start(unsigned hz) {
    outb(0x61, inb(0x61) & 0xFC);                           /* PIT channel 2 gate off, speaker off */
    outb(0x43, 0xB0);                                       /* channel 2, lo/hi byte, mode 0 */
    outb(0x42, 59659 & 0xFF);                               /* 59659 / 1193182 Hz = 50 ms */
    outb(0x42, 59659 >> 8);

    lapic_write(LAPIC_TIMER_DIV, 0x3);                      /* divide by 16 */
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED | VEC_TIMER);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
    outb(0x61, (inb(0x61) & 0xFC) | 1);                     /* gate on: start counting */
    while (!(inb(0x61) & 0x20)) ;                           /* wait for OUT2 */
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CUR);
    lapic_write(LAPIC_TIMER_INIT, 0);

    timer_ticks_per_ms = elapsed / 50;
    if (!timer_ticks_per_ms) panic("LAPIC timer calibration failed");
    kprintf("LAPIC timer: %u ticks/ms (~%u MHz bus/16)\n", timer_ticks_per_ms, timer_ticks_per_ms / 1000);

    lapic_write(LAPIC_LVT_TIMER, VEC_TIMER | LVT_PERIODIC);
    lapic_write(LAPIC_TIMER_INIT, timer_ticks_per_ms * (1000 / hz));
}
