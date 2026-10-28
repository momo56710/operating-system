#pragma once
#include <stddef.h>
#include <stdarg.h>
#include "sys.h"

size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
char  *strcpy(char *dst, const char *src);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
int    atoi(const char *s);

int  printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  puts(const char *s);
int  putchar(int c);

void *malloc(size_t n);
void  free(void *p);
