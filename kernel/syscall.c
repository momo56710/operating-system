#include "syscall.h"
#include "process.h"
#include "task.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "vfs.h"
#include "pmm.h"
#include "heap.h"
#include "net.h"
#include "cpu.h"
#include "fb.h"
#include "input.h"
#include "power.h"

/* ---- checked access to user memory ---- */

/* Is [addr, addr+n) entirely mapped, user-accessible (and writable if `write`)? */
static int user_range_ok(uint64_t addr, size_t n, int write) {
    if (!n) return 1;
    if (addr < 0x1000 || addr >= USER_MAX || n > USER_MAX - addr) return 0;
    uint64_t pml4 = current_task()->pml4;
    if (!pml4) return 0;
    for (uint64_t page = addr & ~4095ULL; page < addr + n; page += 4096) {
        uint64_t flags;
        if (!vmm_lookup(pml4, page, &flags) && !(process_grow_stack(page) && vmm_lookup(pml4, page, &flags))) return 0;
        if (!(flags & VMM_USER)) return 0;
        if (write && !(flags & VMM_WRITE)) return 0;
    }
    return 1;
}

int copy_from_user(void *dst, uint64_t usrc, size_t n) {
    if (!user_range_ok(usrc, n, 0)) return -1;
    user_access_begin();
    memcpy(dst, (const void *)usrc, n);
    user_access_end();
    return 0;
}

int copy_to_user(uint64_t udst, const void *src, size_t n) {
    if (!user_range_ok(udst, n, 1)) return -1;
    user_access_begin();
    memcpy((void *)udst, src, n);
    user_access_end();
    return 0;
}

int copy_str_from_user(char *dst, uint64_t usrc, size_t max) {
    for (size_t i = 0; i < max; i++) {
        if (!user_range_ok(usrc + i, 1, 0)) return -1;
        user_access_begin();
        dst[i] = *(const char *)(usrc + i);
        user_access_end();
        if (!dst[i]) return 0;
    }
    return -1;                                   /* no terminator within max bytes */
}

/* ---- the calls ---- */

#define ARGV_MAX 32
#define ARG_LEN  256
#define IO_CHUNK 2048

static struct file *fd_file(uint64_t fd) {
    return fd < FD_MAX ? current_task()->fds[fd] : 0;
}

static int fd_install(struct file *f) {
    struct task *t = current_task();
    for (int i = 0; i < FD_MAX; i++)
        if (!t->fds[i]) { t->fds[i] = f; return i; }
    file_close(f);
    return -1;
}

/* Copy a path from user memory and make it absolute relative to the caller's working directory. */
static int user_path(uint64_t uptr, char *abs) {
    char raw[PATH_MAX_LEN];
    if (copy_str_from_user(raw, uptr, sizeof raw) < 0 || !raw[0]) return -1;       /* an empty path names nothing (ENOENT) */
    return vfs_normalize(current_task()->cwd, raw, abs, PATH_MAX_LEN);
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_file(fd);
    if (!f) return -1;
    char chunk[IO_CHUNK];
    uint64_t done = 0;
    while (done < len) {
        size_t n = len - done < sizeof chunk ? len - done : sizeof chunk;
        if (copy_from_user(chunk, buf + done, n) < 0) return done ? (int64_t)done : -1;
        int64_t w = file_write(f, chunk, n);
        if (w < 0) return done ? (int64_t)done : -1;
        done += w;
        if ((size_t)w < n) break;
    }
    return (int64_t)done;
}

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_file(fd);
    if (!f) return -1;
    char chunk[IO_CHUNK];
    size_t n = len < sizeof chunk ? len : sizeof chunk;     /* one chunk per call; the C library loops */
    int64_t r = file_read(f, chunk, n);
    if (r > 0 && copy_to_user(buf, chunk, r) < 0) return -1;
    return r;
}

static int64_t sys_open(uint64_t upath, uint64_t flags) {
    char path[PATH_MAX_LEN];
    if (user_path(upath, path) < 0) return -1;
    struct file *f = file_open(path, (int)flags);
    return f ? fd_install(f) : -1;
}

static int64_t sys_close(uint64_t fd) {
    struct file *f = fd_file(fd);
    if (!f) return -1;
    current_task()->fds[fd] = 0;
    file_close(f);
    return 0;
}

static int64_t sys_dup2(uint64_t from, uint64_t to) {
    struct file *f = fd_file(from);
    if (!f || to >= FD_MAX) return -1;
    if (from == to) return (int64_t)to;
    struct task *t = current_task();
    struct file *old = t->fds[to];
    t->fds[to] = file_dup(f);
    if (old) file_close(old);
    return (int64_t)to;
}

