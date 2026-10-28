#include "modules.h"
#include "string.h"

#define MAX_MODULES 16

static struct { const char *name; size_t name_len; const void *data; size_t size; } mods[MAX_MODULES];
static size_t nmods;

void modules_init(struct limine_module_response *r) {
    if (!r) return;
    for (uint64_t i = 0; i < r->module_count && nmods < MAX_MODULES; i++) {
        struct limine_file *f = r->modules[i];
        const char *path = f->path, *base = path;
        for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
        size_t len = strlen(base);
        if (len > 4 && !strcmp(base + len - 4, ".elf")) len -= 4;
        mods[nmods].name = base;
        mods[nmods].name_len = len;
        mods[nmods].data = f->address;
        mods[nmods].size = f->size;
        nmods++;
    }
}

const void *module_find(const char *name, size_t *size) {
    size_t n = strlen(name);
    for (size_t i = 0; i < nmods; i++)
        if (mods[i].name_len == n && !memcmp(mods[i].name, name, n)) {
            if (size) *size = mods[i].size;
            return mods[i].data;
        }
    return 0;
}

size_t module_count(void) { return nmods; }
const char *module_name(size_t i) { return mods[i].name; }
size_t module_name_len(size_t i) { return mods[i].name_len; }
