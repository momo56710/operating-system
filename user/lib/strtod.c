#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

/* ---- integers ---- */

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* Shared by all strto* integer functions. Returns the magnitude; *neg tells the sign; *over flags overflow. */
static unsigned long long parse_integer(const char *s, char **end, int base, int *neg, int *over, int *any) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    *neg = 0; *over = 0; *any = 0;
    if (*p == '+' || *p == '-') { *neg = *p == '-'; p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_value((unsigned char)p[2]) < 16) { p += 2; base = 16; }
    else if (base == 0) base = p[0] == '0' ? 8 : 10;
    unsigned long long v = 0;
    for (; digit_value((unsigned char)*p) < base; p++) {
        unsigned d = (unsigned)digit_value((unsigned char)*p);
        if (v > (ULLONG_MAX - d) / (unsigned)base) *over = 1; else v = v * (unsigned)base + d;
        *any = 1;
    }
    if (end) *end = (char *)(*any ? p : s);
    return v;
}

long long strtoll(const char *s, char **end, int base) {
    int neg, over, any;
    unsigned long long v = parse_integer(s, end, base, &neg, &over, &any);
    if (over || (!neg && v > (unsigned long long)LLONG_MAX) || (neg && v > (unsigned long long)LLONG_MAX + 1)) {
        errno = ERANGE;
        return neg ? LLONG_MIN : LLONG_MAX;
    }
    return neg ? (long long)(0 - v) : (long long)v;
}
unsigned long long strtoull(const char *s, char **end, int base) {
    int neg, over, any;
    unsigned long long v = parse_integer(s, end, base, &neg, &over, &any);
    if (over) { errno = ERANGE; return ULLONG_MAX; }
    return neg ? 0 - v : v;
}
long strtol(const char *s, char **end, int base) { return (long)strtoll(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtoull(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }

/* ---- floating point ---- */

/* Decimal strings are turned into a double through 80-bit extended arithmetic: up to 19 significant digits
 * are collected exactly, then scaled by a power of ten. This is exact for the common cases (short numbers
 * with small exponents) and within one unit in the last place otherwise. */
static const long double pow10_exact[28] = {
    1e0L, 1e1L, 1e2L, 1e3L, 1e4L, 1e5L, 1e6L, 1e7L, 1e8L, 1e9L, 1e10L, 1e11L, 1e12L, 1e13L, 1e14L, 1e15L,
    1e16L, 1e17L, 1e18L, 1e19L, 1e20L, 1e21L, 1e22L, 1e23L, 1e24L, 1e25L, 1e26L, 1e27L };

static long double scale10(long double v, long e) {
    if (e > 5000) e = 5000;
    if (e < -5000) e = -5000;
    while (e >= 27) { v *= pow10_exact[27]; e -= 27; if (v > 1e4900L) return v; }
    while (e <= -27) { v /= pow10_exact[27]; e += 27; if (v < 1e-4900L) return v; }
    return e >= 0 ? v * pow10_exact[e] : v / pow10_exact[-e];
}

double strtod(const char *s, char **end) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }

    if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') && (p[2] == 'f' || p[2] == 'F')) {
        p += 3;
        if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') && (p[2] == 'i' || p[2] == 'I') && (p[3] == 't' || p[3] == 'T') && (p[4] == 'y' || p[4] == 'Y')) p += 5;
        if (end) *end = (char *)p;
        return neg ? -__builtin_inf() : __builtin_inf();
    }
    if ((p[0] == 'n' || p[0] == 'N') && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'n' || p[2] == 'N')) {
        if (end) *end = (char *)(p + 3);
        return __builtin_nan("");
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {                 /* hexadecimal floating point: 0x1.8p3 */
        const char *q = p + 2;
        long double v = 0;
        long exp2 = 0;
        int any = 0, seen_dot = 0;
        for (;; q++) {
            if (*q == '.' && !seen_dot) { seen_dot = 1; continue; }
            int d = digit_value((unsigned char)*q);
            if (d >= 16) break;
            v = v * 16 + d;
            if (seen_dot) exp2 -= 4;
            any = 1;
        }
        if (!any) { if (end) *end = (char *)(p + 1); return neg ? -0.0 : 0.0; }
        if (*q == 'p' || *q == 'P') {
            const char *r = q + 1;
            int eneg = 0;
            if (*r == '+' || *r == '-') { eneg = *r == '-'; r++; }
            if (*r >= '0' && *r <= '9') {
                long e = 0;
                while (*r >= '0' && *r <= '9') { if (e < 100000) e = e * 10 + (*r - '0'); r++; }
                exp2 += eneg ? -e : e;
                q = r;
            }
        }
        while (exp2 > 0 && v < 1e4900L) { v *= 2; exp2--; }
        while (exp2 < 0 && v > 1e-4900L) { v /= 2; exp2++; }
        if (end) *end = (char *)q;
        return neg ? -(double)v : (double)v;
    }

    unsigned long long mant = 0;
    int ndig = 0, dropped = 0, any = 0, seen_dot = 0;
    long exp10 = 0;
    const char *q = p;
    for (;; q++) {
        if (*q == '.' && !seen_dot) { seen_dot = 1; continue; }
        if (*q < '0' || *q > '9') break;
        any = 1;
        if (ndig < 19) { mant = mant * 10 + (unsigned)(*q - '0'); if (mant) ndig++; if (seen_dot) exp10--; }
        else { dropped++; if (!seen_dot) exp10++; }                      /* more digits than fit: they only shift the scale */
    }
    (void)dropped;
    if (!any) { if (end) *end = (char *)s; return 0.0; }
    if (*q == 'e' || *q == 'E') {
        const char *r = q + 1;
        int eneg = 0;
        if (*r == '+' || *r == '-') { eneg = *r == '-'; r++; }
        if (*r >= '0' && *r <= '9') {
            long e = 0;
            while (*r >= '0' && *r <= '9') { if (e < 100000) e = e * 10 + (*r - '0'); r++; }
            exp10 += eneg ? -e : e;
            q = r;
        }
    }
    if (end) *end = (char *)q;
    if (mant == 0) return neg ? -0.0 : 0.0;
    long double v = scale10((long double)mant, exp10);
    double d;
    if (v >= 1.797693134862315807e308L) { errno = ERANGE; d = __builtin_inf(); }          /* 2^1024 - 2^970: halfway to the next double */
    else if (v < 4.9406564584124654e-324L / 2) { errno = ERANGE; d = 0.0; }
    else { d = (double)v; if (d < 2.2250738585072014e-308) errno = ERANGE; }          /* subnormal: some precision is lost */
    return neg ? -d : d;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, 0); }
