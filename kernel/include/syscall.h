#pragma once
#include <stdint.h>
#include <stddef.h>
#include "interrupt.h"

/* System call numbers (shared with user/include/sys.h). Arguments: rdi, rsi, rdx, r10, r8, r9. */
#define SYS_WRITE  1   /* write(fd, buf, len) -> bytes written or -1 */
#define SYS_EXIT   2   /* exit(code) */
#define SYS_GETPID 3
#define SYS_YIELD  4
#define SYS_SLEEP  5   /* sleep(ms) */
#define SYS_BRK    6   /* brk(addr): 0 returns the current break */
#define SYS_MMAP   7   /* mmap(len) -> anonymous zeroed memory */
#define SYS_MUNMAP 8   /* munmap(addr, len) */
#define SYS_FORK   9
#define SYS_EXEC   10  /* exec(name, argv) */
#define SYS_WAIT   11  /* wait(&status) -> child pid or -1 */
#define SYS_UPTIME 12  /* uptime in ms */

void syscall_init(void);
int64_t syscall_dispatch(struct interrupt_frame *f);

/* Copy between kernel and user memory, checking every page is mapped and user-accessible
 * (and writable, for copy_to_user). Return 0, or -1 for a bad pointer. */
int copy_from_user(void *dst, uint64_t usrc, size_t n);
int copy_to_user(uint64_t udst, const void *src, size_t n);
int copy_str_from_user(char *dst, uint64_t usrc, size_t max);   /* NUL-terminated, fails if too long */
