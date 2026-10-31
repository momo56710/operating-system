#include "sys.h"

/* Provided by stdio.c when it is linked: flush buffered output before reading input or forking. */
extern void __stdio_prepare_read(void) __attribute__((weak));
extern void __stdio_flush_all(void) __attribute__((weak));

long write(int fd, const void *buf, size_t len) { return syscall3(SYS_WRITE, fd, (long)buf, (long)len); }
void _exit(int code) { syscall3(SYS_EXIT, code, 0, 0); for (;;) ; }
long time_seconds(void) { return syscall3(SYS_TIME, 0, 0, 0); }
int  getpid(void) { return (int)syscall3(SYS_GETPID, 0, 0, 0); }
void yield(void) { syscall3(SYS_YIELD, 0, 0, 0); }
void sleep_ms(unsigned long ms) { syscall3(SYS_SLEEP, (long)ms, 0, 0); }
int  fork(void) {
    if (__stdio_flush_all) __stdio_flush_all();                          /* buffered output must not be duplicated into the child */
    return (int)syscall3(SYS_FORK, 0, 0, 0);
}
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

int  open(const char *path, int flags) { return (int)syscall3(SYS_OPEN, (long)path, flags, 0); }
int  close(int fd) { return (int)syscall3(SYS_CLOSE, fd, 0, 0); }
long read(int fd, void *buf, size_t len) {
    if (fd == 0 && __stdio_prepare_read) __stdio_prepare_read();        /* show a pending prompt before waiting for input */
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}
long lseek(int fd, long off, int whence) { return syscall3(SYS_LSEEK, fd, off, whence); }
int  stat(const char *path, struct stat *st) { return (int)syscall3(SYS_STAT, (long)path, (long)st, 0); }
int  readdir(int fd, unsigned long index, struct dirent *d) { return (int)syscall3(SYS_READDIR, fd, (long)index, (long)d); }
int  dup2(int from, int to) { return (int)syscall3(SYS_DUP2, from, to, 0); }
int  pipe(int fds[2]) { return (int)syscall3(SYS_PIPE, (long)fds, 0, 0); }
int  chdir(const char *path) { return (int)syscall3(SYS_CHDIR, (long)path, 0, 0); }
int  getcwd(char *buf, size_t size) { return (int)syscall3(SYS_GETCWD, (long)buf, (long)size, 0); }
int  mkdir(const char *path) { return (int)syscall3(SYS_MKDIR, (long)path, 0, 0); }
int  unlink(const char *path) { return (int)syscall3(SYS_UNLINK, (long)path, 0, 0); }
int  sysinfo(int what, unsigned long index, void *buf) { return (int)syscall3(SYS_SYSINFO, what, (long)index, (long)buf); }
long ioctl(int fd, int request, long arg) { return syscall3(SYS_IOCTL, fd, request, arg); }
long netctl(int op, void *arg) { return syscall3(SYS_NETCTL, op, (long)arg, 0); }

int socket(int type) { return (int)syscall3(SYS_SOCKET, type, 0, 0); }
int bind(int fd, unsigned port) { return (int)syscall3(SYS_BIND, fd, port, 0); }
int connect(int fd, unsigned ip, unsigned port) {
    struct sockaddr_in a = { ip, (uint16_t)port, 0 };
    return (int)syscall3(SYS_CONNECT, fd, (long)&a, 0);
}
int listen(int fd, int backlog) { return (int)syscall3(SYS_LISTEN, fd, backlog, 0); }
int accept(int fd, unsigned *ip, unsigned *port) {
    struct sockaddr_in a = { 0, 0, 0 };
    int r = (int)syscall3(SYS_ACCEPT, fd, (long)&a, 0);
    if (ip) *ip = a.ip;
    if (port) *port = a.port;
    return r;
}
long sendto(int fd, const void *buf, size_t len, unsigned ip, unsigned port) {
    struct sockmsg m = { (uint64_t)buf, len, ip, (uint16_t)port, 0, 0, 0 };
    return syscall3(SYS_SENDTO, fd, (long)&m, 0);
}
long recvfrom(int fd, void *buf, size_t len, unsigned *ip, unsigned *port, int timeout_ms) {
    struct sockmsg m = { (uint64_t)buf, len, 0, 0, 0, timeout_ms, 0 };
    long r = syscall3(SYS_RECVFROM, fd, (long)&m, 0);
    if (ip) *ip = m.ip;
    if (port) *port = m.port;
    return r;
}
int shutdown(int fd) { return (int)syscall3(SYS_SHUTDOWN, fd, 0, 0); }
