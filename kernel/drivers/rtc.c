#include "io.h"
#include "task.h"
#include "kprintf.h"

/* The battery-backed clock (CMOS RTC). Read once at boot; after that wall time is that moment plus
 * the time the kernel has been running. QEMU's RTC follows the host's clock in UTC. */

static uint64_t boot_epoch;

static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }

static int days_from_civil(int y, int m, int d) {       /* days since 1970-01-01 (proleptic Gregorian) */
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

void rtc_init(void) {
    uint8_t s, mi, h, d, mo, y, regb;
    for (int tries = 0; tries < 5; tries++) {
        while (cmos(0x0A) & 0x80) ;                               /* an update is in progress: wait */
        s = cmos(0); mi = cmos(2); h = cmos(4); d = cmos(7); mo = cmos(8); y = cmos(9);
        uint8_t s2 = cmos(0);
        if (s == s2) break;                                       /* read the same second twice: consistent */
    }
    regb = cmos(0x0B);
    if (!(regb & 4)) {                                            /* values are BCD */
        s = (s & 15) + (s >> 4) * 10; mi = (mi & 15) + (mi >> 4) * 10;
        h = ((h & 0x0F) + ((h & 0x70) >> 4) * 10) | (h & 0x80);
        d = (d & 15) + (d >> 4) * 10; mo = (mo & 15) + (mo >> 4) * 10; y = (y & 15) + (y >> 4) * 10;
    }
    if (!(regb & 2) && (h & 0x80)) h = ((h & 0x7F) + 12) % 24;    /* 12-hour clock, PM */
    int year = 2000 + y;
    boot_epoch = (uint64_t)days_from_civil(year, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    uint64_t up = uptime_ms() / 1000;
    boot_epoch -= up;                                             /* so that boot_epoch + uptime = now */
    kprintf("RTC: %04d-%02d-%02d %02d:%02d:%02d UTC\n", year, mo, d, h, mi, s);
}

uint64_t wall_time(void) { return boot_epoch + uptime_ms() / 1000; }
