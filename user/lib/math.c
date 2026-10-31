#include <math.h>
#include <stdint.h>
#include <errno.h>

/* The x87 floating-point unit has hardware instructions for sine, cosine, arctangent, logarithms and
 * powers of two, working in 80-bit extended precision. These functions wrap them and handle the special
 * cases (NaN, infinity, domain errors) in C. Built with -fno-builtin. */

#define LD long double

static double make_nan(void) { volatile double z = 0.0; return z / z; }     /* what the CPU itself produces for 0/0 */

static double from_bits(uint64_t b) { union { uint64_t u; double d; } v = { b }; return v.d; }
static uint64_t to_bits(double d) { union { uint64_t u; double d; } v = { .d = d }; return v.u; }

double fabs(double x) { return from_bits(to_bits(x) & 0x7FFFFFFFFFFFFFFFULL); }
double copysign(double x, double y) { return from_bits((to_bits(x) & 0x7FFFFFFFFFFFFFFFULL) | (to_bits(y) & 0x8000000000000000ULL)); }
double sqrt(double x) { return __builtin_sqrt(x); }
static LD sqrtl_(LD x) { LD r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }

/* ---- rounding ---- */
double trunc(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;                 /* 2^52 and up are all integers; also NaN and inf */
    return copysign((double)(long long)x, x);
}
double floor(double x) { double t = trunc(x); return t > x ? t - 1.0 : t; }
double ceil(double x) { double t = trunc(x); return t < x ? t + 1.0 : t; }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

/* ---- scaling ---- */
double frexp(double x, int *e) {
    uint64_t b = to_bits(x);
    int ex = (int)((b >> 52) & 0x7FF);
    if (ex == 0x7FF || x == 0) { *e = 0; return x; }
    if (ex == 0) {                                                   /* subnormal: normalise first */
        x *= 18014398509481984.0;                                    /* 2^54 */
        b = to_bits(x);
        ex = (int)((b >> 52) & 0x7FF) - 54;
    }
    *e = ex - 1022;
    return from_bits((b & 0x800FFFFFFFFFFFFFULL) | (1022ULL << 52));
}

static double pow2i(int n) {                                         /* exactly 2^n for -1022..1023 */
    return from_bits((uint64_t)(n + 1023) << 52);
}

double ldexp(double x, int e) {
    if (x == 0 || x != x || fabs(x) > 1.7976931348623157e308) return x;
    if (e > 2100) e = 2100;
    if (e < -2200) e = -2200;
    while (e > 1000) { x *= pow2i(1000); e -= 1000; }
    while (e < -1000) { x *= pow2i(-1000); e += 1000; }
    return x * pow2i(e);
}

double modf(double x, double *ip) {
    if (fabs(x) >= 4503599627370496.0) { *ip = x; return copysign(0.0, x); }
    double t = trunc(x);
    *ip = t;
    return copysign(x - t, x);
}

