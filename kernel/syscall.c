#include "syscall.h"
#include "process.h"
#include "task.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"

/* ---- checked access to user memory ---- */

/* Is [addr, addr+n) entirely mapped, user-accessible (and writable if `write`)? */
static int user_range_ok(uint64_t addr, size_t n, int write) {
    if (!n) return 1;
    if (addr < 0x1000 || addr >= USER_MAX || n > USER_MAX - addr) return 0;
    uint64_t pml4 = current_task()->pml4;
    if (!pml4) return 0;
    for (uint64_t page = addr & ~4095ULL; page < addr + n; page += 4096) {
        uint64_t flags;
        if (!vmm_lookup(pml4, page, &flags)) return 0;
        if (!(flags & VMM_USER)) return 0;
        if (write && !(flags & VMM_WRITE)) return 0;
    }
    return 1;
}

int copy_from_user(void *dst, uint64_t usrc, size_t n) {
    if (!user_range_ok(usrc, n, 0)) return -1;
    memcpy(dst, (const void *)usrc, n);
    return 0;
}

int copy_to_user(uint64_t udst, const void *src, size_t n) {
    if (!user_range_ok(udst, n, 1)) return -1;
    memcpy((void *)udst, src, n);
    return 0;
}

int copy_str_from_user(char *dst, uint64_t usrc, size_t max) {
    for (size_t i = 0; i < max; i++) {
        if (!user_range_ok(usrc + i, 1, 0)) return -1;
        dst[i] = *(const char *)(usrc + i);
        if (!dst[i]) return 0;
    }
    return -1;                                   /* no terminator within max bytes */
}

/* ---- the calls ---- */

#define ARGV_MAX 16
#define ARG_LEN  256

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd != 1 && fd != 2) return -1;
    char chunk[256];
    uint64_t done = 0;
    while (done < len) {
        size_t n = len - done < sizeof chunk ? len - done : sizeof chunk;
        if (copy_from_user(chunk, buf + done, n) < 0) return done ? (int64_t)done : -1;
        kwrite(chunk, n);
        done += n;
    }
    return (int64_t)done;
}

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
    case SYS_WRITE:  r = sys_write(a0, a1, a2); break;
    case SYS_EXIT:   process_exit((int)a0);
    case SYS_GETPID: r = (int64_t)current_task()->pid; break;
    case SYS_YIELD:  yield(); r = 0; break;
    case SYS_SLEEP:  sleep_ms(a0); r = 0; break;
    case SYS_BRK:    r = process_brk(a0); break;
    case SYS_MMAP:   r = process_mmap(a0); break;
    case SYS_MUNMAP: r = process_munmap(a0, a1); break;
    case SYS_FORK:   r = process_fork(f); break;
    case SYS_UPTIME: r = (int64_t)uptime_ms(); break;
    case SYS_WAIT: {
        int status = 0;
        r = process_wait(&status);
        if (r >= 0 && a0 && copy_to_user(a0, &status, sizeof status) < 0) r = -1;
        break;
    }
    case SYS_EXEC: {
        char name[ARG_LEN];
        char strs[ARGV_MAX][ARG_LEN], *ptrs[ARGV_MAX + 1];
        if (copy_str_from_user(name, a0, sizeof name) < 0 || load_argv(a1, strs, ptrs) < 0) { r = -1; break; }
        r = process_exec(name, ptrs, f);          /* on success the frame now points at the new program */
        break;
    }
    default: r = -1;
    }
    f->rax = (uint64_t)r;
    return r;
}
