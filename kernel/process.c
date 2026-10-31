#include "process.h"
#include "task.h"
#include "elf.h"
#include "modules.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "kprintf.h"
#include "panic.h"
#include "spinlock.h"
#include "string.h"
#include "vfs.h"
#include "cpu.h"

#define PAGE        4096ULL
#define STACK_TOP   0x00007FFFFFFFF000ULL     /* just above the highest stack byte; the page above stays unmapped */
#define STACK_PAGES 32                        /* 128 KiB */
#define MMAP_BASE   0x0000000100000000ULL
#define MMAP_MAX    (256ULL << 20)            /* per call */
#define BRK_LIMIT   0x00000000F0000000ULL
#define ARGS_MAX    32

extern void enter_user(uint64_t rip, uint64_t rsp) __attribute__((noreturn));
extern void resume_user(struct interrupt_frame *f) __attribute__((noreturn));

static inline uint64_t page_up(uint64_t v) { return (v + PAGE - 1) & ~(PAGE - 1); }

static int map_user_page(uint64_t pml4, uint64_t va) {
    uint64_t phys = pmm_alloc_frame();
    if (!phys) return -1;
    memset(phys_to_virt(phys), 0, PAGE);
    if (vmm_map_in(pml4, va, phys, VMM_USER | VMM_WRITE | VMM_NX) < 0) { pmm_free_frame(phys); return -1; }
    return 0;
}

/* Write into another address space through its physical pages (no need to switch to it). */
static int user_copy_out(uint64_t pml4, uint64_t va, const void *src, size_t n) {
    const uint8_t *s = src;
    while (n) {
        uint64_t phys = vmm_lookup(pml4, va & ~(PAGE - 1), 0);
        if (!phys) return -1;
        size_t off = va & (PAGE - 1), chunk = PAGE - off < n ? PAGE - off : n;
        memcpy((uint8_t *)phys_to_virt(phys) + off, s, chunk);
        va += chunk; s += chunk; n -= chunk;
    }
    return 0;
}

static int map_stack(uint64_t pml4) {
    for (int i = 1; i <= STACK_PAGES; i++)
        if (map_user_page(pml4, STACK_TOP - i * PAGE) < 0) return -1;
    return 0;
}

/* System V entry layout: rsp -> argc, argv[0..argc-1], NULL, envp (just NULL). */
static int build_user_stack(uint64_t pml4, char *const argv[], uint64_t *rsp_out) {
    int argc = 0;
    while (argv && argv[argc]) argc++;
    if (argc > ARGS_MAX) return -1;

    uint64_t addr[ARGS_MAX], sp = STACK_TOP, total = 0;
    for (int i = 0; i < argc; i++) total += strlen(argv[i]) + 1;
    if (total > 8192) return -1;
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        if (user_copy_out(pml4, sp, argv[i], len) < 0) return -1;
        addr[i] = sp;
    }
    uint64_t words = 1 + argc + 1 + 1;
    sp = (sp - words * 8) & ~15ULL;
    uint64_t vec[ARGS_MAX + 3] = { (uint64_t)argc };
    for (int i = 0; i < argc; i++) vec[1 + i] = addr[i];
    vec[1 + argc] = 0;
    vec[2 + argc] = 0;
    if (user_copy_out(pml4, sp, vec, words * 8) < 0) return -1;
    *rsp_out = sp;
    return 0;
}

#define MAX_PROGRAM (8ULL << 20)

/* Read a whole executable into kernel memory. `path` is relative to the caller's working directory. */
static void *read_program(const char *path, size_t *size) {
    char abs[PATH_MAX_LEN];
    if (vfs_normalize(current_task()->cwd, path, abs, sizeof abs) < 0) return 0;
    struct file *f = file_open(abs, O_RDONLY);
    if (!f) return 0;
    uint64_t total = file_size(f);
    void *buf = 0;
    if (f->vn->type == FT_FILE && total && total <= MAX_PROGRAM && (buf = kmalloc(total))) {
        uint64_t got = 0;
        while (got < total) {
            int64_t r = file_read(f, (uint8_t *)buf + got, total - got);
            if (r <= 0) break;
            got += r;
        }
        if (got != total) { kfree(buf); buf = 0; }
    }
    file_close(f);
    if (buf) *size = total;
    return buf;
}

