#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

/* Built with -fno-builtin: the compiler must not turn these loops back into calls to themselves. */

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
size_t strnlen(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return n; }

char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }
char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) {
    char *e = d + strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; i++) e[i] = s[i];
    e[i] = 0;
    return d;
}

int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; if (!*a) return 0; }
    return 0;
}
int strcoll(const char *a, const char *b) { return strcmp(a, b); }          /* the "C" locale */

char *strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char *)s; return c ? 0 : (char *)s; }
char *strrchr(const char *s, int c) { const char *r = 0; for (; *s; s++) if (*s == (char)c) r = s; return (char *)(c ? r : s); }
char *strstr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, nl)) return (char *)h;
    return 0;
}
size_t strspn(const char *s, const char *accept) { size_t n = 0; while (s[n] && strchr(accept, s[n])) n++; return n; }
size_t strcspn(const char *s, const char *reject) { size_t n = 0; while (s[n] && !strchr(reject, s[n])) n++; return n; }
char *strpbrk(const char *s, const char *accept) { for (; *s; s++) if (strchr(accept, *s)) return (char *)s; return 0; }
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); if (d) memcpy(d, s, n); return d; }

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n) {
    if ((uintptr_t)d <= (uintptr_t)s || (uintptr_t)d >= (uintptr_t)s + n) return memcpy(d, s, n);
    unsigned char *a = (unsigned char *)d + n; const unsigned char *b = (const unsigned char *)s + n;
    while (n--) *--a = *--b;
    return d;
}
void *memset(void *d, int c, size_t n) {
    void *r = d;
    __asm__ volatile ("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
    return 0;
}

char *strerror(int e) {
    switch (e) {
    case 0: return "Success";
    case EPERM: return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case EIO: return "Input/output error";
    case EBADF: return "Bad file descriptor";
    case ECHILD: return "No child processes";
    case ENOMEM: return "Cannot allocate memory";
    case EACCES: return "Permission denied";
    case EEXIST: return "File exists";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case EINVAL: return "Invalid argument";
    case EMFILE: return "Too many open files";
    case ENOSPC: return "No space left on device";
    case EPIPE: return "Broken pipe";
    case EDOM: return "Numerical argument out of domain";
    case ERANGE: return "Numerical result out of range";
    case ENOSYS: return "Function not implemented";
    default: return "Unknown error";
    }
}
