#include "sync.h"
#include "panic.h"

void sem_init(struct sem *s, int count) {
    s->lock.locked = 0;
    s->count = count;
    s->head = s->tail = 0;
}

void sem_wait(struct sem *s) {
    uint64_t flags = spin_lock_irqsave(&s->lock);
    if (s->count > 0) {
        s->count--;
        spin_unlock_irqrestore(&s->lock, flags);
        return;
    }
    struct task *me = current_task();
    me->wait_next = 0;
    if (s->tail) s->tail->wait_next = me; else s->head = me;
    s->tail = me;
    me->state = TASK_BLOCKED;
    spin_unlock(&s->lock);            /* interrupts stay off until we are switched back in */
    schedule();
    irq_restore(flags);
}

void sem_post(struct sem *s) {
    uint64_t flags = spin_lock_irqsave(&s->lock);
    struct task *t = s->head;
    if (t) {                          /* hand the token straight to the oldest waiter */
        s->head = t->wait_next;
        if (!s->head) s->tail = 0;
        t->state = TASK_READY;
    } else {
        s->count++;
    }
    spin_unlock_irqrestore(&s->lock, flags);
}

void mutex_init(struct mutex *m) {
    sem_init(&m->sem, 1);
    m->owner = 0;
}

void mutex_lock(struct mutex *m) {
    if (m->owner == current_task()) panic("mutex_lock: task %s already holds it", current_task()->name);
    sem_wait(&m->sem);
    m->owner = current_task();
}

void mutex_unlock(struct mutex *m) {
    if (m->owner != current_task()) panic("mutex_unlock: not the owner");
    m->owner = 0;
    sem_post(&m->sem);
}

void waitq_block(struct waitq *q, spinlock_t *lock, uint64_t flags) {
    struct task *me = current_task();
    me->wait_next = q->head;
    q->head = me;
    me->state = TASK_BLOCKED;
    spin_unlock(lock);                 /* interrupts are still off, so nothing can slip in before we sleep */
    schedule();
    irq_restore(flags);
}

void waitq_wake_all(struct waitq *q) {
    for (struct task *t = q->head; t; ) {
        struct task *next = t->wait_next;
        t->state = TASK_READY;
        t = next;
    }
    q->head = 0;
}
