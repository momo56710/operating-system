#include "task.h"
#include "sync.h"
#include "heap.h"
#include "pmm.h"
#include "kprintf.h"

/* Run with: make iso EXTRA=-DSYNC_TEST. Needs the scheduler and timer to be running. */

#define WORKERS 4
#define ITERS   500

static struct mutex mtx;
static struct sem done, items;
static volatile int shared_unsafe, shared_safe;
static volatile long consumed_sum;
static int failures;

#define CHECK(cond, msg) do { if (!(cond)) { kprintf("  FAIL: %s\n", msg); failures++; } } while (0)

/* Read, yield, write: the yield forces a task switch in the middle of the update. */
static void racer(void *arg) {
    int safe = (int)(uintptr_t)arg;
    for (int i = 0; i < ITERS; i++) {
        if (safe) {
            mutex_lock(&mtx);
            int v = shared_safe;
            yield();
            shared_safe = v + 1;
            mutex_unlock(&mtx);
        } else {
            int v = shared_unsafe;
            yield();
            shared_unsafe = v + 1;
        }
    }
    sem_post(&done);
}

static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 1000; i++) {
        sem_post(&items);
        if (i % 7 == 0) yield();
    }
    sem_post(&done);
}

static void consumer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 1000; i++) {
        sem_wait(&items);
        consumed_sum += i;
    }
    sem_post(&done);
}

static void short_lived(void *arg) { (void)arg; sem_post(&done); }   /* returns, so task_exit runs */

static void wait_done(int n) { for (int i = 0; i < n; i++) sem_wait(&done); }

void sync_selftest(void *arg) {
    (void)arg;
    mutex_init(&mtx);
    sem_init(&done, 0);
    sem_init(&items, 0);
    kprintf("\nSync self-test:\n");

    for (int i = 0; i < WORKERS; i++) task_create("racer-unsafe", racer, (void *)0);
    wait_done(WORKERS);
    for (int i = 0; i < WORKERS; i++) task_create("racer-safe", racer, (void *)1);
    wait_done(WORKERS);
    kprintf("  shared counter, no lock: %d of %d (lost updates are the point)\n",
            shared_unsafe, WORKERS * ITERS);
    kprintf("  shared counter, mutex:   %d of %d\n", shared_safe, WORKERS * ITERS);
    CHECK(shared_safe == WORKERS * ITERS, "mutex-protected counter lost updates");

    task_create("consumer", consumer, 0);
    task_create("producer", producer, 0);
    wait_done(2);
    CHECK(consumed_sum == 1000L * 1001 / 2, "semaphore producer/consumer lost or duplicated items");
    kprintf("  producer/consumer: consumed sum %ld (expected %ld)\n", consumed_sum, 1000L * 1001 / 2);

    uint64_t span, used0, used1, fr;
    sleep_ms(100);                                   /* let the idle task reap the tasks above first */
    heap_stats(&span, &used0, &fr);
    uint64_t frames0 = pmm_free_frames();
    for (int i = 0; i < 20; i++) task_create("short", short_lived, 0);
    wait_done(20);
    sleep_ms(100);                                   /* give the idle task time to reap them */
    heap_stats(&span, &used1, &fr);
    CHECK(used1 == used0, "heap bytes not returned after 20 tasks exited");
    kprintf("  20 tasks exited: heap used %lu -> %lu, free frames %lu -> %lu\n",
            used0, used1, frames0, pmm_free_frames());

    uint64_t t0 = uptime_ms();
    sleep_ms(100);
    uint64_t dt = uptime_ms() - t0;
    CHECK(dt >= 100 && dt <= 115, "sleep_ms(100) outside 100-115 ms");
    kprintf("  sleep_ms(100) took %lu ticks\n", dt);

    kprintf("Sync self-test: %s\n", failures ? "FAILED" : "PASSED");
}
