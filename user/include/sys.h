#pragma once
#include <stdint.h>
#include <stddef.h>

/* System call numbers: keep in step with kernel/include/syscall.h. */
#define SYS_WRITE  1
#define SYS_EXIT   2
#define SYS_GETPID 3
#define SYS_YIELD  4
#define SYS_SLEEP  5
#define SYS_BRK    6
#define SYS_MMAP   7
#define SYS_MUNMAP 8
#define SYS_FORK   9
#define SYS_EXEC   10
#define SYS_WAIT   11
#define SYS_UPTIME 12

/* syscall clobbers rcx and r11 */
static inline long syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}

long write(int fd, const void *buf, size_t len);
void exit(int code) __attribute__((noreturn));
int  getpid(void);
void yield(void);
void sleep_ms(unsigned long ms);
void *sbrk(long increment);
void *mmap(size_t len);
int  munmap(void *addr, size_t len);
int  fork(void);
int  exec(const char *name, char *const argv[]);
int  wait(int *status);
unsigned long uptime_ms(void);
