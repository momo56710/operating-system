#pragma once
#include <stdint.h>
#include "vfs.h"

enum task_state {
    TASK_RUNNING, TASK_READY, TASK_SLEEPING, TASK_BLOCKED,
    TASK_ZOMBIE,      /* finished and unwanted: the idle task frees its stack */
    TASK_DEFUNCT      /* a process that exited but whose parent has not wait()ed yet */
};

struct task {
    uint64_t        rsp;          /* saved kernel stack pointer; must stay the first field */
    uint64_t        pid;
    enum task_state state;
    uint64_t        wake_tick;    /* when sleeping */
    int             quantum;      /* ticks left in this time slice */
    int             kslot;        /* which guarded kernel stack it owns (see kstack.c); -1 for the boot task */
    uint64_t        kstack_top;   /* where the CPU starts the kernel stack on entry from ring 3 */
    struct task    *next;         /* circular list of all tasks */
    struct task    *wait_next;    /* link in a semaphore wait queue */
    char            name[16];

    /* User processes only (pml4 == 0 means a plain kernel thread) */
    uint64_t        pml4;         /* physical address of this process's top-level page table */
    uint64_t        brk_start, brk, mmap_next;
    struct task    *parent;
    int             exit_code;
    int             waiting;      /* blocked in wait() */
    int             killed;       /* kill() was called: the process dies when it next runs in user mode */
    struct file    *fds[FD_MAX];  /* open files; 0, 1, 2 are stdin, stdout, stderr */
    char            cwd[CWD_MAX];
    uint8_t         fpu[512] __attribute__((aligned(16)));   /* FXSAVE area; only user tasks use it */
};

struct task_init {                /* optional extra setup for task_create_ex */
    uint64_t     pml4, brk_start, brk, mmap_next;
    struct task *parent;
    struct task *inherit;         /* copy this task's open files and working directory */
    struct task *fpu_from;        /* start with a copy of this task's FPU/SSE registers (fork), else a clean state */
};

void         sched_init(void);    /* turns the boot context into task 0, the idle task */
struct task *task_create(const char *name, void (*fn)(void *), void *arg);
struct task *task_create_ex(const char *name, void (*fn)(void *), void *arg, const struct task_init *init);
void         task_exit(void);
void         yield(void);
void         sleep_ms(uint64_t ms);
void         schedule(void);      /* interrupts must be off */
void         sched_tick(void);    /* called from the timer interrupt */
void         reap_zombies(void);
uint64_t     uptime_ms(void);
struct task *current_task(void);
void         task_print_all(void);   /* for the shell's ps command */
void         task_set_space(uint64_t pml4);   /* load these page tables and remember it */
void         task_use_kernel_space(void);     /* load the kernel's own (before freeing a process's) */
int          task_info(uint64_t index, struct si_task *out);   /* for sysinfo(SI_TASK) */
struct task *task_find_child(struct task *parent, int defunct_only);
void         task_orphan_children(struct task *parent);
int          task_kill_child(struct task *parent, uint64_t pid);   /* 0 ok, -1 if pid is not a live child */
