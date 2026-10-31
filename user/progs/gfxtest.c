#include "libc.h"
#include "abi.h"
#include "sys.h"

/* Smoke test for the graphics and input system calls: paint the screen, draw a square that follows the
 * mouse, and report what arrived. Esc quits. */
static unsigned *fb;
static struct gfx_info gi;

static void rect(int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && j >= 0 && i < (int)gi.width && j < (int)gi.height) fb[j * (gi.pitch / 4) + i] = c;
}

int main(void) {
    if (syscall3(SYS_GFX, GFX_INFO, (long)&gi, 0) < 0) { printf("no framebuffer\n"); return 1; }
    fb = (unsigned *)syscall3(SYS_GFX, GFX_MAP, 0, 0);
    if ((long)fb <= 0) { printf("cannot map the framebuffer\n"); return 1; }
    if (syscall3(SYS_GFX, GFX_ACQUIRE, 0, 0) < 0) { printf("screen is busy\n"); return 1; }
    rect(0, 0, gi.width, gi.height, 0x000000);
    rect(20, 20, gi.width - 40, gi.height - 40, 0xffffff);
    rect(22, 22, gi.width - 44, gi.height - 44, 0x000000);
    int keys = 0, moves = 0, clicks = 0, wheel = 0, lastx = 0, lasty = 0, done = 0;
    unsigned prev = 0;
    struct input_event ev[16];
    struct gfx_poll p = { (unsigned long)ev, 16, 20000, 0, 0 };
    while (!done) {
        p.timeout_ms = 20000;
        syscall3(SYS_GFX, GFX_POLL, (long)&p, 0);
        if (!p.count) break;
        for (unsigned i = 0; i < p.count; i++) {
            struct input_event *e = &ev[i];
            if (e->type == EV_KEY) {
                if (e->value) keys++;
                if (e->value && e->code == 1) done = 1;
                if (e->value && e->ch) rect(40 + (keys % 40) * 12, 60, 10, 10, 0xffffff);
            } else {
                moves++;
                if (e->buttons & ~prev) clicks++;
                prev = e->buttons;
                wheel += e->wheel;
                rect(lastx - 4, lasty - 4, 8, 8, 0x000000);
                lastx = e->x; lasty = e->y;
                rect(lastx - 4, lasty - 4, 8, 8, e->buttons ? 0xff4040 : 0xffffff);
            }
        }
    }
    syscall3(SYS_GFX, GFX_RELEASE, 0, 0);
    printf("gfxtest: %ux%u pitch %u shifts r%u g%u b%u; %d keys, %d mouse events, %d clicks, wheel %d, last pos %d,%d\n",
           gi.width, gi.height, gi.pitch, gi.red_shift, gi.green_shift, gi.blue_shift, keys, moves, clicks, wheel, lastx, lasty);
    return 0;
}
