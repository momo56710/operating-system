#include "task.h"
#include "spinlock.h"
#include "heap.h"
#include "string.h"
#include "panic.h"
#include "kprintf.h"
#include "vmm.h"
#include "gdt.h"
#include "vfs.h"
#include "cpu.h"
#include "kstack.h"

#define QUANTUM_TICKS 10          /* 10 ms time slice at 1000 Hz */

extern void switch_to(uint64_t *save_rsp, uint64_t new_rsp);
extern void task_trampoline(void);

static struct task idle_task;     /* the boot context: pid 0, runs only when nothing else can */
static struct task *current = &idle_task;
static uint64_t next_pid = 1;
static volatile uint64_t ticks;
static uint64_t loaded_pml4;

extern uint64_t syscall_kstack;   /* arch/x86_64/syscall.c: where `syscall` puts the kernel stack */

struct task *current_task(void) { return current; }
uint64_t uptime_ms(void) { return ticks; }

void sched_init(void) {
    idle_task.pid = 0;
    idle_task.state = TASK_RUNNING;
    idle_task.next = &idle_task;
    idle_task.kslot = -1;
    idle_task.quantum = QUANTUM_TICKS;
    loaded_pml4 = vmm_kernel_pml4();
    memcpy(idle_task.name, "idle", 5);
}

struct task *task_create(const char *name, void (*fn)(void *), void *arg) {
    return task_create_ex(name, fn, arg, 0);
}

struct task *task_create_ex(const char *name, void (*fn)(void *), void *arg, const struct task_init *init) {
    struct task *t = kmalloc(sizeof *t);
    uint64_t top;
    int slot = t ? kstack_alloc(&top) : -1;
    if (!t || slot < 0) panic("task_create: out of memory");
    memset(t, 0, sizeof *t);
    t->cwd[0] = '/';
    memcpy(t->fpu, fpu_default_state, sizeof t->fpu);

    /* Fake the frame switch_to() expects to pop, ending in a `ret` to the trampoline. */
    uint64_t *sp = (uint64_t *)top - 7;
    sp[0] = 0;                        /* r15 */
    sp[1] = 0;                        /* r14 */
    sp[2] = (uint64_t)arg;            /* r13: argument for fn */
    sp[3] = (uint64_t)fn;             /* r12: entry point */
    sp[4] = 0;                        /* rbp */
    sp[5] = 0;                        /* rbx */
    sp[6] = (uint64_t)task_trampoline;

    t->rsp = (uint64_t)sp;
    t->kslot = slot;
    t->kstack_top = top;
    if (init) {
        t->pml4 = init->pml4;
        t->brk_start = init->brk_start;
        t->brk = init->brk;
        t->mmap_next = init->mmap_next;
        t->parent = init->parent;
        if (init->fpu_from) memcpy(t->fpu, init->fpu_from->fpu, sizeof t->fpu);
        if (init->inherit) {
            for (int i = 0; i < FD_MAX; i++)
                if (init->inherit->fds[i]) t->fds[i] = file_dup(init->inherit->fds[i]);
            memcpy(t->cwd, init->inherit->cwd, CWD_MAX);
        }
    }
    t->state = TASK_READY;
    t->quantum = QUANTUM_TICKS;
    for (int i = 0; i < 15 && name[i]; i++) t->name[i] = name[i];

    uint64_t flags = irq_save();
    t->pid = next_pid++;
    t->next = idle_task.next;         /* insert right after idle in the circular list */
    idle_task.next = t;
    irq_restore(flags);
    return t;
}

/* Next READY task after `prev` in round-robin order, or idle if nothing can run. */
static struct task *pick_next(struct task *prev) {
    struct task *t = prev->next;
    for (;;) {
        if (t != &idle_task && t->state == TASK_READY) return t;
        if (t == prev) break;
        t = t->next;
    }
    return &idle_task;
}

void schedule(void) {                 /* interrupts are off */
    struct task *prev = current;
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    struct task *next = pick_next(prev);
    next->state = TASK_RUNNING;
    next->quantum = QUANTUM_TICKS;
    if (next == prev) return;
    current = next;
    if (next->kstack_top) {                       /* ring 3 -> ring 0 entries land on this stack */
        tss_set_rsp0(next->kstack_top);
        syscall_kstack = next->kstack_top;
    }
    uint64_t want = next->pml4 ? next->pml4 : vmm_kernel_pml4();
    if (want != loaded_pml4) { vmm_switch(want); loaded_pml4 = want; }
    if (prev->pml4) fpu_save(prev->fpu);          /* user programs may use SSE; the kernel never does */
    if (next->pml4) fpu_restore(next->fpu);
    switch_to(&prev->rsp, next->rsp);
}

