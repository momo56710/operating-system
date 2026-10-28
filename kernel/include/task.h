#pragma once
#include <stdint.h>

enum task_state { TASK_RUNNING, TASK_READY, TASK_SLEEPING, TASK_BLOCKED, TASK_ZOMBIE };

struct task {
    uint64_t        rsp;          /* saved kernel stack pointer; must stay the first field */
    uint64_t        pid;
    enum task_state state;
    uint64_t        wake_tick;    /* when sleeping */
    int             quantum;      /* ticks left in this time slice */
    void           *stack;
    struct task    *next;         /* circular list of all tasks */
    struct task    *wait_next;    /* link in a semaphore wait queue */
    char            name[16];
};

void         sched_init(void);    /* turns the boot context into task 0, the idle task */
struct task *task_create(const char *name, void (*fn)(void *), void *arg);
void         task_exit(void);
void         yield(void);
void         sleep_ms(uint64_t ms);
void         schedule(void);      /* interrupts must be off */
void         sched_tick(void);    /* called from the timer interrupt */
void         reap_zombies(void);
uint64_t     uptime_ms(void);
struct task *current_task(void);
void         task_print_all(void);   /* for the shell's ps command */
