#pragma once
#include <stdint.h>
#include <stddef.h>
#include "interrupt.h"

#include "abi.h"

void syscall_init(void);
int64_t syscall_dispatch(struct interrupt_frame *f);

/* Copy between kernel and user memory, checking every page is mapped and user-accessible
 * (and writable, for copy_to_user). Return 0, or -1 for a bad pointer. */
int copy_from_user(void *dst, uint64_t usrc, size_t n);
int copy_to_user(uint64_t udst, const void *src, size_t n);
int copy_str_from_user(char *dst, uint64_t usrc, size_t max);   /* NUL-terminated, fails if too long */
