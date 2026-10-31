#pragma once
#include <stdarg.h>
#include <stddef.h>

/* Where printf-style output goes. `count` is the number of characters produced so far. */
struct sink {
    void (*put)(struct sink *s, const char *p, size_t n);
    void  *ctx;
    size_t count;
};

int __vformat(struct sink *s, const char *fmt, va_list ap);   /* returns the characters produced */
