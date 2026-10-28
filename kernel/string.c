#include "string.h"

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    void *ret = dst;
    __asm__ volatile ("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d <= s || d >= s + n) {
        return memcpy(dst, src, n);
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c ? 0 : (char *)s;
}

char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (; *s; s++) if (*s == (char)c) r = s;
    return (char *)(c ? r : s);
}
