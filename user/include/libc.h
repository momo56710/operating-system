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
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *hay, const char *needle);
char  *strcat(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);

int  printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int  puts(const char *s);
int  putchar(int c);

int  snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
char *strdup(const char *s);

void *malloc(size_t n);
void *realloc(void *p, size_t n);
void  free(void *p);

/* Write all of buf, even if the kernel takes it in pieces. */
long write_all(int fd, const void *buf, size_t len);
/* Read one line (up to max-1 bytes, newline kept) from fd; returns its length, 0 at end of input. */
long read_line(int fd, char *buf, size_t max);

int parse_ip(const char *s, unsigned *ip);   /* dotted quad -> host-order address; 0 if not one */
unsigned dns_lookup(const char *name, unsigned server, unsigned port);   /* A record over UDP; 0 on failure */
unsigned resolve(const char *name);                                       /* dotted quad, or a DNS lookup via the system's server */
void     ip_to_str(unsigned ip, char *out);                               /* "10.0.2.2" */