static int64_t sys_pipe(uint64_t ufds) {
    struct file *rd = 0, *wr = 0;
    if (pipe_create(&rd, &wr) < 0) { if (rd) file_close(rd); if (wr) file_close(wr); return -1; }
    int r = fd_install(rd);
    int w = r >= 0 ? fd_install(wr) : -1;
    int fds[2] = { r, w };
    if (r < 0 || w < 0 || copy_to_user(ufds, fds, sizeof fds) < 0) {
        if (r >= 0) sys_close(r);
        if (w >= 0) sys_close(w);
        else if (r < 0) file_close(wr);
        return -1;
    }
    return 0;
}

static int64_t sys_stat(uint64_t upath, uint64_t ust) {
    char path[PATH_MAX_LEN];
    struct stat st;
    if (user_path(upath, path) < 0 || vfs_stat(path, &st) < 0) return -1;
    return copy_to_user(ust, &st, sizeof st);
}

static int64_t sys_readdir(uint64_t fd, uint64_t index, uint64_t udirent) {
    struct file *f = fd_file(fd);
    struct dirent d;
    if (!f) return -1;
    int r = file_readdir(f, index, &d);
    if (r > 0 && copy_to_user(udirent, &d, sizeof d) < 0) return -1;
    return r;
}

static int64_t sys_chdir(uint64_t upath) {
    char path[PATH_MAX_LEN];
    struct stat st;
    if (user_path(upath, path) < 0 || vfs_stat(path, &st) < 0 || st.type != FT_DIR || strlen(path) >= CWD_MAX) return -1;
    memcpy(current_task()->cwd, path, strlen(path) + 1);
    return 0;
}

static int64_t sys_getcwd(uint64_t ubuf, uint64_t size) {
    const char *cwd = current_task()->cwd;
    size_t n = strlen(cwd) + 1;
    return n <= size && copy_to_user(ubuf, cwd, n) == 0 ? 0 : -1;
}

static int64_t sys_mkdir(uint64_t upath) {
    char path[PATH_MAX_LEN];
    return user_path(upath, path) < 0 ? -1 : vfs_mkdir(path);
}

static int64_t sys_unlink(uint64_t upath) {
    char path[PATH_MAX_LEN];
    return user_path(upath, path) < 0 ? -1 : vfs_unlink(path);
}

static int64_t sys_socket(uint64_t type) {
    struct file *f = sock_create((int)type);
    return f ? fd_install(f) : -1;
}

static int64_t sys_connect(uint64_t fd, uint64_t uaddr) {
    struct file *f = fd_file(fd);
    struct sockaddr_in a;
    if (!f || !sock_is_socket(f) || copy_from_user(&a, uaddr, sizeof a) < 0) return -1;
    return sock_connect(f, a.ip, a.port, 5000);
}

static int64_t sys_accept(uint64_t fd, uint64_t uaddr) {
    struct file *f = fd_file(fd);
    if (!f || !sock_is_socket(f)) return -1;
    struct sockaddr_in a = { 0, 0, 0 };
    struct file *nf = sock_accept(f, &a.ip, &a.port, 0);
    if (!nf) return -1;
    if (uaddr) copy_to_user(uaddr, &a, sizeof a);
    return fd_install(nf);
}

static int64_t sys_sendto(uint64_t fd, uint64_t umsg) {
    struct file *f = fd_file(fd);
    struct sockmsg m;
    if (!f || !sock_is_socket(f) || copy_from_user(&m, umsg, sizeof m) < 0) return -1;
    char chunk[IO_CHUNK];
    uint64_t done = 0;
    do {                                                   /* a stream may be longer than our buffer; a datagram never is */
        size_t n = m.len - done < sizeof chunk ? m.len - done : sizeof chunk;
        if (copy_from_user(chunk, m.buf + done, n) < 0) return done ? (int64_t)done : -1;
        int64_t w = sock_sendto(f, chunk, n, m.ip, m.port);
        if (w < 0) return done ? (int64_t)done : -1;
        done += w;
        if ((size_t)w < n) break;
    } while (done < m.len);
    return (int64_t)done;
}

static int64_t sys_recvfrom(uint64_t fd, uint64_t umsg) {
    struct file *f = fd_file(fd);
    struct sockmsg m;
    if (!f || !sock_is_socket(f) || copy_from_user(&m, umsg, sizeof m) < 0) return -1;
    char chunk[IO_CHUNK];
    size_t n = m.len < sizeof chunk ? m.len : sizeof chunk;
    int64_t r = sock_recvfrom(f, chunk, n, &m.ip, &m.port, m.timeout_ms);
    if (r > 0 && copy_to_user(m.buf, chunk, r) < 0) return -1;
    copy_to_user(umsg, &m, sizeof m);                      /* the sender's address */
    return r;
}