/* ---- the x87 primitives ---- */
static LD x_fyl2x(LD y, LD x) { LD r; __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }   /* y * log2(x) */
static LD x_f2xm1(LD x) { LD r; __asm__("f2xm1" : "=t"(r) : "0"(x)); return r; }                           /* 2^x - 1, |x| <= 1 */
static LD x_rint(LD x) { LD r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
static LD x_atan2(LD y, LD x) { LD r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }  /* atan2(y, x) */
static LD x_prem(LD x, LD y) {
    LD r;
    __asm__("1: fprem\n\tfnstsw %%ax\n\ttestb $4, %%ah\n\tjnz 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc", "st(1)");
    return r;
}

/* 2^z for an extended-precision z, as a double */
static double exp2_ld(LD z) {
    if (z > 1100) return __builtin_inf();
    if (z < -1200) return 0.0;
    LD n = x_rint(z);
    LD p = x_f2xm1(z - n) + 1.0L;                                    /* 2^fraction, in [0.7, 1.42] */
    return ldexp((double)p, (int)n);
}

double fmod(double x, double y) {
    if (x != x || y != y || y == 0 || fabs(x) > 1.7976931348623157e308) { errno = EDOM; return make_nan(); }
    if (fabs(y) > 1.7976931348623157e308) return x;
    return (double)x_prem(x, y);
}

#define TWO_PI 6.283185307179586476925286766559L

double sin(double x) {
    if (x != x || fabs(x) > 1.7976931348623157e308) { errno = EDOM; return make_nan(); }
    LD v = x;
    if (fabs(x) > 4e18) v = x_prem(v, TWO_PI);                       /* the instruction only takes arguments below 2^63 */
    LD r; __asm__("fsin" : "=t"(r) : "0"(v));
    return (double)r;
}
double cos(double x) {
    if (x != x || fabs(x) > 1.7976931348623157e308) { errno = EDOM; return make_nan(); }
    LD v = x;
    if (fabs(x) > 4e18) v = x_prem(v, TWO_PI);
    LD r; __asm__("fcos" : "=t"(r) : "0"(v));
    return (double)r;
}
double tan(double x) {
    if (x != x || fabs(x) > 1.7976931348623157e308) { errno = EDOM; return make_nan(); }
    LD v = x;
    if (fabs(x) > 4e18) v = x_prem(v, TWO_PI);
    LD one, r; __asm__("fptan" : "=t"(one), "=u"(r) : "0"(v));
    (void)one;
    return (double)r;
}

double atan2(double y, double x) {
    if (x != x || y != y) return make_nan();
    return (double)x_atan2(y, x);
}
double atan(double x) { return atan2(x, 1.0); }
double asin(double x) {
    if (x != x || fabs(x) > 1.0) { errno = EDOM; return make_nan(); }
    return (double)x_atan2(x, sqrtl_((1.0L - x) * (1.0L + x)));
}
double acos(double x) {
    if (x != x || fabs(x) > 1.0) { errno = EDOM; return make_nan(); }
    return (double)x_atan2(sqrtl_((1.0L - x) * (1.0L + x)), x);
}

double exp(double x) {
    if (x != x) return x;
    if (x > 709.782712893384) { errno = ERANGE; return __builtin_inf(); }
    if (x < -745.1332191019412) return 0.0;
    return exp2_ld((LD)x * 1.44269504088896340735992468100189214L);
}

double log(double x) {
    if (x != x) return x;
    if (x < 0) { errno = EDOM; return make_nan(); }
    if (x == 0) { errno = ERANGE; return -__builtin_inf(); }
    if (x > 1.7976931348623157e308) return x;
    return (double)x_fyl2x(0.693147180559945309417232121458176568L, x);
}
double log2(double x) {
    if (x != x) return x;
    if (x < 0) { errno = EDOM; return make_nan(); }
    if (x == 0) { errno = ERANGE; return -__builtin_inf(); }
    if (x > 1.7976931348623157e308) return x;
    return (double)x_fyl2x(1.0L, x);
}
double log10(double x) {
    if (x != x) return x;
    if (x < 0) { errno = EDOM; return make_nan(); }
    if (x == 0) { errno = ERANGE; return -__builtin_inf(); }
    if (x > 1.7976931348623157e308) return x;
    return (double)x_fyl2x(0.301029995663981195213738894724493027L, x);
}

double pow(double x, double y) {
    if (y == 0 || x == 1) return 1.0;
    if (x != x || y != y) return make_nan();
    double ay = fabs(y), ax = fabs(x);
    int yint = ay >= 4503599627370496.0 || ay == floor(ay);
    int yodd = yint && ay < 9007199254740992.0 && (((long long)ay) & 1);
    int neg = x < 0 || (x == 0 && 1.0 / x < 0);
    if (ay > 1.7976931348623157e308) {                                /* y is infinite */
        if (ax == 1) return 1.0;
        return ((ax > 1) == (y > 0)) ? __builtin_inf() : 0.0;
    }
    if (x == 0) {
        if (y < 0) { errno = ERANGE; return (neg && yodd) ? -__builtin_inf() : __builtin_inf(); }
        return (neg && yodd) ? -0.0 : 0.0;
    }
    if (ax > 1.7976931348623157e308) {                                /* x is infinite */
        double r = y > 0 ? __builtin_inf() : 0.0;
        return (neg && yodd) ? -r : r;
    }
    if (x < 0 && !yint) { errno = EDOM; return make_nan(); }
    double r = exp2_ld(x_fyl2x(y, ax));                               /* 2^(y * log2|x|), carried in 80 bits */
    if (r > 1.7976931348623157e308) errno = ERANGE;
    return (x < 0 && yodd) ? -r : r;
}

double hypot(double x, double y) { return sqrt(x * x + y * y); }
double cbrt(double x) {
    if (x == 0 || x != x || fabs(x) > 1.7976931348623157e308) return x;
    double r = pow(fabs(x), 1.0 / 3.0);
    r = r - (r * r * r - fabs(x)) / (3 * r * r);                      /* one Newton step cleans up the last bits */
    return x < 0 ? -r : r;
}
double sinh(double x) { double e = exp(x); return (e - 1.0 / e) / 2; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) / 2; }
double tanh(double x) {
    if (x > 20) return 1.0;
    if (x < -20) return -1.0;
    double e = exp(2 * x);
    return (e - 1) / (e + 1);
}
