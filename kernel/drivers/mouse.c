#include "input.h"
#include "interrupt.h"
#include "apic.h"
#include "io.h"
#include "kprintf.h"

/* PS/2 mouse (the second port of the keyboard controller). Plain mice send 3-byte packets; if the mouse
 * accepts the "IntelliMouse" handshake it also reports the scroll wheel in a 4th byte. */

static int have_wheel;
static uint8_t packet[4];
static int count;

static int wait_write(void) { for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) return 0; return -1; }
static int wait_read(void)  { for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) return 0; return -1; }

static void ctl_write(uint8_t cmd) { wait_write(); outb(0x64, cmd); }

/* Send one byte to the mouse and return its answer (0xFA means "acknowledged"). */
static int mouse_cmd(uint8_t b) {
    ctl_write(0xD4);                                   /* the next data byte goes to the mouse */
    wait_write(); outb(0x60, b);
    if (wait_read() < 0) return -1;
    return inb(0x60);
}

static void mouse_irq(void) {
    uint8_t b = inb(0x60);
    if (count == 0 && !(b & 8)) return;                /* the first byte always has bit 3 set: resynchronise */
    packet[count++] = b;
    if (count < (have_wheel ? 4 : 3)) return;
    count = 0;
    int dx = packet[1] - ((packet[0] << 4) & 0x100);
    int dy = packet[2] - ((packet[0] << 3) & 0x100);
    if (packet[0] & 0xC0) return;                      /* overflow: ignore this packet */
    int wheel = 0;
    if (have_wheel) { wheel = packet[3] & 0x0F; if (wheel & 8) wheel -= 16; }
    input_mouse(dx, -dy, -wheel, packet[0] & 7);       /* the mouse's y axis points up; the screen's points down */
}

void mouse_init(void) {
    ctl_write(0xA8);                                   /* enable the second port */
    ctl_write(0x20); wait_read();
    uint8_t cfg = inb(0x60);
    cfg |= 2;                                          /* interrupt on mouse data */
    cfg &= ~0x20;                                      /* clock on */
    ctl_write(0x60); wait_write(); outb(0x60, cfg);

    if (mouse_cmd(0xF6) != 0xFA) { kprintf("mouse: none found\n"); return; }      /* defaults */
    int rates[3] = { 200, 100, 80 };                   /* the magic sequence that turns on the wheel */
    for (int i = 0; i < 3; i++) { mouse_cmd(0xF3); mouse_cmd((uint8_t)rates[i]); }
    mouse_cmd(0xF2);
    if (wait_read() == 0) have_wheel = inb(0x60) == 3;
    mouse_cmd(0xF3); mouse_cmd(100);                   /* sample rate */
    if (mouse_cmd(0xF4) != 0xFA) { kprintf("mouse: cannot enable\n"); return; }   /* start streaming */
    irq_register(12, mouse_irq);
    irq_enable(12);
    kprintf("mouse: PS/2%s\n", have_wheel ? " with wheel" : "");
}
