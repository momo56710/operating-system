#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void io_wait(void) { outb(0x80, 0); }

/* The legacy PIC is remapped away from the CPU exception vectors and then silenced;
 * interrupts come from the I/O APIC instead (see apic.c). */
void pic_init(void) {
    outb(PIC1_CMD, 0x11); io_wait();          /* ICW1: init, expect ICW4 */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, IRQ_BASE); io_wait();     /* ICW2: vector offsets */
    outb(PIC2_DATA, IRQ_BASE + 8); io_wait();
    outb(PIC1_DATA, 4); io_wait();            /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 2); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();         /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFF);                    /* mask everything */
    outb(PIC2_DATA, 0xFF);
}

void pic_mask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (1 << (irq & 7)));
}

void pic_unmask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) & ~(1 << (irq & 7)));
    if (irq >= 8) pic_unmask(2);              /* the cascade line must be open too */
}

void pic_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

/* IRQ7 / IRQ15 can fire spuriously; the in-service register tells us. */
int pic_is_spurious(int irq) {
    if (irq != 7 && irq != 15) return 0;
    uint16_t cmd = irq == 7 ? PIC1_CMD : PIC2_CMD;
    outb(cmd, 0x0B);                          /* read ISR */
    int spurious = !(inb(cmd) & 0x80);
    if (spurious && irq == 15) outb(PIC1_CMD, 0x20);  /* master still needs an EOI */
    return spurious;
}