void sched_tick(void) {               /* timer interrupt, interrupts off */
    ticks++;
    for (struct task *t = idle_task.next; t != &idle_task; t = t->next)
        if (t->state == TASK_SLEEPING && t->wake_tick <= ticks) t->state = TASK_READY;
    if (current == &idle_task || --current->quantum <= 0)
        schedule();
}

void yield(void) {
    uint64_t flags = irq_save();
    schedule();
    irq_restore(flags);
}

void sleep_ms(uint64_t ms) {
    if (current == &idle_task) panic("the idle task cannot sleep");
    uint64_t flags = irq_save();
    current->wake_tick = ticks + (ms ? ms : 1);
    current->state = TASK_SLEEPING;
    schedule();
    irq_restore(flags);
}

void task_set_space(uint64_t pml4) {
    loaded_pml4 = pml4;
    vmm_switch(pml4);
}

void task_use_kernel_space(void) {
    task_set_space(vmm_kernel_pml4());
}

/* First child (any, or only one that already exited) of `parent`, or 0. Interrupts must be off. */
struct task *task_find_child(struct task *parent, int defunct_only) {
    for (struct task *t = idle_task.next; t != &idle_task; t = t->next)
        if (t->parent == parent && (!defunct_only || t->state == TASK_DEFUNCT)) return t;
    return 0;
}

/* The parent is going away: children become parentless, and already-finished ones can be freed. */
void task_orphan_children(struct task *parent) {
    for (struct task *t = idle_task.next; t != &idle_task; t = t->next) {
        if (t->parent != parent) continue;
        t->parent = 0;
        if (t->state == TASK_DEFUNCT) t->state = TASK_ZOMBIE;
    }
}

void task_exit(void) {
    irq_save();
    task_orphan_children(current);
    current->state = TASK_ZOMBIE;
    schedule();
    panic("a zombie task was scheduled again");
}

/* Free the stacks of finished tasks. Runs from the idle loop, never on the dead task's own stack. */
void reap_zombies(void) {
    for (;;) {
        struct task *dead = 0, *prev = &idle_task;
        uint64_t flags = irq_save();
        for (struct task *t = idle_task.next; t != &idle_task; prev = t, t = t->next)
            if (t->state == TASK_ZOMBIE) { dead = t; prev->next = t->next; break; }
        irq_restore(flags);
        if (!dead) return;
        kstack_free(dead->kslot);
        kfree(dead);
    }
}

int task_info(uint64_t index, struct si_task *out) {
    int found = 0;
    uint64_t flags = irq_save();
    struct task *t = &idle_task;
    do {
        if (index-- == 0) {
            out->pid = t->pid;
            out->parent_pid = t->parent ? t->parent->pid : 0;
            out->state = t->state;
            out->is_user = t->pml4 != 0;
            memcpy(out->name, t->name, sizeof out->name);
            found = 1;
            break;
        }
        t = t->next;
    } while (t != &idle_task);
    irq_restore(flags);
    return found;
}

void task_print_all(void) {
    static const char *names[] = { "running", "ready", "sleeping", "blocked", "zombie", "defunct" };
    kprintf("PID  STATE     NAME\n");
    uint64_t flags = irq_save();
    struct task *t = &idle_task;
    do {
        kprintf("%-4lu %-9s %s\n", t->pid, names[t->state], t->name);
        t = t->next;
    } while (t != &idle_task);
    irq_restore(flags);
}

int task_kill_child(struct task *parent, uint64_t pid) {
    int r = -1;
    uint64_t flags = irq_save();
    struct task *t = &idle_task;
    do {
        if (t->pid == pid && t->parent == parent && t->pml4 && t->state != TASK_ZOMBIE && t->state != TASK_DEFUNCT) {
            t->killed = 1;
            if (t->state == TASK_SLEEPING) t->wake_tick = 0;      /* wake it so it can notice */
            r = 0;
            break;
        }
        t = t->next;
    } while (t != &idle_task);
    irq_restore(flags);
    return r;
}
