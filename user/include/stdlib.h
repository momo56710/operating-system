#pragma once
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void  free(void *p);
void  exit(int code) __attribute__((noreturn));      /* flushes every open stdio stream first */
void  _exit(int code) __attribute__((noreturn));     /* straight to the kernel */
void  abort(void) __attribute__((noreturn));
int   atexit(void (*fn)(void));
int   abs(int x);
long  labs(long x);
long long llabs(long long x);
int   atoi(const char *s);
long  atol(const char *s);
double atof(const char *s);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
void  qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
int   rand(void);
void  srand(unsigned seed);
char *getenv(const char *name);
int   setenv(const char *name, const char *value, int overwrite);
int   unsetenv(const char *name);
int   system(const char *cmd);
