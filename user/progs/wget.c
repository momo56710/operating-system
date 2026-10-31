#include "libc.h"

/* wget [-O file] http://host[:port]/path : fetch a page over HTTP/1.0 and print it (or save it). */
int main(int argc, char **argv) {
    const char *out_path = 0, *url = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-O") && i + 1 < argc) out_path = argv[++i];
        else url = argv[i];
    }
    if (!url || strncmp(url, "http://", 7)) { dprintf(2, "usage: wget [-O file] http://host[:port]/path\n"); return 1; }
    char host[100], path[200];
    unsigned port = 80;
    const char *p = url + 7;
    int hl = 0;
    while (*p && *p != '/' && *p != ':' && hl < (int)sizeof host - 1) host[hl++] = *p++;
    host[hl] = 0;
    if (*p == ':') { p++; port = 0; while (*p >= '0' && *p <= '9') port = port * 10 + (*p++ - '0'); }
    strncpy(path, *p ? p : "/", sizeof path - 1);
    path[sizeof path - 1] = 0;

    unsigned ip = resolve(host);
    if (!ip) { dprintf(2, "wget: cannot resolve %s\n", host); return 1; }
    int fd = socket(SOCK_TCP);
    if (fd < 0 || connect(fd, ip, port) < 0) { dprintf(2, "wget: cannot connect to %s:%u\n", host, port); return 1; }

    char req[400];
    int rl = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: LeOS-wget\r\nConnection: close\r\n\r\n", path, host);
    if (write_all(fd, req, rl) < 0) { dprintf(2, "wget: send failed\n"); return 1; }

    int out = 1;
    if (out_path && (out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC)) < 0) { dprintf(2, "wget: cannot write %s\n", out_path); return 1; }

    static char head[4096];
    int hlen = 0, in_body = 0, status = 0;
    unsigned long body = 0;
    char buf[2048];
    long n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        if (in_body) { write_all(out, buf, n); body += n; continue; }
        for (long i = 0; i < n && !in_body; i++) {            /* collect the header up to the blank line */
            if (hlen < (int)sizeof head - 1) head[hlen++] = buf[i];
            if (hlen >= 4 && !memcmp(head + hlen - 4, "\r\n\r\n", 4)) {
                in_body = 1;
                head[hlen] = 0;
                status = atoi(head + 9);                       /* "HTTP/1.0 200 OK" */
                long rest = n - i - 1;
                if (rest > 0) { write_all(out, buf + i + 1, rest); body += rest; }
            }
        }
    }
    if (out != 1) close(out);
    close(fd);
    dprintf(2, "wget: HTTP %d, %lu bytes\n", status, body);
    return status == 200 ? 0 : 1;
}