static int64_t sys_netctl(uint64_t op, uint64_t uptr) {
    if (op == NET_INFO) {
        struct net_info n;
        memset(&n, 0, sizeof n);
        memcpy(n.mac, netif.mac, 6);
        n.up = netif.up; n.ip = netif.ip; n.mask = netif.mask; n.gw = netif.gw; n.dns = netif.dns;
        n.rx_frames = netif.rx_frames; n.tx_frames = netif.tx_frames;
        n.rx_bytes = netif.rx_bytes; n.tx_bytes = netif.tx_bytes; n.rx_dropped = netif.rx_dropped;
        return copy_to_user(uptr, &n, sizeof n);
    }
    if (op == NET_SETDNS) {
        uint32_t ip;
        if (copy_from_user(&ip, uptr, sizeof ip) < 0) return -1;
        netif.dns = ip;
        return 0;
    }
    if (op == NET_PING) {
        struct ping_req p;
        if (!netif.up || copy_from_user(&p, uptr, sizeof p) < 0) return -1;
        p.rtt_ms = icmp_ping(p.ip, (uint16_t)p.seq, p.timeout_ms > 0 ? p.timeout_ms : 1000);
        if (copy_to_user(uptr, &p, sizeof p) < 0) return -1;
        return p.rtt_ms >= 0 ? 0 : -1;
    }
    return -1;
}

extern void disk_stats(struct si_disk *d);
extern uint64_t wall_time(void);

static int64_t sys_gfx(uint64_t op, uint64_t a1) {
    struct task *t = current_task();
    struct gfx_info gi;
    uint64_t phys;
    if (fb_get_info(&gi, &phys) < 0) return -1;
    switch (op) {
    case GFX_INFO: return copy_to_user(a1, &gi, sizeof gi);
    case GFX_MAP:  return process_map_device(phys, gi.size);
    case GFX_ACQUIRE: return gfx_acquire((uint32_t)t->pid, gi.width, gi.height);
    case GFX_RELEASE: gfx_release((uint32_t)t->pid); return 0;
    case GFX_POLL: {
        struct gfx_poll p;
        if (gfx_owner() != t->pid || copy_from_user(&p, a1, sizeof p) < 0 || !p.max) return -1;
        if (p.max > 64) p.max = 64;
        uint64_t deadline = uptime_ms() + (p.timeout_ms > 0 ? (uint64_t)p.timeout_ms : 0);
        while (!input_pending() && p.timeout_ms != 0 && (p.timeout_ms < 0 || uptime_ms() < deadline))
            sleep_ms(1);
        struct input_event ev[64];
        uint32_t n = 0;
        while (n < p.max && input_pop(&ev[n])) n++;
        if (n && copy_to_user(p.buf, ev, n * sizeof ev[0]) < 0) return -1;
        p.count = n;
        return copy_to_user(a1, &p, sizeof p);
    }
    }
    return -1;
}

static int64_t sys_sysinfo(uint64_t what, uint64_t index, uint64_t ubuf) {
    switch (what) {
    case SI_MEM: {
        struct si_mem m;
        uint64_t span, used, fr;
        heap_stats(&span, &used, &fr);
        m.total_kb = pmm_total_frames() * 4;
        m.free_kb = pmm_free_frames() * 4;
        m.heap_used = used;
        m.heap_free = fr;
        m.uptime_ms = uptime_ms();
        return copy_to_user(ubuf, &m, sizeof m);
    }
    case SI_TASK: {
        struct si_task t;
        memset(&t, 0, sizeof t);
        if (!task_info(index, &t)) return 0;
        return copy_to_user(ubuf, &t, sizeof t) < 0 ? -1 : 1;
    }
    case SI_DISK: {
        struct si_disk d;
        disk_stats(&d);
        return copy_to_user(ubuf, &d, sizeof d);
    }
    }
    return -1;
}

struct argv_buf {                       /* too big for the kernel stack, so it lives on the heap */
    char  strs[ARGV_MAX][ARG_LEN];
    char *ptrs[ARGV_MAX + 1];
};

/* Copy a user argv (array of string pointers ending in 0) into kernel memory. */
static int load_argv(uint64_t uargv, char strs[ARGV_MAX][ARG_LEN], char *ptrs[ARGV_MAX + 1]) {
    int n = 0;
    if (uargv) {
        for (; n < ARGV_MAX; n++) {
            uint64_t p;
            if (copy_from_user(&p, uargv + n * 8, 8) < 0) return -1;
            if (!p) break;
            if (copy_str_from_user(strs[n], p, ARG_LEN) < 0) return -1;
            ptrs[n] = strs[n];
        }
        if (n == ARGV_MAX) return -1;            /* too many arguments */
    }
    ptrs[n] = 0;
    return n;
}

