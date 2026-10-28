#include "libc.h"

/* mandel: the Mandelbrot set in ASCII, using 16.16 fixed-point because user programs have no
 * floating point yet. */
#define ONE 65536L

int main(void) {
    static const char shades[] = " .:-=+*#%@";
    const int w = 70, h = 22, max_iter = 60;
    for (int py = 0; py < h; py++) {
        char row[80];
        long ci = -(12 * ONE) / 10 + (py * (24 * ONE) / 10) / h;
        for (int px = 0; px < w; px++) {
            long cr = -(22 * ONE) / 10 + (px * (30 * ONE) / 10) / w;
            long zr = 0, zi = 0;
            int it = 0;
            while (it < max_iter) {
                long zr2 = (zr * zr) >> 16, zi2 = (zi * zi) >> 16;
                if (zr2 + zi2 > 4 * ONE) break;
                zi = ((2 * zr * zi) >> 16) + ci;
                zr = zr2 - zi2 + cr;
                it++;
            }
            row[px] = it == max_iter ? '@' : shades[it * 9 / max_iter];
        }
        row[w] = 0;
        puts(row);
    }
    return 0;
}
