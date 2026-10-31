#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "format.h"

/* The printf engine: parses a format string and hands the produced characters to a sink. Integers,
 * strings and pointers are straightforward. Floating-point conversions work from the exact decimal
 * expansion of the double (big-integer arithmetic) and round the way a real C library does
 * (ties to even), so %.14g and friends give the same digits as glibc. */

/* ---- exact decimal digits of a double ---- */

#define BASE 1000000000u
#define MAXL 100
struct big { uint32_t d[MAXL]; int n; };

static void big_set(struct big *b, uint64_t v) {
    b->n = 0;
    while (v) { b->d[b->n++] = (uint32_t)(v % BASE); v /= BASE; }
}

static void big_mul(struct big *b, uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t t = (uint64_t)b->d[i] * m + carry;
        b->d[i] = (uint32_t)(t % BASE);
        carry = t / BASE;
    }
    while (carry) { b->d[b->n++] = (uint32_t)(carry % BASE); carry /= BASE; }
}

static int big_digits(const struct big *b, char *out) {
    int n = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        char tmp[9];
        uint32_t v = b->d[i];
        for (int k = 8; k >= 0; k--) { tmp[k] = (char)('0' + v % 10); v /= 10; }
        int start = 0;
        if (i == b->n - 1) while (start < 8 && tmp[start] == '0') start++;   /* no leading zeros on the first limb */
        for (int k = start; k < 9; k++) out[n++] = tmp[k];
    }
    if (!n) out[n++] = '0';
    return n;
}

/* v must be finite and positive. digs gets the digits, *decpt the decimal point position:
 * value = 0.DIGITS x 10^decpt. */
static int exact_digits(double v, char *digs, int *decpt) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    int be = (int)((bits >> 52) & 0x7FF);
    uint64_t m = bits & ((1ULL << 52) - 1);
    int e;
    if (be == 0) e = -1074; else { m |= 1ULL << 52; e = be - 1075; }
    while (m && !(m & 1) && e < 0) { m >>= 1; e++; }             /* fewer bits, less work */
    struct big b;
    big_set(&b, m);
    int nd;
    if (e >= 0) {
        while (e >= 29) { big_mul(&b, 1u << 29); e -= 29; }
        if (e) big_mul(&b, 1u << e);
        nd = big_digits(&b, digs);
        *decpt = nd;
    } else {
        int k = -e;                                              /* value = m * 5^k / 10^k */
        int kk = k;
        while (kk >= 13) { big_mul(&b, 1220703125u); kk -= 13; }  /* 5^13 */
        static const uint32_t p5[13] = { 1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625, 48828125, 244140625 };
        if (kk) big_mul(&b, p5[kk]);
        nd = big_digits(&b, digs);
        *decpt = nd - k;
    }
    return nd;
}

/* Keep only the first `keep` digits, rounding to nearest with ties to even. May lower *nd and raise *decpt. */
static void round_digits(char *digs, int *nd, int keep, int *decpt) {
    if (keep >= *nd) return;
    if (keep < 0) { *nd = 0; return; }                           /* far below the rounding position: zero */
    int up;
    char first_dropped = digs[keep];
    if (first_dropped > '5') up = 1;
    else if (first_dropped < '5') up = 0;
    else {
        int sticky = 0;
        for (int i = keep + 1; i < *nd; i++) if (digs[i] != '0') { sticky = 1; break; }
        up = sticky ? 1 : (keep > 0 && ((digs[keep - 1] - '0') & 1));   /* an exact tie: to the even neighbour */
    }
    *nd = keep;
    if (!up) return;
    int i = keep - 1;
    while (i >= 0 && digs[i] == '9') { digs[i] = '0'; i--; }
    if (i >= 0) { digs[i]++; return; }
    if (keep == 0) { digs[0] = '1'; *nd = 1; (*decpt)++; return; }   /* 0.6 rounds to 1 at keep == 0 */
    digs[0] = '1';                                               /* 999 -> 1000 */
    for (int k = 1; k < keep; k++) digs[k] = '0';
    (*decpt)++;
}

