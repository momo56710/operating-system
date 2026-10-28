#include "kprintf.h"
#include "serial.h"
#include "fb.h"
#include "spinlock.h"
#include <stdint.h>
#include <stddef.h>

static spinlock_t print_lock;

static void putc_raw(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    fb_putc(c);
}

void kputc(char c) {
    uint64_t f = spin_lock_irqsave(&print_lock);
    putc_raw(c);
    spin_unlock_irqrestore(&print_lock, f);
}

void kwrite(const char *buf, unsigned long n) {
    uint64_t f = spin_lock_irqsave(&print_lock);
    for (unsigned long i = 0; i < n; i++) putc_raw(buf[i]);
    spin_unlock_irqrestore(&print_lock, f);
}

void kclear(void) {
    uint64_t f = spin_lock_irqsave(&print_lock);
    serial_puts("\x1b[2J\x1b[H");
    fb_clear();
    spin_unlock_irqrestore(&print_lock, f);
}

void kprintf_force_unlock(void) { print_lock.locked = 0; }

static int put_str(const char *s, int width, int zero, int left) {
    int n = 0, len = 0;
    while (s[len]) len++;
    if (!left) for (int i = len; i < width; i++) { putc_raw(zero ? '0' : ' '); n++; }
    for (int i = 0; i < len; i++) { putc_raw(s[i]); n++; }
    if (left) for (int i = len; i < width; i++) { putc_raw(' '); n++; }
    return n;
}

static int put_num(uint64_t v, unsigned base, int is_signed, int width, int zero, int left) {
    char buf[24];
    int i = sizeof buf - 1, neg = 0;
    buf[i] = 0;
    if (is_signed && (int64_t)v < 0) { neg = 1; v = -v; }
    do { buf[--i] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) buf[--i] = '-';
    return put_str(&buf[i], width, zero, left);
}

static int kvprintf_locked(const char *fmt, va_list ap) {
    int n = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { putc_raw(*fmt); n++; continue; }
        fmt++;
        int zero = 0, width = 0, lng = 0, left = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'z') { lng = 1; fmt++; }
        switch (*fmt) {
        case 'c': putc_raw((char)va_arg(ap, int)); n++; break;
        case 's': { const char *s = va_arg(ap, const char *);
                    n += put_str(s ? s : "(null)", width, 0, left); break; }
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            n += put_num((uint64_t)v, 10, 1, width, zero, left); break; }
        case 'u': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            n += put_num(v, 10, 0, width, zero, left); break; }
        case 'x': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            n += put_num(v, 16, 0, width, zero, left); break; }
        case 'p':
            putc_raw('0'); putc_raw('x'); n += 2;
            n += put_num((uint64_t)(uintptr_t)va_arg(ap, void *), 16, 0, 16, 1, 0); break;
        case '%': putc_raw('%'); n++; break;
        case 0: return n;
        default: putc_raw('%'); putc_raw(*fmt); n += 2;
        }
    }
    return n;
}

int kvprintf(const char *fmt, va_list ap) {
    uint64_t f = spin_lock_irqsave(&print_lock);
    int n = kvprintf_locked(fmt, ap);
    spin_unlock_irqrestore(&print_lock, f);
    return n;
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}
