#include "acpi.h"
#include "string.h"

struct rsdp {
    char     sig[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
    uint8_t  ext_checksum, reserved[3];
} __attribute__((packed));

static uint64_t hhdm_base;
static struct acpi_sdt *root;      /* XSDT or RSDT */
static int root_is_xsdt;

static int sum_ok(const void *p, uint32_t len) {
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++) s += ((const uint8_t *)p)[i];
    return s == 0;
}

int acpi_init(void *rsdp_addr, uint64_t hhdm) {
    hhdm_base = hhdm;
    uint64_t a = (uint64_t)rsdp_addr;
    if (a < hhdm) a += hhdm;                         /* Limine may hand over a physical address */
    struct rsdp *r = (struct rsdp *)a;
    if (memcmp(r->sig, "RSD PTR ", 8) || !sum_ok(r, 20)) return -1;

    if (r->revision >= 2 && r->xsdt) { root = (struct acpi_sdt *)(r->xsdt + hhdm); root_is_xsdt = 1; }
    else                              { root = (struct acpi_sdt *)((uint64_t)r->rsdt + hhdm); }
    return memcmp(root->sig, root_is_xsdt ? "XSDT" : "RSDT", 4) ? -1 : 0;
}

struct acpi_sdt *acpi_find(const char sig[4]) {
    if (!root) return 0;
    uint32_t n = (root->length - sizeof *root) / (root_is_xsdt ? 8 : 4);
    uint8_t *entries = (uint8_t *)root + sizeof *root;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t phys = root_is_xsdt ? ((uint64_t *)entries)[i] : ((uint32_t *)entries)[i];
        struct acpi_sdt *t = (struct acpi_sdt *)(phys + hhdm_base);
        if (!memcmp(t->sig, sig, 4) && sum_ok(t, t->length)) return t;
    }
    return 0;
}
