#pragma once
#include <stdint.h>
#include <stddef.h>

/* Map the PT_LOAD segments of a static x86-64 ELF executable into `pml4`.
 * On success returns 0 and fills in the entry point and the first address after the image. */
int elf_load(uint64_t pml4, const void *image, size_t size, uint64_t *entry, uint64_t *image_end);
