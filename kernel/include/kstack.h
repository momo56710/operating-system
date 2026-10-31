#pragma once
#include <stdint.h>

/* Kernel stacks live in their own virtual region, 16 KiB each with an unmapped guard page below, so an
 * overflow faults instead of silently overwriting whatever sits underneath. */
int  kstack_alloc(uint64_t *top);      /* a slot number and the top of the stack, or -1 */
void kstack_free(int slot);
int  kstack_guard_hit(uint64_t addr);  /* the slot whose guard page contains addr, or -1 */
