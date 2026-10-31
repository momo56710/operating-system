#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sys.h"
#include "format.h"

long write_all(int fd, const void *buf, size_t len);
long read_line(int fd, char *buf, size_t max);

/* Buffered streams over file descriptors. One buffer per stream serves both reading and writing;
 * a stream is only ever in one of the two states at a time (pending writes, or unread input). */

#define F_READ   0x01
#define F_WRITE  0x02
#define F_EOF    0x04
#define F_ERR    0x08
#define F_LINE   0x10      /* flush at each newline */
#define F_NOBUF  0x20      /* flush at each call */
#define F_OWNBUF 0x40      /* buf was malloc'ed here */
#define F_STATIC 0x80      /* stdin, stdout, stderr: never freed */
#define F_APPEND 0x100

struct _FILE {
    int fd, flags;
    unsigned char *buf;
    size_t bufsz, rpos, rlen, wlen;
    int ungot;
    struct _FILE *next;
};

static struct _FILE f_stdin  = { 0, F_READ | F_STATIC, 0, 0, 0, 0, 0, -1, 0 };
static struct _FILE f_stdout = { 1, F_WRITE | F_STATIC, 0, 0, 0, 0, 0, -1, 0 };
static struct _FILE f_stderr = { 2, F_WRITE | F_STATIC | F_NOBUF, 0, 0, 0, 0, 0, -1, 0 };
FILE *stdin = &f_stdin, *stdout = &f_stdout, *stderr = &f_stderr;
static struct _FILE *open_files;       /* every stream created by fopen, for fflush(NULL) */
static int std_init_done;

static void std_init(void) {
    if (std_init_done) return;
    std_init_done = 1;
    if (isatty(1)) f_stdout.flags |= F_LINE;           /* a terminal: show output as soon as a line is complete */
    f_stdin.next = &f_stdout; f_stdout.next = &f_stderr;
}

static int alloc_buf(FILE *f) {
    if (f->buf) return 0;
    f->buf = malloc(BUFSIZ);
    if (!f->buf) { f->flags |= F_NOBUF; return -1; }
    f->bufsz = BUFSIZ;
    f->flags |= F_OWNBUF;
    return 0;
}

static int flush_write(FILE *f) {
    if (!f->wlen) return 0;
    long r = write_all(f->fd, f->buf, f->wlen);
    f->wlen = 0;
    if (r < 0) { f->flags |= F_ERR; return EOF; }
    return 0;
}

static void drop_read(FILE *f) {                      /* give unread buffered input back to the descriptor */
    if (f->rlen > f->rpos) lseek(f->fd, -(long)(f->rlen - f->rpos), SEEK_CUR);
    f->rpos = f->rlen = 0;
}

int fflush(FILE *f) {
    std_init();
    if (f) return flush_write(f);
    int r = 0;
    for (FILE *s = &f_stdin; s; s = s->next) if (flush_write(s)) r = EOF;
    for (FILE *s = open_files; s; s = s->next) if (flush_write(s)) r = EOF;
    return r;
}

/* Called by exit() and fork(): nothing buffered may be lost or duplicated. */
void __stdio_flush_all(void) { fflush(0); }

/* Called by read(): a prompt printed without a newline must appear before we wait for input. */
void __stdio_prepare_read(void) { if (f_stdout.wlen) flush_write(&f_stdout); }

static int fill(FILE *f) {
    if (f == &f_stdin) __stdio_prepare_read();
    if (f->wlen && flush_write(f)) return EOF;
    if (alloc_buf(f) < 0) { f->bufsz = 1; f->buf = (unsigned char *)&f->ungot; }     /* no memory: one byte at a time */
    long n = read(f->fd, f->buf, f->bufsz);
    if (n > 0) { f->rpos = 0; f->rlen = (size_t)n; return 0; }
    f->flags |= n == 0 ? F_EOF : F_ERR;
    return EOF;
}

int fgetc(FILE *f) {
    std_init();
    if (!(f->flags & F_READ)) { f->flags |= F_ERR; return EOF; }
    if (f->ungot >= 0) { int c = f->ungot; f->ungot = -1; return c; }
    if (f->rpos == f->rlen && fill(f) == EOF) return EOF;
    return f->buf[f->rpos++];
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
    if (c == EOF || f->ungot >= 0) return EOF;
    f->ungot = c & 0xFF;
    f->flags &= ~F_EOF;
    return c & 0xFF;
}

char *fgets(char *s, int n, FILE *f) {
    if (n <= 0) return 0;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (!i && (f->flags & (F_EOF | F_ERR))) return 0;
    s[i] = 0;
    return s;
}