/* Builds a complete address space for the executable at `path`; the three out-parameters
 * are valid on success. */
static uint64_t load_program(const char *path, char *const argv[],
                             uint64_t *entry, uint64_t *rsp, uint64_t *image_end) {
    size_t size;
    void *img = read_program(path, &size);
    if (!img) return 0;
    uint64_t pml4 = vmm_new_space();
    if (!pml4) { kfree(img); return 0; }
    int bad = elf_load(pml4, img, size, entry, image_end) < 0 ||
              map_stack(pml4) < 0 ||
              build_user_stack(pml4, argv, rsp) < 0;
    kfree(img);
    if (bad) { vmm_free_space(pml4); return 0; }
    return pml4;
}

static const char *base_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

struct start_info { uint64_t entry, rsp; };

static void user_start(void *arg) {
    struct start_info si = *(struct start_info *)arg;
    kfree(arg);
    enter_user(si.entry, si.rsp);
}

long process_spawn(const char *path, char *const argv[]) {
    uint64_t entry, rsp, end;
    uint64_t pml4 = load_program(path, argv, &entry, &rsp, &end);
    if (!pml4) return -1;
    struct start_info *si = kmalloc(sizeof *si);
    if (!si) { vmm_free_space(pml4); return -1; }
    si->entry = entry;
    si->rsp = rsp;
    struct task_init ti = { pml4, end, end, MMAP_BASE, current_task(), current_task(), 0 };
    return (long)task_create_ex(base_name(path), user_start, si, &ti)->pid;
}

void process_exit(int code) {
    struct task *t = current_task();
    for (int i = 0; i < FD_MAX; i++)             /* closing the last write end of a pipe is what lets its reader see end of file */
        if (t->fds[i]) { file_close(t->fds[i]); t->fds[i] = 0; }
    console_set_cooked();                        /* a program that dies in raw mode must not leave the console that way */
    irq_save();
    uint64_t pml4 = t->pml4;
    task_use_kernel_space();          /* never free the page tables the CPU is using */
    t->pml4 = 0;
    if (pml4) vmm_free_space(pml4);
    task_orphan_children(t);
    t->exit_code = code;
    if (t->parent) {                  /* keep the exit status until the parent collects it */
        t->state = TASK_DEFUNCT;
        if (t->parent->waiting) { t->parent->waiting = 0; t->parent->state = TASK_READY; }
    } else {
        t->state = TASK_ZOMBIE;
    }
    schedule();
    panic("an exited process was scheduled again");
}

long process_wait(int *status) {
    struct task *me = current_task();
    for (;;) {
        uint64_t f = irq_save();
        struct task *c = task_find_child(me, 1);
        if (c) {
            long pid = (long)c->pid;
            *status = c->exit_code;
            c->parent = 0;
            c->state = TASK_ZOMBIE;   /* now the idle task may free its stack */
            irq_restore(f);
            return pid;
        }
        if (!task_find_child(me, 0)) { irq_restore(f); return -1; }
        me->waiting = 1;
        me->state = TASK_BLOCKED;
        schedule();
        irq_restore(f);
    }
}

void process_fault(struct interrupt_frame *f, const char *what) {
    struct task *t = current_task();
    if (f->vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf("[kernel] process %lu (%s) killed: %s at address %lx, rip=%lx\n", t->pid, t->name, what, cr2, f->rip);
    } else {
        kprintf("[kernel] process %lu (%s) killed: %s, rip=%lx\n", t->pid, t->name, what, f->rip);
    }
    process_exit(PROC_FAULT_BASE + (int)f->vector);
}

static void fork_child(void *arg) {
    struct interrupt_frame frame = *(struct interrupt_frame *)arg;
    kfree(arg);
    resume_user(&frame);
}