/* ---- the three float layouts; each writes into out and returns the length ---- */

static int fmt_fixed(char *out, const char *digs, int nd, int decpt, int prec, int alt) {
    int n = 0;
    if (nd == 0) decpt = 1;
    if (decpt <= 0) out[n++] = '0';
    else for (int i = 0; i < decpt; i++) out[n++] = i < nd ? digs[i] : '0';
    if (prec > 0 || alt) out[n++] = '.';
    for (int i = 0; i < prec; i++) {
        int idx = decpt + i;
        out[n++] = (idx >= 0 && idx < nd) ? digs[idx] : '0';
    }
    return n;
}

static int fmt_exp(char *out, const char *digs, int nd, int decpt, int prec, char econv, int alt) {
    int n = 0, x = nd == 0 ? 0 : decpt - 1;
    out[n++] = nd ? digs[0] : '0';
    if (prec > 0 || alt) out[n++] = '.';
    for (int i = 1; i <= prec; i++) out[n++] = i < nd ? digs[i] : '0';
    out[n++] = econv;
    out[n++] = x < 0 ? '-' : '+';
    if (x < 0) x = -x;
    char t[8]; int tl = 0;
    do { t[tl++] = (char)('0' + x % 10); x /= 10; } while (x);
    if (tl < 2) t[tl++] = '0';
    while (tl) out[n++] = t[--tl];
    return n;
}

#define DIGBUF 800
#define OUTBUF 2300

/* %f %e %g for a finite value; sign is handled by the caller. */
static int fmt_double(char *out, double v, char conv, int prec, int alt) {
    char digs[DIGBUF];
    int nd, decpt = 1;
    if (v == 0) { digs[0] = '0'; nd = 0; decpt = 1; }
    else nd = exact_digits(v, digs, &decpt);
    char lower = (char)(conv | 0x20);

    if (lower == 'f') {
        round_digits(digs, &nd, decpt + prec, &decpt);
        return fmt_fixed(out, digs, nd, decpt, prec, alt);
    }
    if (lower == 'e') {
        round_digits(digs, &nd, prec + 1, &decpt);
        return fmt_exp(out, digs, nd, decpt, prec, conv, alt);
    }
    int p = prec == 0 ? 1 : prec;                               /* %g: p significant digits */
    round_digits(digs, &nd, p, &decpt);
    int x = nd == 0 ? 0 : decpt - 1;
    int n;
    if (x >= -4 && x < p) n = fmt_fixed(out, digs, nd, decpt, p - 1 - x, alt);
    else                  n = fmt_exp(out, digs, nd, decpt, p - 1, conv == 'G' ? 'E' : 'e', alt);
    if (!alt) {                                                 /* drop trailing zeros and a bare decimal point */
        int epos = n;
        for (int i = 0; i < n; i++) if (out[i] == 'e' || out[i] == 'E') { epos = i; break; }
        int dot = -1;
        for (int i = 0; i < epos; i++) if (out[i] == '.') { dot = i; break; }
        if (dot >= 0) {
            int end = epos;
            while (end > dot + 1 && out[end - 1] == '0') end--;
            if (end == dot + 1) end = dot;
            memmove(out + end, out + epos, (size_t)(n - epos));
            n -= epos - end;
        }
    }
    return n;
}

