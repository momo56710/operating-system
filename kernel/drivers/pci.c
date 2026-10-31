#include "pci.h"
#include "io.h"
#include "kprintf.h"

/* Legacy configuration access: write an address to 0xCF8, read or write the data at 0xCFC. */
static uint32_t cfg_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC);
}

static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(0xCF8, cfg_addr(bus, slot, func, off));
    return inl(0xCFC);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v) {
    outl(0xCF8, cfg_addr(bus, slot, func, off));
    outl(0xCFC, v);
}

/* Visit every device on every bus. */
static void for_each(void (*fn)(const struct pci_dev *, void *), void *ctx) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read32(bus, slot, func, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (!func) break; continue; }       /* nothing here */
                uint32_t cls = pci_read32(bus, slot, func, 8);
                struct pci_dev d = { bus, slot, func, id & 0xFFFF, id >> 16, cls >> 24, (cls >> 16) & 0xFF, (cls >> 8) & 0xFF };
                fn(&d, ctx);
                if (!func && !(((pci_read32(bus, slot, 0, 0x0C) >> 16) & 0xFF) & 0x80)) break;   /* single-function device */
            }
        }
    }
}

struct find_ctx { uint16_t vendor, device; struct pci_dev *out; int found; };

static void find_cb(const struct pci_dev *d, void *c) {
    struct find_ctx *f = c;
    if (!f->found && d->vendor == f->vendor && d->device == f->device) { *f->out = *d; f->found = 1; }
}

int pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out) {
    struct find_ctx f = { vendor, device, out, 0 };
    for_each(find_cb, &f);
    return f.found ? 0 : -1;
}

static void print_cb(const struct pci_dev *d, void *c) {
    (void)c;
    kprintf("  pci %02x:%02x.%x  %04x:%04x  class %02x.%02x\n", d->bus, d->slot, d->func, d->vendor, d->device, d->class_code, d->subclass);
}

void pci_scan_print(void) { for_each(print_cb, 0); }

struct class_ctx { uint8_t cls, sub; int prog; struct pci_dev *out; int found; };

static void class_cb(const struct pci_dev *d, void *c) {
    struct class_ctx *f = c;
    if (!f->found && d->class_code == f->cls && d->subclass == f->sub && (f->prog < 0 || d->prog_if == f->prog)) { *f->out = *d; f->found = 1; }
}

int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, struct pci_dev *out) {
    struct class_ctx f = { class_code, subclass, prog_if, out, 0 };
    for_each(class_cb, &f);
    return f.found ? 0 : -1;
}

void pci_enable(const struct pci_dev *d, int mmio, int io) {
    uint32_t cmd = pci_read32(d->bus, d->slot, d->func, 0x04);
    cmd |= 4;                                          /* bus master: the device may read and write memory */
    if (mmio) cmd |= 2;
    if (io) cmd |= 1;
    pci_write32(d->bus, d->slot, d->func, 0x04, cmd);
}