long process_fork(struct interrupt_frame *f) {
    struct task *me = current_task();
    if (!me->pml4) return -1;
    uint64_t pml4 = vmm_clone_space(me->pml4);
    if (!pml4) return -1;
    struct interrupt_frame *copy = kmalloc(sizeof *copy);
    if (!copy) { vmm_free_space(pml4); return -1; }
    *copy = *f;
    copy->rax = 0;                    /* fork returns 0 in the child */
    fpu_save(me->fpu);                /* the child starts with the parent's floating-point registers */
    struct task_init ti = { pml4, me->brk_start, me->brk, me->mmap_next, me, me, me };
    return (long)task_create_ex(me->name, fork_child, copy, &ti)->pid;
}

long process_exec(const char *path, char *const argv[], struct interrupt_frame *f) {
    struct task *me = current_task();
    uint64_t entry, rsp, end;
    uint64_t pml4 = load_program(path, argv, &entry, &rsp, &end);
    if (!pml4) return -1;             /* the old image is untouched */

    uint64_t flags = irq_save();
    uint64_t old = me->pml4;
    me->pml4 = pml4;
    task_set_space(pml4);
    memcpy(me->fpu, fpu_default_state, sizeof me->fpu);      /* a new program starts with clean FPU/SSE registers */
    fpu_restore(me->fpu);
    irq_restore(flags);
    if (old) vmm_free_space(old);

    me->brk_start = me->brk = end;
    me->mmap_next = MMAP_BASE;
    const char *nm = base_name(path);
    for (int i = 0; i < 15; i++) me->name[i] = nm[i];
    me->name[15] = 0;

    memset(f, 0, sizeof *f);          /* a fresh register state for the new program */
    f->rip = entry;
    f->rsp = rsp;
    f->rflags = 0x202;
    f->cs = 0x23;
    f->ss = 0x1B;
    return 0;
}

long process_brk(uint64_t addr) {
    struct task *t = current_task();
    if (!t->pml4) return -1;
    if (!addr) return (long)t->brk;
    if (addr < t->brk_start || addr > BRK_LIMIT) return -1;

    uint64_t cur = page_up(t->brk), want = page_up(addr);
    for (uint64_t va = cur; va < want; va += PAGE) {
        if (map_user_page(t->pml4, va) < 0) {
            for (uint64_t u = cur; u < va; u += PAGE) pmm_free_frame(vmm_unmap_in(t->pml4, u));
            return -1;
        }
    }
    for (uint64_t va = want; va < cur; va += PAGE) {
        uint64_t phys = vmm_unmap_in(t->pml4, va);
        if (phys) pmm_free_frame(phys);
    }
    t->brk = addr;
    return (long)addr;
}

long process_mmap(uint64_t len) {
    struct task *t = current_task();
    if (!t->pml4 || !len || len > MMAP_MAX) return -1;
    uint64_t base = t->mmap_next, pages = page_up(len) / PAGE;
    if (base + (pages + 1) * PAGE >= USER_MAX) return -1;
    for (uint64_t i = 0; i < pages; i++) {
        if (map_user_page(t->pml4, base + i * PAGE) < 0) {
            for (uint64_t j = 0; j < i; j++) pmm_free_frame(vmm_unmap_in(t->pml4, base + j * PAGE));
            return -1;
        }
    }
    t->mmap_next = base + (pages + 1) * PAGE;      /* one unmapped guard page after every mapping */
    return (long)base;
}

long process_munmap(uint64_t addr, uint64_t len) {
    struct task *t = current_task();
    if (!t->pml4 || (addr & (PAGE - 1)) || addr < MMAP_BASE || addr >= t->mmap_next || !len) return -1;
    uint64_t end = page_up(addr + len);
    if (end > t->mmap_next) return -1;
    for (uint64_t va = addr; va < end; va += PAGE) {
        uint64_t phys = vmm_unmap_in(t->pml4, va);
        if (phys) pmm_free_frame(phys);
    }
    return 0;
}
