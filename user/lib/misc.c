#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <assert.h>
#include <errno.h>
#include "sys.h"

int errno;

/* ---- process ---- */

#define MAX_ATEXIT 32
static void (*exit_handlers[MAX_ATEXIT])(void);
static int nexit;
extern void __stdio_flush_all(void);

int atexit(void (*fn)(void)) { if (nexit == MAX_ATEXIT) return -1; exit_handlers[nexit++] = fn; return 0; }

void exit(int code) {
    while (nexit) exit_handlers[--nexit]();
    __stdio_flush_all();
    _exit(code);
}

void abort(void) {
    __stdio_flush_all();
    write(2, "Aborted\n", 8);
    _exit(134);
}

void __assert_fail(const char *expr, const char *file, int line) {
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line);
    abort();
}

/* LeOS does not pass an environment to programs yet, so there is a built-in set of defaults, and
 * setenv() changes only this process's copy. */
#define MAX_ENV 32
static struct { char name[32]; char value[128]; } env_table[MAX_ENV] = {
    { "PATH", "/bin:/disk/bin" }, { "HOME", "/" }, { "TERM", "leos" }, { "USER", "root" }, { "SHELL", "/bin/sh" } };
static int env_count = 5;

char *getenv(const char *name) {
    for (int i = 0; i < env_count; i++) if (!strcmp(env_table[i].name, name)) return env_table[i].value;
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=') || strlen(name) >= 32 || strlen(value) >= 128) { errno = EINVAL; return -1; }
    for (int i = 0; i < env_count; i++)
        if (!strcmp(env_table[i].name, name)) {
            if (overwrite) strcpy(env_table[i].value, value);
            return 0;
        }
    if (env_count == MAX_ENV) { errno = ENOMEM; return -1; }
    strcpy(env_table[env_count].name, name);
    strcpy(env_table[env_count].value, value);
    env_count++;
    return 0;
}

int unsetenv(const char *name) {
    for (int i = 0; i < env_count; i++)
        if (!strcmp(env_table[i].name, name)) { env_table[i] = env_table[--env_count]; break; }
    return 0;
}

/* system(): run the command line through the shell (sh -c). */
int system(const char *cmd) {
    if (!cmd) return 1;                                                  /* yes, a shell is available */
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = { "sh", "-c", (char *)cmd, 0 };
        exec("/bin/sh", argv);
        _exit(127);
    }
    int status = 0;
    if (wait(&status) < 0) return -1;
    return status >= 1000 ? 128 + (status - 1000) : status;              /* the exit code itself; 128+N if killed by exception N */
}

sighandler_t signal(int sig, sighandler_t h) { (void)sig; (void)h; return SIG_DFL; }
int raise(int sig) { if (sig == SIGABRT) abort(); return 0; }

int isatty(int fd) { return ioctl(fd, TTY_SIZE, 0) >= 0; }

/* ---- locale: only "C" ---- */

char *setlocale(int category, const char *locale) {
    (void)category;
    if (!locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX")) return "C";
    return 0;
}

struct lconv *localeconv(void) {
    static struct lconv lc = { ".", "", "", "", "", "", "", "", "", "" };
    return &lc;
}

/* ---- numbers ---- */

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

static unsigned long rand_state = 1;
void srand(unsigned seed) { rand_state = seed; }
int rand(void) { rand_state = rand_state * 6364136223846793005UL + 1442695040888963407UL; return (int)((rand_state >> 33) & 0x7FFFFFFF); }

/* ---- sorting and searching ---- */

static void swap_bytes(char *a, char *b, size_t n) { while (n--) { char t = *a; *a++ = *b; *b++ = t; } }

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    char *a = base;
    while (n > 1) {
        if (n < 8) {                                                      /* small: insertion sort */
            for (size_t i = 1; i < n; i++)
                for (size_t j = i; j > 0 && cmp(a + (j - 1) * size, a + j * size) > 0; j--)
                    swap_bytes(a + (j - 1) * size, a + j * size, size);
            return;
        }
        swap_bytes(a, a + (n / 2) * size, size);                          /* the middle element is the pivot */
        size_t i = 0, j = n;
        for (;;) {
            do i++; while (i < n && cmp(a + i * size, a) < 0);
            do j--; while (cmp(a + j * size, a) > 0);
            if (i >= j) break;
            swap_bytes(a + i * size, a + j * size, size);
        }
        swap_bytes(a, a + j * size, size);
        if (j < n - j - 1) { qsort(a, j, size, cmp); a += (j + 1) * size; n -= j + 1; }    /* recurse into the smaller part */
        else               { qsort(a + (j + 1) * size, n - j - 1, size, cmp); n = j; }
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    const char *lo = base;
    while (n) {
        const char *mid = lo + (n / 2) * size;
        int c = cmp(key, mid);
        if (!c) return (void *)mid;
        if (c > 0) { lo = mid + size; n -= n / 2 + 1; } else n /= 2;
    }
    return 0;
}
