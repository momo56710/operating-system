#pragma once
#include <stdint.h>

struct acpi_sdt {
    char     sig[4];
    uint32_t length;
    uint8_t  revision, checksum;
    char     oem_id[6], oem_table_id[8];
    uint32_t oem_revision, creator_id, creator_revision;
} __attribute__((packed));

int              acpi_init(void *rsdp, uint64_t hhdm);   /* 0 ok, -1 invalid */
struct acpi_sdt *acpi_find(const char sig[4]);           /* virtual address, or 0 */