int64_t syscall_dispatch(struct interrupt_frame *f) {
    uint64_t a0 = f->rdi, a1 = f->rsi, a2 = f->rdx;
    int64_t r = -1;

    switch (f->rax) {
    case SYS_WRITE:   r = sys_write(a0, a1, a2); break;
    case SYS_READ:    r = sys_read(a0, a1, a2); break;
    case SYS_OPEN:    r = sys_open(a0, a1); break;
    case SYS_CLOSE:   r = sys_close(a0); break;
    case SYS_LSEEK:   { struct file *fl = fd_file(a0); r = fl ? file_seek(fl, (int64_t)a1, (int)a2) : -1; break; }
    case SYS_STAT:    r = sys_stat(a0, a1); break;
    case SYS_READDIR: r = sys_readdir(a0, a1, a2); break;
    case SYS_DUP2:    r = sys_dup2(a0, a1); break;
    case SYS_PIPE:    r = sys_pipe(a0); break;
    case SYS_CHDIR:   r = sys_chdir(a0); break;
    case SYS_GETCWD:  r = sys_getcwd(a0, a1); break;
    case SYS_MKDIR:   r = sys_mkdir(a0); break;
    case SYS_UNLINK:  r = sys_unlink(a0); break;
    case SYS_SYSINFO: r = sys_sysinfo(a0, a1, a2); break;
    case SYS_NETCTL:  r = sys_netctl(a0, a1); break;
    case SYS_SOCKET:  r = sys_socket(a0); break;
    case SYS_BIND:    { struct file *fl = fd_file(a0); r = fl && sock_is_socket(fl) ? sock_bind(fl, (uint16_t)a1) : -1; break; }
    case SYS_CONNECT: r = sys_connect(a0, a1); break;
    case SYS_LISTEN:  { struct file *fl = fd_file(a0); r = fl && sock_is_socket(fl) ? sock_listen(fl, (int)a1) : -1; break; }
    case SYS_ACCEPT:  r = sys_accept(a0, a1); break;
    case SYS_SENDTO:  r = sys_sendto(a0, a1); break;
    case SYS_RECVFROM: r = sys_recvfrom(a0, a1); break;
    case SYS_SHUTDOWN: { struct file *fl = fd_file(a0); r = fl && sock_is_socket(fl) ? sock_shutdown(fl) : -1; break; }
    case SYS_IOCTL:   { struct file *fl = fd_file(a0); r = fl ? file_ioctl(fl, a1, a2) : -1; break; }
    case SYS_EXIT:    process_exit((int)a0);
#ifdef SMEP_TEST
    case SYS_GETPID:  ((void (*)(void))0x400000)(); break;     /* test switch: the kernel jumps into user code */
#else
    case SYS_GETPID:  r = (int64_t)current_task()->pid; break;
#endif
    case SYS_YIELD:   yield(); r = 0; break;
    case SYS_SLEEP:   sleep_ms(a0); r = 0; break;
    case SYS_BRK:     r = process_brk(a0); break;
    case SYS_MMAP:    r = process_mmap(a0); break;
    case SYS_MUNMAP:  r = process_munmap(a0, a1); break;
    case SYS_FORK:    r = process_fork(f); break;
    case SYS_UPTIME:  r = (int64_t)uptime_ms(); break;
    case SYS_TIME:    r = (int64_t)wall_time(); break;
    case SYS_GFX:     r = sys_gfx(a0, a1); break;
    case SYS_POWER:   if (a0 == POWER_OFF) power_off(); if (a0 == POWER_REBOOT) power_reboot(); r = -1; break;
    case SYS_KILL:    r = task_kill_child(current_task(), a0); break;
    case SYS_WAIT: {
        int status = 0;
        r = process_wait(&status, (int)a1 & WNOHANG);
        if (r >= 0 && a0 && copy_to_user(a0, &status, sizeof status) < 0) r = -1;
        break;
    }
    case SYS_EXEC: {
        char name[ARG_LEN];
        struct argv_buf *ab = kmalloc(sizeof *ab);
        if (!ab) { r = -1; break; }
        if (copy_str_from_user(name, a0, sizeof name) < 0 || load_argv(a1, ab->strs, ab->ptrs) < 0) { kfree(ab); r = -1; break; }
        r = process_exec(name, ab->ptrs, f);      /* on success the frame now points at the new program */
        kfree(ab);
        break;
    }
    default: r = -1;
    }
    if (current_task()->killed) process_exit(137);
    f->rax = (uint64_t)r;
    return r;
}
