#pragma once
#include <stddef.h>
#include "limine.h"

/* Files the bootloader loaded next to the kernel. Until there is a filesystem (Phase 6),
 * user programs reach the kernel this way, looked up by file name (for example "initrd.tar"). */
void        modules_init(struct limine_module_response *r);
const void *module_find(const char *name, size_t *size);
size_t      module_count(void);
const char *module_name(size_t i);   /* not NUL-terminated at the suffix: use module_name_len */
size_t      module_name_len(size_t i);
