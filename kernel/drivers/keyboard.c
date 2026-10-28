#include "keyboard.h"
#include "interrupt.h"
#include "apic.h"
#include "sync.h"
#include "spinlock.h"
#include "io.h"

/* Scancode set 1 (what QEMU's PS/2 controller delivers), keys 0x00-0x39. */
static const char normal[58] __attribute__((nonstring)) =
    "\0\0" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char shifted[58] __attribute__((nonstring)) =
    "\0\0" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

static int shift, caps, ctrl;

/* Typed characters wait here until a task reads them with kbd_getc(). */
#define QSIZE 128
static char queue[QSIZE];
static volatile unsigned q_head, q_tail;
static struct sem q_sem;

static void kbd_push(char c) {                   /* interrupt context */
    unsigned next = (q_head + 1) % QSIZE;
    if (next == q_tail) return;                  /* full: drop the key */
    queue[q_head] = c;
    q_head = next;
    sem_post(&q_sem);
}

char kbd_getc(void) {
    sem_wait(&q_sem);
    uint64_t f = irq_save();
    char c = queue[q_tail];
    q_tail = (q_tail + 1) % QSIZE;
    irq_restore(f);
    return c;
}

static void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    int release = sc & 0x80;
    sc &= 0x7F;

    if (sc == 0x2A || sc == 0x36) { shift = !release; return; }
    if (sc == 0x1D) { ctrl = !release; return; }
    if (release) return;
    if (sc == 0x3A) { caps = !caps; return; }
    if (sc >= sizeof normal) return;

    char c = (shift ? shifted : normal)[sc];
    if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;                            /* flip letter case */
    if (!c) return;
    if (ctrl && ((c | 0x20) >= 'a' && (c | 0x20) <= 'z')) c &= 0x1F;     /* Ctrl-D is 4 */

    kbd_push(c);
}

/* The bootloader may leave the 8042 PS/2 controller in any state, so set it
 * up ourselves: enable port 1 and its IRQ, then tell the keyboard to scan. */
static void kbc_wait_write(void) { for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ; }
static void kbc_wait_read(void)  { for (int i = 0; i < 100000 && !(inb(0x64) & 1); i++) ; }

void keyboard_init(void) {
    while (inb(0x64) & 1) inb(0x60);              /* flush stale bytes */
    kbc_wait_write(); outb(0x64, 0xAE);           /* enable first PS/2 port */
    kbc_wait_write(); outb(0x64, 0x20);           /* read controller config byte */
    kbc_wait_read();  uint8_t cfg = inb(0x60);
    cfg |= 0x01;                                  /* bit 0: IRQ1 enabled */
    cfg &= ~0x10;                                 /* bit 4: port 1 clock on */
    kbc_wait_write(); outb(0x64, 0x60);           /* write config byte */
    kbc_wait_write(); outb(0x60, cfg);
    kbc_wait_write(); outb(0x60, 0xF4);           /* keyboard: enable scanning */
    kbc_wait_read();  inb(0x60);                  /* ACK (0xFA) */

    sem_init(&q_sem, 0);
    irq_register(1, keyboard_irq);
    irq_enable(1);
}
