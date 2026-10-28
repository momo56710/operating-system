#pragma once
#include <stdarg.h>

void kputc(char c);
int  kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  kvprintf(const char *fmt, va_list ap);
