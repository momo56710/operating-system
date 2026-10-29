#pragma once
#include <stdint.h>
#include <stddef.h>

#include "abi.h"

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

int  open(const char *path, int flags);
int  close(int fd);
long read(int fd, void *buf, size_t len);
long lseek(int fd, long off, int whence);
int  stat(const char *path, struct stat *st);
int  readdir(int fd, unsigned long index, struct dirent *d);
int  dup2(int from, int to);
int  pipe(int fds[2]);
int  chdir(const char *path);
int  getcwd(char *buf, size_t size);
int  mkdir(const char *path);
int  unlink(const char *path);
int  sysinfo(int what, unsigned long index, void *buf);
long ioctl(int fd, int request, long arg);