/* %a: hexadecimal floating point, e.g. 0x1.8p+1 */
static int fmt_hexfloat(char *out, double v, int prec, int upper, int alt) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    int be = (int)((bits >> 52) & 0x7FF);
    uint64_t m = bits & ((1ULL << 52) - 1);
    int lead = be ? 1 : 0, e = be ? be - 1023 : (m ? -1022 : 0);
    int n = 0;
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (prec >= 0 && prec < 13) {                                /* round the 13 hex digits of the fraction to prec digits */
        int drop = (13 - prec) * 4;
        uint64_t half = 1ULL << (drop - 1), rem = m & ((1ULL << drop) - 1);
        m >>= drop;
        if (rem > half || (rem == half && ((m | (uint64_t)lead) & 1))) {
            m++;
            if (prec == 0 ? m >= 1 : m >> (prec * 4)) { m = 0; lead++; }      /* the carry reaches the leading digit */
        }
        m <<= drop;
    }
    out[n++] = '0'; out[n++] = upper ? 'X' : 'x';
    out[n++] = (char)('0' + lead);
    int digits = 13;
    if (prec < 0) { while (digits > 0 && !((m >> ((13 - digits) * 4)) & 0xF)) digits--; }
    else digits = prec;
    if (digits > 0 || alt) out[n++] = '.';
    for (int i = 0; i < digits; i++) out[n++] = i < 13 ? hex[(m >> ((12 - i) * 4)) & 0xF] : '0';
    out[n++] = upper ? 'P' : 'p';
    out[n++] = e < 0 ? '-' : '+';
    if (e < 0) e = -e;
    char t[8]; int tl = 0;
    do { t[tl++] = (char)('0' + e % 10); e /= 10; } while (e);
    while (tl) out[n++] = t[--tl];
    return n;
}

/* ---- the engine ---- */

static void pad(struct sink *s, char c, int n) {
    char buf[32];
    memset(buf, c, sizeof buf);
    while (n > 0) { int k = n < (int)sizeof buf ? n : (int)sizeof buf; s->put(s, buf, (size_t)k); n -= k; }
}

/* Emit prefix + body with width, justification and zero padding applied. */
static void emit(struct sink *s, const char *prefix, int plen, const char *body, int blen, int width, int left, int zero) {
    int total = plen + blen, fill = width > total ? width - total : 0;
    if (!left && !zero) pad(s, ' ', fill);
    if (plen) s->put(s, prefix, (size_t)plen);
    if (!left && zero) pad(s, '0', fill);
    s->put(s, body, (size_t)blen);
    if (left) pad(s, ' ', fill);
}

