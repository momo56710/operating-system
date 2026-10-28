/* Phase 0 stub: proves the bootloader hands off to our code.
 * Replaced by the real serial driver and kprintf in Phase 1. */

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_puts(const char *s) {
    while (*s) outb(0x3F8, (unsigned char)*s++);
}

/* Keeps the .data segment non-empty so the ELF has a valid rw- PT_LOAD.
 * Goes away in Phase 1 when the Limine request block lives in .data. */
static volatile int boot_marker = 1;

void kmain(void) {
    boot_marker++;
    serial_puts("LeOS: kernel reached, halting\n");
    for (;;) __asm__ volatile ("hlt");
}
