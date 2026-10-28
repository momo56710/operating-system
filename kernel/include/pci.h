#pragma once
#include <stdint.h>

struct pci_dev {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint8_t  class_code, subclass;
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v);
int      pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out);   /* 0 found, -1 not */
void     pci_scan_print(void);                                              /* list every device on bus 0 */
