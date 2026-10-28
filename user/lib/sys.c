#include "sys.h"

long write(int fd, const void *buf, size_t len) { return syscall3(SYS_WRITE, fd, (long)buf, (long)len); }
void exit(int code) { syscall3(SYS_EXIT, code, 0, 0); for (;;) ; }
int  getpid(void) { return (int)syscall3(SYS_GETPID, 0, 0, 0); }
void yield(void) { syscall3(SYS_YIELD, 0, 0, 0); }
void sleep_ms(unsigned long ms) { syscall3(SYS_SLEEP, (long)ms, 0, 0); }
int  fork(void) { return (int)syscall3(SYS_FORK, 0, 0, 0); }
int  exec(const char *name, char *const argv[]) { return (int)syscall3(SYS_EXEC, (long)name, (long)argv, 0); }
int  wait(int *status) { return (int)syscall3(SYS_WAIT, (long)status, 0, 0); }
unsigned long uptime_ms(void) { return (unsigned long)syscall3(SYS_UPTIME, 0, 0, 0); }
void *mmap(size_t len) { long r = syscall3(SYS_MMAP, (long)len, 0, 0); return r < 0 ? (void *)0 : (void *)r; }
int  munmap(void *addr, size_t len) { return (int)syscall3(SYS_MUNMAP, (long)addr, (long)len, 0); }

/* Grow (or shrink) the heap by `increment` bytes; returns the old break, or (void *)-1 on failure. */
void *sbrk(long increment) {
    long cur = syscall3(SYS_BRK, 0, 0, 0);
    if (cur < 0) return (void *)-1;
    if (!increment) return (void *)cur;
    long r = syscall3(SYS_BRK, cur + increment, 0, 0);
    return r < 0 ? (void *)-1 : (void *)cur;
}
