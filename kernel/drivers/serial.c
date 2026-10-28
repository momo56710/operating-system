#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* no interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on to set the baud divisor */
    outb(COM1 + 0, 0x03);   /* divisor 3 = 38400 baud (low byte) */
    outb(COM1 + 1, 0x00);   /* (high byte) */
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);   /* enable and clear FIFOs, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20))   /* wait for transmit holding register empty */
        ;
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}