size_t fread(void *p, size_t size, size_t n, FILE *f) {
    size_t want = size * n, got = 0;
    unsigned char *out = p;
    if (!want) return 0;
    while (got < want) {
        int c = fgetc(f);
        if (c == EOF) break;
        out[got++] = (unsigned char)c;
        size_t avail = f->rlen - f->rpos;                 /* take the rest of the buffer in one go */
        if (avail && want - got) {
            size_t k = avail < want - got ? avail : want - got;
            memcpy(out + got, f->buf + f->rpos, k);
            f->rpos += k; got += k;
        }
    }
    return size ? got / size : 0;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *f) {
    std_init();
    size_t total = size * n;
    if (!total) return 0;
    if (!(f->flags & F_WRITE)) { f->flags |= F_ERR; return 0; }
    if (f->rlen > f->rpos || f->ungot >= 0) { drop_read(f); f->ungot = -1; }
    const unsigned char *in = p;
    if (f->flags & F_NOBUF) { if (write_all(f->fd, in, total) < 0) { f->flags |= F_ERR; return 0; } return n; }
    if (alloc_buf(f) < 0) { if (write_all(f->fd, in, total) < 0) { f->flags |= F_ERR; return 0; } return n; }
    size_t done = 0;
    while (done < total) {
        if (f->wlen == f->bufsz && flush_write(f)) return done / size;
        if (!f->wlen && total - done >= f->bufsz) {       /* a big write: skip the buffer */
            if (write_all(f->fd, in + done, total - done) < 0) { f->flags |= F_ERR; return done / size; }
            return n;
        }
        size_t k = f->bufsz - f->wlen < total - done ? f->bufsz - f->wlen : total - done;
        memcpy(f->buf + f->wlen, in + done, k);
        f->wlen += k; done += k;
    }
    if ((f->flags & F_LINE) && memchr(in, '\n', total)) flush_write(f);
    return n;
}

int fputc(int c, FILE *f) { unsigned char ch = (unsigned char)c; return fwrite(&ch, 1, 1, f) == 1 ? ch : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *s, FILE *f) { size_t n = strlen(s); return fwrite(s, 1, n, f) == n ? 0 : EOF; }
int puts(const char *s) { return (fputs(s, stdout) == EOF || fputc('\n', stdout) == EOF) ? EOF : 0; }

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    if (flush_write(f)) return -1;
    f->flags &= ~(F_LINE | F_NOBUF);
    if (mode == _IOLBF) f->flags |= F_LINE;
    else if (mode == _IONBF) f->flags |= F_NOBUF;
    if (buf && size && !f->buf) { f->buf = (unsigned char *)buf; f->bufsz = size; }
    return 0;
}
void setbuf(FILE *f, char *buf) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ); }

int fseek(FILE *f, long off, int whence) {
    if (flush_write(f)) return -1;
    if (whence == SEEK_CUR && f->rlen > f->rpos) off -= (long)(f->rlen - f->rpos) + (f->ungot >= 0);   /* the descriptor is ahead of us */
    f->rpos = f->rlen = 0;
    f->ungot = -1;
    if (lseek(f->fd, off, whence) < 0) return -1;
    f->flags &= ~F_EOF;
    return 0;
}

long ftell(FILE *f) {
    long pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    return pos - (long)(f->rlen - f->rpos) - (f->ungot >= 0) + (long)f->wlen;
}

void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->flags &= ~F_ERR; }
int feof(FILE *f) { return (f->flags & F_EOF) != 0; }
int ferror(FILE *f) { return (f->flags & F_ERR) != 0; }
void clearerr(FILE *f) { f->flags &= ~(F_EOF | F_ERR); }
int fileno(FILE *f) { return f->fd; }

FILE *fopen(const char *path, const char *mode) {
    std_init();
    int flags = 0, fl = 0, plus = 0;
    for (const char *m = mode; *m; m++) if (*m == '+') plus = 1;
    switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; fl = F_READ | (plus ? F_WRITE : 0); break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; fl = F_WRITE | (plus ? F_READ : 0); break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; fl = F_WRITE | F_APPEND | (plus ? F_READ : 0); break;
    default: errno = EINVAL; return 0;
    }
    int fd = open(path, flags);
    if (fd < 0) {                                       /* the kernel only says "failed", so work out why */
        struct stat st;
        if (stat(path, &st) < 0) errno = ENOENT;
        else if (st.type == FT_DIR && flags != O_RDONLY) errno = EISDIR;
        else errno = EMFILE;
        return 0;
    }
    FILE *f = calloc(1, sizeof *f);
    if (!f) { close(fd); errno = ENOMEM; return 0; }
    f->fd = fd; f->flags = fl; f->ungot = -1;
    if (isatty(fd)) f->flags |= F_LINE;
    f->next = open_files;
    open_files = f;
    return f;
}

