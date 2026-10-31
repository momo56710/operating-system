/* libctest: a differential test of the C library. This one file is compiled twice, for the host (glibc)
 * and for LeOS, and the two outputs are compared line by line. Everything printed is deterministic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <float.h>

static void section(const char *name) { printf("=== %s\n", name); }

static void test_printf_ints(void) {
    section("printf integers");
    long vals[] = { 0, 1, -1, 42, -42, 255, 65535, 123456789, 2147483647, -2147483647 - 1 };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        int v = (int)vals[i];
        printf("%d|%5d|%-5d|%05d|%+d|% d|%x|%X|%#x|%o|%#o|%u|%.3d|%8.3d|%-8.3d|\n", v, v, v, v, v, v, v, v, v, v, v, v, v, v, v);
    }
    long long big[] = { 9223372036854775807LL, -9223372036854775807LL - 1, 4294967296LL, -1LL };
    for (unsigned i = 0; i < 4; i++) printf("%lld|%llu|%llx|%20lld|%-20llx|\n", big[i], big[i], big[i], big[i], big[i]);
    printf("%.0d|%5.0d|%#.0o|%hhd|%hd|%ld|%zu|\n", 0, 0, 0, 300, 70000, -5L, (size_t)99);
    printf("%s|%10s|%-10s|%.2s|%10.3s|%-10.3s|%s\n", "hello", "hello", "hello", "hello", "hello", "hello", "");
    printf("%c|%5c|%-5c|%%|%5%|\n", 'a', 'b', 'c');
    printf("%*d|%-*d|%.*d|%*.*d\n", 6, 42, 6, 42, 4, 7, 8, 3, 9);
}

static void test_printf_doubles(void) {
    section("printf doubles");
    double vals[] = { 0.0, 1.0, 0.1, 0.5, 1.5, 2.5, 3.14159265358979, 1e10, 1e-10, 123456789.0, 1.7976931348623157e308,
        4.9406564584124654e-324, 2.2250738585072014e-308, 1.0 / 3, 100.0, 1e15, 1e16, 1e17, 123456.789, 0.000123456, 99999.5,
        0.99999995, 9.9999999e22, 5e-5, 1234.5678, 0.3, 2.0 / 3, 1e100, 1e-100, 255.0, 1e21, 12345678901234567.0 };
    const char *fmts[] = { "%f", "%.0f", "%.2f", "%10.3f", "%-12.1f|", "%+.2f", "%e", "%.0e", "%.3e", "%E", "%g", "%.3g", "%.10g",
        "%.14g", "%.17g", "%G", "%#g", "%#.0f", "%012.2f", "%a", "%.3a", "%10.4g|", "% f", "%.20f", "%.0g" };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++)
        for (unsigned j = 0; j < sizeof fmts / sizeof *fmts; j++) {
            char fmt[40];
            snprintf(fmt, sizeof fmt, "%s", fmts[j]);
            printf("[%02u %-7s] ", i, fmts[j]);
            printf(fmt, vals[i]);
            printf("\n");
            if (vals[i] != 0) { printf("[%02u -%-6s] ", i, fmts[j]); printf(fmt, -vals[i]); printf("\n"); }
        }
    printf("%f %f %f %e %g\n", 1.0 / 0.0, -1.0 / 0.0, __builtin_nan(""), 1.0 / 0.0, -1.0 / 0.0);
    printf("%5.1f|%-8g|%08f|%+g\n", 1.0 / 0.0, 1.0 / 0.0, -1.0 / 0.0, __builtin_nan(""));
    printf("%.0f %.0f %.0f %.0f %.1f %.1f\n", 0.5, 1.5, 2.5, 3.5, 0.25, 0.35);
}

static void test_strtod(void) {
    section("strtod");
    const char *in[] = { "0", "1", "-1.5", "3.14159", "1e10", "1e-10", "1E+5", ".5", "5.", "0x1p4", "0x1.8p1", "0xA", "inf", "-inf",
        "nan", "  42abc", "1e400", "1e-400", "123456789012345678901234567890", "0.1", "0.2", "0.30000000000000004", "9007199254740993",
        "4.9e-324", "2.2250738585072014e-308", "1.7976931348623157e308", "1.5e3x", "-.25e1", "+7", "abc", "", "1e", "1e+", "0.000001",
        "123.456e-2", "6.02214076e23", "1.2345678901234567890", "100", "99999999999999999999" };
    for (unsigned i = 0; i < sizeof in / sizeof *in; i++) {
        char *end;
        errno = 0;
        double v = strtod(in[i], &end);
        printf("[%s] -> %.17g end=%d errno=%d\n", in[i], v, (int)(end - in[i]), errno != 0);
    }
    const char *ints[] = { "0", "42", "-17", "  99x", "0x1F", "077", "2147483647", "2147483648", "-2147483649", "9223372036854775807",
        "9223372036854775808", "18446744073709551615", "18446744073709551616", "zz", "" };
    for (unsigned i = 0; i < sizeof ints / sizeof *ints; i++) {
        char *end;
        errno = 0;
        long l = strtol(ints[i], &end, 0);
        unsigned long ul = strtoul(ints[i], &end, 0);
        long l36 = strtol(ints[i], &end, 36);
        printf("[%s] strtol=%ld strtoul=%lu base36=%ld end=%d erange=%d atoi=%d\n", ints[i], l, ul, l36, (int)(end - ints[i]), errno == ERANGE, atoi(ints[i]));
    }
}

static void test_math(void) {
    section("math");
    double xs[] = { -2.5, -1, -0.5, 0, 0.3, 0.5, 1, 1.5, 2, 3, 10, 100, 1e-5, 12345.6789, 700, -700 };
    for (unsigned i = 0; i < sizeof xs / sizeof *xs; i++) {
        double x = xs[i];
        printf("x=%g: sin %.12g cos %.12g tan %.12g atan %.12g exp %.12g sqrt %.12g floor %g ceil %g fabs %g sinh %.10g\n", x,
            sin(x), cos(x), tan(x), atan(x), exp(x), x >= 0 ? sqrt(x) : 0.0, floor(x), ceil(x), fabs(x), sinh(x));
        if (x > 0) printf("   log %.12g log2 %.12g log10 %.12g cbrt %.10g\n", log(x), log2(x), log10(x), cbrt(x));
        if (fabs(x) <= 1) printf("   asin %.12g acos %.12g\n", asin(x), acos(x));
    }
    double ys[] = { -3, -1, 0, 0.5, 1, 2, 3, 10 };
    for (unsigned i = 0; i < sizeof xs / sizeof *xs; i++)
        for (unsigned j = 0; j < sizeof ys / sizeof *ys; j++) {
            double x = xs[i], y = ys[j];
            if (fabs(x) > 100 && fabs(y) > 3) continue;
            printf("x=%g y=%g: pow %.12g atan2 %.12g fmod %.12g\n", x, y, pow(x, y), atan2(x, y), y != 0 ? fmod(x, y) : 0.0);
        }
    printf("pow(2,10)=%.17g pow(10,2)=%.17g pow(2,0.5)=%.17g pow(2,-1)=%.17g pow(-8,3)=%.17g pow(0,0)=%g pow(0,-1)=%g\n",
        pow(2, 10), pow(10, 2), pow(2, 0.5), pow(2, -1), pow(-8, 3), pow(0, 0), pow(0, -1));
    int e;
    double m = frexp(48.0, &e);
    double ip, fp = modf(-3.75, &ip);
    printf("frexp %g %d  ldexp %g %g  modf %g %g  round %g %g %g trunc %g %g\n", m, e, ldexp(0.75, 4), ldexp(1.0, -1074), fp, ip, round(2.5), round(-2.5), round(0.4), trunc(-2.7), trunc(2.7));
    printf("exp(1)=%.12g pi=%.12g sqrt2=%.17g hypot=%.12g\n", exp(1), 4 * atan(1), sqrt(2), hypot(3, 4));
}

static void test_strings(void) {
    section("strings");
    char buf[64];
    printf("%zu %zu %d %d %d\n", strlen("hello"), strlen(""), strcmp("a", "b") < 0, strcmp("b", "a") > 0, strcmp("abc", "abc"));
    printf("%d %d\n", strncmp("abcd", "abcf", 3), strncmp("abcd", "abcf", 4) < 0);
    strcpy(buf, "foo"); strcat(buf, "bar"); strncat(buf, "bazqux", 3);
    printf("%s|%s|%s|%s\n", buf, strchr(buf, 'b'), strrchr(buf, 'a'), strstr(buf, "rba"));
    printf("%zu %zu %s %d\n", strspn("aabbcc", "ab"), strcspn("hello world", " "), strpbrk("hello", "lo"), strchr("abc", 'z') == NULL);
    memset(buf, 'x', 5); buf[5] = 0;
    printf("%s %d %d %s\n", buf, memcmp("abc", "abd", 3) < 0, memchr("hello", 'l', 5) != NULL, (char *)memchr("hello", 'l', 5));
    char a[16] = "0123456789";
    memmove(a + 2, a, 8);
    printf("%s\n", a);
    memmove(a, a + 3, 5);
    printf("%s\n", a);
    printf("%s|%s|%s\n", strerror(ENOENT), strerror(EINVAL), strerror(ERANGE));
    int sum = 0;
    for (int c = 0; c < 256; c++) sum += (!!isalpha(c) << 0) + (!!isdigit(c) << 1) + (!!isspace(c) << 2) + (!!isupper(c) << 3) + (!!islower(c) << 4) +
                                        (!!ispunct(c) << 5) + (!!isalnum(c) << 6) + (!!isxdigit(c) << 7) + (!!isprint(c) << 8) + (!!iscntrl(c) << 9) + (!!isgraph(c) << 10);
    printf("ctype checksum %d, %c%c %c%c\n", sum, toupper('a'), toupper('Z'), tolower('A'), tolower('z'));
    char tiny[8];
    int n0 = snprintf(tiny, 8, "%s", "0123456789"), n1 = snprintf(tiny, 1, "abc"), n2 = snprintf(NULL, 0, "%d%d", 12, 345), n3 = snprintf(tiny, 5, "%d", 123456);
    printf("%d %d %d %d [%s]\n", n0, n1, n2, n3, tiny);
}

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

static void test_sorting(void) {
    section("qsort / bsearch / rand-free");
    static int a[3000];
    unsigned s = 12345;
    for (int i = 0; i < 3000; i++) { s = s * 1103515245u + 12345u; a[i] = (int)((s >> 8) % 100000); }
    qsort(a, 3000, sizeof a[0], cmp_int);
    int sorted = 1;
    long sum = 0;
    for (int i = 0; i < 3000; i++) { if (i && a[i - 1] > a[i]) sorted = 0; sum += (long)a[i] * (i + 1); }
    printf("sorted=%d min=%d max=%d weighted=%ld\n", sorted, a[0], a[2999], sum);
    int key = a[1234], *found = bsearch(&key, a, 3000, sizeof a[0], cmp_int);
    int missing = -5;
    printf("found=%d missing=%d\n", found != NULL && *found == key, bsearch(&missing, a, 3000, sizeof a[0], cmp_int) == NULL);
    char words[5][8] = { "pear", "apple", "fig", "banana", "cherry" };
    qsort(words, 5, 8, (int (*)(const void *, const void *))strcmp);
    for (int i = 0; i < 5; i++) printf("%s ", words[i]);
    printf("\n");
}

static void test_stdio(void) {
    section("stdio files");
    const char *path = "/tmp/libctest.txt";
    FILE *f = fopen(path, "w");
    if (!f) { printf("cannot create %s\n", path); return; }
    for (int i = 1; i <= 5; i++) fprintf(f, "line %d: %05.1f %s\n", i, i * 1.5, "text");
    fputs("tail without newline", f);
    printf("ftell after writing: %ld\n", ftell(f));
    fclose(f);

    f = fopen(path, "r");
    char line[100];
    int n = 0;
    while (fgets(line, sizeof line, f)) { n++; if (n <= 2 || n == 6) printf("got: %s%s", line, strchr(line, '\n') ? "" : "\n"); }
    printf("lines=%d eof=%d error=%d ftell=%ld\n", n, feof(f), ferror(f), ftell(f));
    rewind(f);
    int c = fgetc(f), c2 = fgetc(f);
    ungetc(c2, f);
    printf("chars %c %c then %c, ftell=%ld\n", c, c2, fgetc(f), ftell(f));
    fseek(f, 5, SEEK_SET);
    char b4[5] = { 0 };
    size_t r = fread(b4, 1, 4, f);
    printf("read %zu [%s] at %ld\n", r, b4, ftell(f));
    fseek(f, -4, SEEK_END);
    r = fread(b4, 1, 4, f);
    printf("tail [%s] %zu eof=%d\n", b4, r, feof(f));
    fseek(f, 0, SEEK_END);
    printf("size=%ld\n", ftell(f));
    fclose(f);

    f = fopen(path, "a");
    fprintf(f, "\nappended %d", 42);
    fclose(f);
    f = fopen(path, "r+");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    fputs("LINE", f);
    fseek(f, 0, SEEK_SET);
    fgets(line, sizeof line, f);
    printf("after update: size=%ld first=%s", size, line);
    fclose(f);

    f = fopen(path, "rb");
    char *all = malloc((size_t)size + 1);
    size_t got = fread(all, 1, (size_t)size, f);
    all[got] = 0;
    unsigned h = 5381;
    for (size_t i = 0; i < got; i++) h = h * 33 + (unsigned char)all[i];
    printf("read back %zu bytes, hash %u\n", got, h);
    free(all);
    fclose(f);
    printf("fopen missing: %s, errno=%d\n", fopen("/tmp/definitely/not/here", "r") ? "opened" : "NULL", errno == ENOENT);
    printf("remove: %d, again: %d\n", remove(path), remove(path));

    char sbuf[100];
    int len = sprintf(sbuf, "%d-%s-%c-%.2f", 7, "x", 'y', 2.5);
    printf("sprintf %d [%s]\n", len, sbuf);
}

static jmp_buf jb;
static int depth_reached;
static void thrower(int depth, int val) { depth_reached = depth; if (depth == 0) longjmp(jb, val); thrower(depth - 1, val); }

static void test_setjmp(void) {
    section("setjmp");
    volatile int local = 5;
    int r = setjmp(jb);
    if (r == 0) { local = 6; thrower(10, 7); printf("not reached\n"); }
    printf("returned %d, local=%d, depth=%d\n", r, local, depth_reached);
    r = setjmp(jb);
    if (r == 0) longjmp(jb, 0);
    printf("longjmp(env, 0) made setjmp return %d\n", r);
}

static void test_malloc(void) {
    section("malloc");
    enum { N = 400 };
    static unsigned char *p[N];
    static size_t sz[N];
    unsigned s = 99;
    int bad = 0;
    for (int it = 0; it < 6000; it++) {
        s = s * 1103515245u + 12345u;
        int i = (int)((s >> 8) % N);
        if (p[i]) {
            for (size_t k = 0; k < sz[i]; k++) if (p[i][k] != (unsigned char)(i + k)) { bad++; break; }
            if (it % 3 == 0) {                                          /* grow or shrink in place or by moving */
                size_t ns = 1 + ((s >> 4) % 5000);
                unsigned char *q = realloc(p[i], ns);
                if (!q) { bad++; continue; }
                size_t keep = ns < sz[i] ? ns : sz[i];
                for (size_t k = 0; k < keep; k++) if (q[k] != (unsigned char)(i + k)) { bad++; break; }
                for (size_t k = 0; k < ns; k++) q[k] = (unsigned char)(i + k);
                p[i] = q; sz[i] = ns;
            } else { free(p[i]); p[i] = 0; }
        } else {
            sz[i] = 1 + ((s >> 4) % 3000);
            p[i] = malloc(sz[i]);
            if (!p[i]) { bad++; continue; }
            for (size_t k = 0; k < sz[i]; k++) p[i][k] = (unsigned char)(i + k);
        }
    }
    for (int i = 0; i < N; i++) free(p[i]);
    int *z = calloc(1000, sizeof *z), zero = 1;
    for (int i = 0; i < 1000; i++) if (z[i]) zero = 0;
    free(z);
    char *big = malloc(5u << 20);
    memset(big, 7, 5u << 20);
    big = realloc(big, 9u << 20);
    printf("malloc stress bad=%d calloc zero=%d big ok=%d\n", bad, zero, big && big[(5u << 20) - 1] == 7);
    free(big);
}

