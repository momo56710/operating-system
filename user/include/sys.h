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
void _exit(int code) __attribute__((noreturn));
int  isatty(int fd);                         /* is this descriptor the console? */
long time_seconds(void);                     /* the kernel's wall clock: seconds since 1970, UTC */
int  getpid(void);
void yield(void);
void sleep_ms(unsigned long ms);
void *sbrk(long increment);
void *mmap(size_t len);
int  munmap(void *addr, size_t len);
int  fork(void);
int  exec(const char *name, char *const argv[]);
int  wait(int *status);
int  waitpid_nohang(int *status);   /* 0 if no child has finished yet */
int  kill(int pid);
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

long netctl(int op, void *arg);

int  socket(int type);
int  bind(int fd, unsigned port);
int  connect(int fd, unsigned ip, unsigned port);
int  listen(int fd, int backlog);
int  accept(int fd, unsigned *ip, unsigned *port);
long sendto(int fd, const void *buf, size_t len, unsigned ip, unsigned port);   /* ip 0: the connected peer */
long recvfrom(int fd, void *buf, size_t len, unsigned *ip, unsigned *port, int timeout_ms);
int  shutdown(int fd);                                                            /* stop sending; keep reading */
