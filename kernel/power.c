#include "power.h"
#include "acpi.h"
#include "io.h"
#include "kprintf.h"
#include "vmm.h"
#include "string.h"

/* The disk cache is write-through, so everything is already on the disk when we get here. */


/* The sleep type for "soft off" (S5) is stored as a small AML package named _S5_ in the DSDT. */
static int find_s5(const uint8_t *dsdt, uint32_t len, uint8_t *typ_a, uint8_t *typ_b) {
    for (uint32_t i = 0; i + 8 < len; i++) {
        if (memcmp(dsdt + i, "_S5_", 4)) continue;
        const uint8_t *p = dsdt + i + 4;
        if (*p != 0x12) continue;                          /* PackageOp */
        p++;
        p += 1 + (*p >> 6);                                /* PkgLength: the top two bits say how many more bytes follow */
        p++;                                               /* number of elements */
        if (*p == 0x0A) p++;                               /* BytePrefix */
        *typ_a = *p++;
        if (*p == 0x0A) p++;
        *typ_b = *p;
        return 0;
    }
    return -1;
}

void power_off(void) {
    kprintf("\nPowering off...\n");
    __asm__ volatile ("cli");

    struct acpi_sdt *fadt = acpi_find("FACP");
    if (fadt) {
        const uint8_t *f = (const uint8_t *)fadt;
        uint32_t smi_cmd = *(const uint32_t *)(f + 48);
        uint8_t acpi_enable = f[52];
        uint32_t pm1a = *(const uint32_t *)(f + 64), pm1b = *(const uint32_t *)(f + 68);
        uint64_t dsdt_phys = *(const uint32_t *)(f + 40);
        if (fadt->length >= 148 && *(const uint64_t *)(f + 140)) dsdt_phys = *(const uint64_t *)(f + 140);
        if (dsdt_phys && pm1a) {
            const struct acpi_sdt *d = (const struct acpi_sdt *)phys_to_virt(dsdt_phys);
            uint8_t ta, tb;
            if (!memcmp(d->sig, "DSDT", 4) && find_s5((const uint8_t *)d, d->length, &ta, &tb) == 0) {
                if (smi_cmd && acpi_enable && !(inw((uint16_t)pm1a) & 1)) {      /* ACPI mode off: ask the firmware to switch it on */
                    outb((uint16_t)smi_cmd, acpi_enable);
                    for (int i = 0; i < 3000000 && !(inw((uint16_t)pm1a) & 1); i++) __asm__ volatile ("pause");
                }
                outw((uint16_t)pm1a, (uint16_t)((ta << 10) | (1 << 13)));        /* SLP_TYP | SLP_EN */
                if (pm1b) outw((uint16_t)pm1b, (uint16_t)((tb << 10) | (1 << 13)));
                for (int i = 0; i < 5000000; i++) __asm__ volatile ("pause");
            }
        }
    }
    /* the well-known emulator ports, in case the tables were no help */
    outw(0x604, 0x2000);       /* QEMU (q35 and newer) */
    outw(0xB004, 0x2000);      /* Bochs, old QEMU */
    outw(0x4004, 0x3400);      /* VirtualBox */
    outw(0x600, 0x34);         /* Cloud Hypervisor / some firmwares */
    kprintf("It is now safe to turn off your computer.\n");
    halt_forever();
}

void power_reboot(void) {
    kprintf("\nRebooting...\n");
    __asm__ volatile ("cli");
    outb(0x64, 0xFE);                                      /* the keyboard controller's reset line */
    for (int i = 0; i < 2000000; i++) __asm__ volatile ("pause");
    struct acpi_sdt *fadt = acpi_find("FACP");
    if (fadt && fadt->length >= 129) {
        const uint8_t *f = (const uint8_t *)fadt;
        if (f[116] == 1) outb((uint16_t)*(const uint64_t *)(f + 120), f[128]);   /* the ACPI reset register, if it is an I/O port */
    }
    outb(0xCF9, 0x06);                                     /* PCI reset */
    for (int i = 0; i < 2000000; i++) __asm__ volatile ("pause");
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) empty = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(empty));     /* last resort: a triple fault resets the CPU */
    halt_forever();
}
