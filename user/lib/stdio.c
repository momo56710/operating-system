#include "libc.h"

/* printf formats into a local buffer and writes it with one system call. */
static size_t put(char *buf, size_t n, size_t max, char c) { if (n < max) buf[n] = c; return n + 1; }

static size_t put_num(char *buf, size_t n, size_t max, unsigned long v, unsigned base, int neg, int width, int zero) {
    char tmp[24];
    int i = 0;
    do { tmp[i++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) tmp[i++] = '-';
    for (int pad = i; pad < width; pad++) n = put(buf, n, max, zero ? '0' : ' ');
    while (i) n = put(buf, n, max, tmp[--i]);
    return n;
}

static int vformat(char *buf, size_t max, const char *fmt, va_list ap) {
    size_t n = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { n = put(buf, n, max, *fmt); continue; }
        fmt++;
        int zero = 0, width = 0, lng = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'z') { lng = 1; fmt++; }
        switch (*fmt) {
        case 'c': n = put(buf, n, max, (char)va_arg(ap, int)); break;
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
                    int len = (int)strlen(s);
                    for (int i = len; i < width; i++) n = put(buf, n, max, ' ');
                    while (*s) { n = put(buf, n, max, *s); s++; }
                    break; }
        case 'd': case 'i': { long v = lng ? va_arg(ap, long) : va_arg(ap, int);
                    n = put_num(buf, n, max, v < 0 ? -(unsigned long)v : (unsigned long)v, 10, v < 0, width, zero); break; }
        case 'u': n = put_num(buf, n, max, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0, width, zero); break;
        case 'x': n = put_num(buf, n, max, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 0, width, zero); break;
        case 'p': n = put(buf, n, max, '0'); n = put(buf, n, max, 'x');
                  n = put_num(buf, n, max, (unsigned long)va_arg(ap, void *), 16, 0, 16, 1); break;
        case '%': n = put(buf, n, max, '%'); break;
        case 0: return (int)n;
        default: n = put(buf, n, max, '%'); n = put(buf, n, max, *fmt);
        }
    }
    return (int)n;
}

int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf) n = sizeof buf;       /* truncated output */
    write(1, buf, n);
    return n;
}

int putchar(int c) { char ch = (char)c; write(1, &ch, 1); return c; }
int puts(const char *s) { write(1, s, strlen(s)); write(1, "\n", 1); return 0; }
