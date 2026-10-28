#pragma once
#include <stdarg.h>

void kputc(char c);
int  kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  kvprintf(const char *fmt, va_list ap);
void kprintf_force_unlock(void);   /* exception and panic paths: never wait on the print lock */
void kclear(void);                  /* clear the screen and the serial terminal */
void kwrite(const char *buf, unsigned long n);   /* raw bytes to the console, atomically */