static void test_time(void) {
    section("time");
    time_t ts[] = { 0, 86399, 86400, 951782400, 1000000000, 1234567890, 1700000000, 2147483647, 4102444800LL, -1, -86400 };
    for (unsigned i = 0; i < sizeof ts / sizeof *ts; i++) {
        time_t t = ts[i];
        struct tm *tm = gmtime(&t);
        char buf[200];
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %a %A %b %B %j %U %W %V %G %g %u %w %y %C %e %I %p %D %F %T %R %Z %% %x %X %c", tm);
        printf("%ld: %s\n", (long)t, buf);
        struct tm copy = *tm;
        printf("   mktime round trip %ld wday=%d yday=%d\n", (long)mktime(&copy), tm->tm_wday, tm->tm_yday);
    }
    struct tm odd = { 0 };
    odd.tm_year = 100; odd.tm_mon = 14; odd.tm_mday = 35; odd.tm_hour = 25; odd.tm_min = 61; odd.tm_sec = 61;   /* out-of-range fields get normalised */
    time_t t = mktime(&odd);
    printf("normalised: %ld -> %04d-%02d-%02d %02d:%02d:%02d\n", (long)t, odd.tm_year + 1900, odd.tm_mon + 1, odd.tm_mday, odd.tm_hour, odd.tm_min, odd.tm_sec);
    char tiny[4];
    printf("strftime too small: %zu\n", strftime(tiny, sizeof tiny, "%Y-%m-%d", gmtime(&(time_t){ 0 })));
    printf("difftime %g\n", difftime(100, 40));
}

int main(void) {
    test_printf_ints();
    test_printf_doubles();
    test_strtod();
    test_math();
    test_strings();
    test_sorting();
    test_stdio();
    test_setjmp();
    test_malloc();
    test_time();
    printf("=== done\n");
    return 0;
}