int fclose(FILE *f) {
    int r = flush_write(f);
    if (close(f->fd) < 0) r = EOF;
    if (f->flags & F_STATIC) { f->flags |= F_ERR; return r; }
    for (FILE **pp = &open_files; *pp; pp = &(*pp)->next) if (*pp == f) { *pp = f->next; break; }
    if (f->flags & F_OWNBUF) free(f->buf);
    free(f);
    return r;
}

FILE *freopen(const char *path, const char *mode, FILE *f) {
    if (!path) return 0;
    fflush(f);
    FILE *n = fopen(path, mode);
    if (!n) return 0;
    close(f->fd);                                       /* f keeps its identity (stdin, stdout...) but takes the new file */
    f->fd = n->fd; f->flags = (f->flags & F_STATIC) | (n->flags & ~F_STATIC);
    f->rpos = f->rlen = f->wlen = 0; f->ungot = -1;
    for (FILE **pp = &open_files; *pp; pp = &(*pp)->next) if (*pp == n) { *pp = n->next; break; }
    if (n->buf && (n->flags & F_OWNBUF)) free(n->buf);
    free(n);
    return f;
}

char *tmpnam(char *s) {
    static char buf[L_tmpnam];
    static unsigned counter;
    char *out = s ? s : buf;
    snprintf(out, L_tmpnam, "/tmp/lua_%d_%u", getpid(), counter++);
    return out;
}

FILE *tmpfile(void) {
    char name[L_tmpnam];
    tmpnam(name);
    FILE *f = fopen(name, "w+");
    if (f) unlink(name);                                /* it disappears once closed */
    return f;
}

int remove(const char *path) { if (unlink(path) < 0) { errno = ENOENT; return -1; } return 0; }

/* No kernel rename yet: copy, then remove the original. */
int rename(const char *from, const char *to) {
    struct stat st;
    if (stat(from, &st) < 0 || st.type == FT_DIR) { errno = ENOENT; return -1; }
    int in = open(from, O_RDONLY), out = open(to, O_WRONLY | O_CREAT | O_TRUNC);
    if (in < 0 || out < 0) { if (in >= 0) close(in); errno = EACCES; return -1; }
    char buf[2048];
    long n;
    while ((n = read(in, buf, sizeof buf)) > 0) if (write_all(out, buf, (size_t)n) < 0) { close(in); close(out); errno = ENOSPC; return -1; }
    close(in); close(out);
    return unlink(from);
}

void perror(const char *s) {
    if (s && *s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", strerror(errno));
}

/* ---- the printf family ---- */

static void file_put(struct sink *s, const char *p, size_t n) { fwrite(p, 1, n, s->ctx); s->count += n; }

struct strctx { char *buf; size_t size; };      /* size = room for characters, not counting the terminator */
static void str_put(struct sink *s, const char *p, size_t n) {
    struct strctx *c = s->ctx;
    if (s->count < c->size) {
        size_t room = c->size - s->count, k = n < room ? n : room;
        memcpy(c->buf + s->count, p, k);
    }
    s->count += n;
}

static void fd_put(struct sink *s, const char *p, size_t n) { write_all((int)(long)s->ctx, p, n); s->count += n; }

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    struct sink s = { file_put, f, 0 };
    return __vformat(&s, fmt, ap);
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct strctx c = { buf, size ? size - 1 : 0 };
    struct sink s = { str_put, &c, 0 };
    int n = __vformat(&s, fmt, ap);
    if (size) buf[(size_t)n < size ? (size_t)n : size - 1] = 0;
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, (size_t)-1 >> 1, fmt, ap); }

int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(stdout, fmt, ap); va_end(ap); return n; }
int fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(f, fmt, ap); va_end(ap); return n; }
int sprintf(char *buf, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsprintf(buf, fmt, ap); va_end(ap); return n; }
int snprintf(char *buf, size_t size, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, size, fmt, ap); va_end(ap); return n; }
int dprintf(int fd, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    struct sink s = { fd_put, (void *)(long)fd, 0 };
    int n = __vformat(&s, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- helpers the LeOS programs use ---- */

long write_all(int fd, const void *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        long w = write(fd, (const char *)buf + done, len - done);
        if (w <= 0) return -1;
        done += (size_t)w;
    }
    return (long)done;
}

long read_line(int fd, char *buf, size_t max) {
    size_t n = 0;
    while (n + 1 < max) {
        char c;
        long r = read(fd, &c, 1);
        if (r <= 0) break;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = 0;
    return (long)n;
}
