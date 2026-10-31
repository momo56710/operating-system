#pragma once
/* One include for LeOS programs: the standard C library plus the system-call wrappers and a few extras. */
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "sys.h"

/* ---- LeOS extras ---- */
/* Write all of buf, even if the kernel takes it in pieces. */
long write_all(int fd, const void *buf, size_t len);
/* Read one line (up to max-1 bytes, newline kept) from fd; returns its length, 0 at end of input. */
long read_line(int fd, char *buf, size_t max);

int      parse_ip(const char *s, unsigned *ip);                           /* dotted quad -> host-order address; 0 if not one */
unsigned dns_lookup(const char *name, unsigned server, unsigned port);    /* A record over UDP; 0 on failure */
unsigned resolve(const char *name);                                       /* dotted quad, or a DNS lookup via the system's server */
void     ip_to_str(unsigned ip, char *out);                               /* "10.0.2.2" */