int __vformat(struct sink *s, const char *fmt, va_list ap) {
    size_t start = s->count;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            const char *run = fmt;
            while (fmt[1] && fmt[1] != '%') fmt++;
            s->put(s, run, (size_t)(fmt - run + 1));
            continue;
        }
        fmt++;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1; else if (*fmt == '+') plus = 1; else if (*fmt == ' ') space = 1;
            else if (*fmt == '#') alt = 1; else if (*fmt == '0') zero = 1; else break;
        }
        int width = 0, prec = -1;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++; prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        int lenmod = 0;                                          /* -2 hh, -1 h, 0 int, 1 l, 2 ll/j/z/t, 3 L */
        if (*fmt == 'h') { fmt++; lenmod = -1; if (*fmt == 'h') { fmt++; lenmod = -2; } }
        else if (*fmt == 'l') { fmt++; lenmod = 1; if (*fmt == 'l') { fmt++; lenmod = 2; } }
        else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') { fmt++; lenmod = 2; }
        else if (*fmt == 'L') { fmt++; lenmod = 3; }

        char conv = *fmt;
        char body[OUTBUF];
        char prefix[4];
        int plen = 0, blen = 0;
        switch (conv) {
        case 0: return (int)(s->count - start);
        case '%': s->put(s, "%", 1); break;
        case 'c': { char c = (char)va_arg(ap, int); emit(s, "", 0, &c, 1, width, left, 0); break; }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            int len = 0;
            while ((prec < 0 || len < prec) && str[len]) len++;
            emit(s, "", 0, str, len, width, left, 0);
            break;
        }
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
            uint64_t v;
            int neg = 0, base = 10;
            if (conv == 'p') { v = (uint64_t)(uintptr_t)va_arg(ap, void *); base = 16; alt = 1; prec = prec < 0 ? 1 : prec; }
            else if (conv == 'd' || conv == 'i') {
                int64_t sv = lenmod >= 1 ? (lenmod == 1 ? (int64_t)va_arg(ap, long) : (int64_t)va_arg(ap, long long))
                                         : (int64_t)va_arg(ap, int);
                if (lenmod == -1) sv = (short)sv; else if (lenmod == -2) sv = (signed char)sv;
                neg = sv < 0;
                v = neg ? (uint64_t)0 - (uint64_t)sv : (uint64_t)sv;
            } else {
                v = lenmod >= 1 ? (lenmod == 1 ? (uint64_t)va_arg(ap, unsigned long) : (uint64_t)va_arg(ap, unsigned long long))
                                : (uint64_t)va_arg(ap, unsigned);
                if (lenmod == -1) v = (unsigned short)v; else if (lenmod == -2) v = (unsigned char)v;
                base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : 10;
            }
            const char *digs = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[24];
            int tl = 0, nonzero = v != 0;
            while (v) { tmp[tl++] = digs[v % (uint64_t)base]; v /= (uint64_t)base; }
            int zeros = prec > tl ? prec - tl : 0;
            if (prec == 0 && tl == 0 && !(conv == 'o' && alt)) zeros = 0, tl = 0;          /* "%.0d" of 0 prints nothing */
            else if (tl == 0 && zeros == 0) tmp[tl++] = '0';
            if (conv == 'o' && alt && zeros == 0 && (tl == 0 || tmp[tl - 1] != '0')) zeros = 1;
            if (conv == 'd' || conv == 'i') { if (neg) prefix[plen++] = '-'; else if (plus) prefix[plen++] = '+'; else if (space) prefix[plen++] = ' '; }
            else if (alt && (conv == 'x' || conv == 'X' || conv == 'p') && (conv == 'p' || nonzero)) { prefix[plen++] = '0'; prefix[plen++] = conv == 'X' ? 'X' : 'x'; }
            for (int i = 0; i < zeros; i++) body[blen++] = '0';
            while (tl) body[blen++] = tmp[--tl];
            emit(s, prefix, plen, body, blen, width, left, zero && prec < 0);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            double v = lenmod == 3 ? (double)va_arg(ap, long double) : va_arg(ap, double);
            uint64_t vbits;
            memcpy(&vbits, &v, 8);
            int neg = (int)(vbits >> 63);                  /* the sign bit, so -0.0 and the -nan hardware makes print as in glibc */
            int upper = conv < 'a';
            if (v < 0) v = -v;
            if (neg) prefix[plen++] = '-'; else if (plus) prefix[plen++] = '+'; else if (space) prefix[plen++] = ' ';
            if (v != v) { memcpy(body, upper ? "NAN" : "nan", 3); blen = 3; zero = 0; }
            else if (v > 1.7976931348623157e308) { memcpy(body, upper ? "INF" : "inf", 3); blen = 3; zero = 0; }
            else if (conv == 'a' || conv == 'A') {
                if (prec > 100) prec = 100;
                if (v == 0) { body[0] = '0'; body[1] = upper ? 'X' : 'x'; body[2] = '0'; blen = 3; if (prec > 0) { body[blen++] = '.'; for (int i = 0; i < prec; i++) body[blen++] = '0'; } body[blen++] = upper ? 'P' : 'p'; body[blen++] = '+'; body[blen++] = '0'; }
                else blen = fmt_hexfloat(body, v, prec, upper, alt);
                /* the 0x belongs with the sign, before any zero padding */
                if (zero && !left) { prefix[plen++] = '0'; prefix[plen++] = body[1]; memmove(body, body + 2, (size_t)(blen - 2)); blen -= 2; }
            } else {
                if (prec < 0) prec = 6;
                if (prec > 1000) prec = 1000;
                blen = fmt_double(body, v, conv, prec, alt);
            }
            emit(s, prefix, plen, body, blen, width, left, zero);
            break;
        }
        default:                                                 /* unknown conversion: print it literally */
            s->put(s, "%", 1);
            s->put(s, &conv, 1);
        }
    }
    return (int)(s->count - start);
}
