#pragma once
#include <stddef.h>
#include <stdarg.h>

typedef struct _FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define stdin stdin
#define EOF (-1)
#define BUFSIZ 4096
#define FILENAME_MAX 256
#define L_tmpnam 32
#define TMP_MAX 100000
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
FILE *tmpfile(void);
int   fclose(FILE *f);
int   fflush(FILE *f);                         /* fflush(NULL) flushes every open stream */
int   setvbuf(FILE *f, char *buf, int mode, size_t size);
void  setbuf(FILE *f, char *buf);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int   fgetc(FILE *f);
int   getc(FILE *f);
int   getchar(void);
char *fgets(char *s, int n, FILE *f);
int   ungetc(int c, FILE *f);
int   fputc(int c, FILE *f);
int   putc(int c, FILE *f);
int   putchar(int c);
int   fputs(const char *s, FILE *f);
int   puts(const char *s);
int   fseek(FILE *f, long off, int whence);
long  ftell(FILE *f);
void  rewind(FILE *f);
int   feof(FILE *f);
int   ferror(FILE *f);
void  clearerr(FILE *f);
int   fileno(FILE *f);
int   remove(const char *path);
int   rename(const char *from, const char *to);
char *tmpnam(char *s);
void  perror(const char *s);

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
