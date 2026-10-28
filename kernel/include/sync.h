#pragma once
#include "spinlock.h"
#include "task.h"

struct sem {
    spinlock_t   lock;
    int          count;
    struct task *head, *tail;   /* tasks waiting, FIFO */
};

struct mutex {
    struct sem   sem;
    struct task *owner;
};

void sem_init(struct sem *s, int count);
void sem_wait(struct sem *s);   /* may block; never call from an interrupt handler */
void sem_post(struct sem *s);   /* safe from interrupt handlers */

void mutex_init(struct mutex *m);
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);

/* A list of tasks waiting for something to change (used by pipes). The caller keeps its own
 * lock; waitq_block releases it atomically with going to sleep. */
struct waitq { struct task *head; };

/* Call with `lock` held and `flags` from spin_lock_irqsave. Sleeps until waitq_wake_all, then
 * returns with the lock released and interrupts restored: re-take the lock and re-check. */
void waitq_block(struct waitq *q, spinlock_t *lock, uint64_t flags);
void waitq_wake_all(struct waitq *q);   /* call with the lock held */
