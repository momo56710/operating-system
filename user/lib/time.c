#include <time.h>
#include <string.h>
#include <stdio.h>
#include "sys.h"

static struct tm tmbuf;

static long days_from_civil(long y, unsigned m, unsigned d) {       /* days since 1970-01-01; m is 1..12 */
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, long *y, unsigned *m, unsigned *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + (*m <= 2);
}

time_t time(time_t *t) { time_t v = time_seconds(); if (t) *t = v; return v; }

clock_t clock(void) {
    static long base = -1;
    long now = (long)uptime_ms();
    if (base < 0) base = now;
    return (now - base) * 1000;
}

double difftime(time_t a, time_t b) { return (double)(a - b); }

struct tm *gmtime(const time_t *t) {
    long s = *t, days = s / 86400, rem = s % 86400;
    if (rem < 0) { rem += 86400; days--; }
    long y; unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    tmbuf.tm_year = (int)(y - 1900); tmbuf.tm_mon = (int)m - 1; tmbuf.tm_mday = (int)d;
    tmbuf.tm_hour = (int)(rem / 3600); tmbuf.tm_min = (int)(rem % 3600 / 60); tmbuf.tm_sec = (int)(rem % 60);
    tmbuf.tm_wday = (int)(((days % 7) + 11) % 7);                    /* 1970-01-01 was a Thursday */
    tmbuf.tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tmbuf.tm_isdst = 0;
    return &tmbuf;
}

struct tm *localtime(const time_t *t) { return gmtime(t); }           /* no time zones: UTC */

time_t mktime(struct tm *tm) {
    long y = tm->tm_year + 1900L, mon = tm->tm_mon;
    y += mon / 12; mon %= 12;
    if (mon < 0) { mon += 12; y--; }
    long days = days_from_civil(y, (unsigned)mon + 1, 1) + (tm->tm_mday - 1);
    time_t t = days * 86400 + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
    *tm = *gmtime(&t);                                                /* normalise every field */
    return t;
}

static const char *wday_names[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *mon_names[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

static int is_leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* ISO 8601 week number; *year gets the week-based year. */
static int iso_week(const struct tm *tm, long *year) {
    long y = tm->tm_year + 1900L;
    int wd = (tm->tm_wday + 6) % 7;                                   /* Monday = 0 */
    int week = (tm->tm_yday - wd + 10) / 7;
    if (week < 1) { y--; week = (tm->tm_yday + (is_leap(y) ? 366 : 365) - wd + 10) / 7; }
    else if (week == 53) {
        int days_in_year = is_leap(y) ? 366 : 365;
        if (tm->tm_yday - wd + 3 >= days_in_year) { week = 1; y++; }
    }
    *year = y;
    return week;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    size_t n = 0;
    for (; *fmt; fmt++) {
        char tmp[64];
        const char *piece = tmp;
        if (*fmt != '%') { tmp[0] = *fmt; tmp[1] = 0; }
        else {
            fmt++;
            if (*fmt == 'E' || *fmt == 'O') fmt++;                    /* alternative forms are the same in the C locale */
            int h12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
            long isoy;
            switch (*fmt) {
            case 'a': snprintf(tmp, sizeof tmp, "%.3s", wday_names[tm->tm_wday]); break;
            case 'A': piece = wday_names[tm->tm_wday]; break;
            case 'b': case 'h': snprintf(tmp, sizeof tmp, "%.3s", mon_names[tm->tm_mon]); break;
            case 'B': piece = mon_names[tm->tm_mon]; break;
            case 'c': strftime(tmp, sizeof tmp, "%a %b %e %H:%M:%S %Y", tm); break;
            case 'C': snprintf(tmp, sizeof tmp, "%02d", (tm->tm_year + 1900) / 100); break;
            case 'd': snprintf(tmp, sizeof tmp, "%02d", tm->tm_mday); break;
            case 'D': strftime(tmp, sizeof tmp, "%m/%d/%y", tm); break;
            case 'e': snprintf(tmp, sizeof tmp, "%2d", tm->tm_mday); break;
            case 'F': strftime(tmp, sizeof tmp, "%Y-%m-%d", tm); break;
            case 'g': iso_week(tm, &isoy); snprintf(tmp, sizeof tmp, "%02ld", isoy % 100); break;
            case 'G': iso_week(tm, &isoy); snprintf(tmp, sizeof tmp, "%ld", isoy); break;
            case 'H': snprintf(tmp, sizeof tmp, "%02d", tm->tm_hour); break;
            case 'I': snprintf(tmp, sizeof tmp, "%02d", h12); break;
            case 'j': snprintf(tmp, sizeof tmp, "%03d", tm->tm_yday + 1); break;
            case 'm': snprintf(tmp, sizeof tmp, "%02d", tm->tm_mon + 1); break;
            case 'M': snprintf(tmp, sizeof tmp, "%02d", tm->tm_min); break;
            case 'n': piece = "\n"; break;
            case 'p': piece = tm->tm_hour < 12 ? "AM" : "PM"; break;
            case 'r': strftime(tmp, sizeof tmp, "%I:%M:%S %p", tm); break;
            case 'R': strftime(tmp, sizeof tmp, "%H:%M", tm); break;
            case 'S': snprintf(tmp, sizeof tmp, "%02d", tm->tm_sec); break;
            case 't': piece = "\t"; break;
            case 'T': case 'X': strftime(tmp, sizeof tmp, "%H:%M:%S", tm); break;
            case 'u': snprintf(tmp, sizeof tmp, "%d", tm->tm_wday ? tm->tm_wday : 7); break;
            case 'U': snprintf(tmp, sizeof tmp, "%02d", (tm->tm_yday + 7 - tm->tm_wday) / 7); break;
            case 'V': snprintf(tmp, sizeof tmp, "%02d", iso_week(tm, &isoy)); break;
            case 'w': snprintf(tmp, sizeof tmp, "%d", tm->tm_wday); break;
            case 'W': snprintf(tmp, sizeof tmp, "%02d", (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7); break;
            case 'x': strftime(tmp, sizeof tmp, "%m/%d/%y", tm); break;
            case 'y': snprintf(tmp, sizeof tmp, "%02d", (tm->tm_year + 1900) % 100); break;
            case 'Y': snprintf(tmp, sizeof tmp, "%d", tm->tm_year + 1900); break;
            case 'z': piece = "+0000"; break;
            case 'Z': piece = "GMT"; break;
            case '%': piece = "%"; break;
            default: tmp[0] = '%'; tmp[1] = *fmt; tmp[2] = 0;
            }
        }
        size_t len = strlen(piece);
        if (n + len >= max) return 0;
        memcpy(s + n, piece, len);
        n += len;
        if (!*fmt) break;
    }
    if (n >= max) return 0;
    s[n] = 0;
    return n;
}
