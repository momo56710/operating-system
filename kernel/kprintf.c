#include "kprintf.h"
#include "serial.h"
#include "fb.h"
#include <stdint.h>
#include <stddef.h>

void kputc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    fb_putc(c);
}

static int put_str(const char *s, int width, int zero) {
    int n = 0, len = 0;
    while (s[len]) len++;
    for (int i = len; i < width; i++) { kputc(zero ? '0' : ' '); n++; }
    for (int i = 0; i < len; i++) { kputc(s[i]); n++; }
    return n;
}

static int put_num(uint64_t v, unsigned base, int is_signed, int width, int zero) {
    char buf[24];
    int i = sizeof buf - 1, neg = 0;
    buf[i] = 0;
    if (is_signed && (int64_t)v < 0) { neg = 1; v = -v; }
    do { buf[--i] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) buf[--i] = '-';
    return put_str(&buf[i], width, zero);
}

int kvprintf(const char *fmt, va_list ap) {
    int n = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { kputc(*fmt); n++; continue; }
        fmt++;
        int zero = 0, width = 0, lng = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'z') { lng = 1; fmt++; }
        switch (*fmt) {
        case 'c': kputc((char)va_arg(ap, int)); n++; break;
        case 's': { const char *s = va_arg(ap, const char *);
                    n += put_str(s ? s : "(null)", width, 0); break; }
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            n += put_num((uint64_t)v, 10, 1, width, zero); break; }
        case 'u': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            n += put_num(v, 10, 0, width, zero); break; }
        case 'x': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            n += put_num(v, 16, 0, width, zero); break; }
        case 'p':
            kputc('0'); kputc('x'); n += 2;
            n += put_num((uint64_t)(uintptr_t)va_arg(ap, void *), 16, 0, 16, 1); break;
        case '%': kputc('%'); n++; break;
        case 0: return n;
        default: kputc('%'); kputc(*fmt); n += 2;
        }
    }
    return n;
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}
