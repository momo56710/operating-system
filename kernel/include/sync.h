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
