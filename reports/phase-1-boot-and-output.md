# Phase 1: Boot and output

Date: 2026-10-28
Status: **complete.** Exit gate passed. Tag to apply: `v0.1-boot`.

Note: Phase 1 was missing when you asked for Phase 2, so I built it first. Phase 2 needs `kprintf` and `panic()`.

## What I did
- `kernel/include/limine.h`: fetched the official Limine protocol header from the upstream repo (0BSD licence).
- `linker.ld`: the Limine request block now lives in `.data` (kept with `KEEP`). The old `boot_marker` hack is gone.
- `kernel/main.c`: requests the framebuffer, the HHDM offset and the memory map from Limine. `kmain()` initialises serial, then the framebuffer, prints a banner, then halts in an `hlt` loop.
- `kernel/drivers/serial.c`: COM1 driver. `serial_init` (38400 baud, 8N1, FIFO on), `serial_putc` (waits for the transmit register), `serial_puts` (adds `\r` before `\n`).
- `kernel/drivers/fb.c`: framebuffer text console. Draws 8x16 glyphs with `\n \r \b \t`, scrolls when full, and draws a `_` cursor. Colours match the splash (warm white on deep purple). Supports 32-bpp modes only.
- `kernel/drivers/font8x16.c`: first 256 glyphs of Terminus (SIL OFL), generated from your system's `Lat2-Terminus16.psfu.gz`.
- `kernel/kprintf.c`: `kprintf`/`kvprintf` with `%s %d %u %x %p %c %%`, zero padding and width, and the `l`/`z` length modifiers. Output goes to serial and screen together.
- `kernel/panic.c`: `panic(fmt, ...)` disables interrupts, prints the message plus caller, rsp, rbp, rflags and CR0/2/3/4, then halts. It's a snapshot at panic time. Full register dumps come with the Phase 2 interrupt frame.
- `kernel/string.c`: `memset/memcpy/memmove/memcmp/strlen`, because the compiler emits calls to them.
- `Makefile`: added `-Ikernel/include`, automatic header dependencies (`-MMD`), and an `EXTRA=` variable for test flags.

## How I tested
- Booted headless in QEMU and read the serial log:

      LeOS booting...
      hello 42
      HHDM offset: 0xffff800000000000
      Memory map entries: 15
      Boot complete, halting.

- Took a screenshot through the QEMU monitor. The same text appears on screen with the cursor below it.
- `make iso EXTRA=-DPANIC_TEST`: the panic banner and register dump print and the machine halts, no reboot.
- `make iso EXTRA=-DSCROLL_TEST`: 70 lines scrolled correctly, with the newest at the bottom.
- Final build is the normal one, with no warnings.

## Exit gate
`kprintf("hello %d\n", 42)` appears on both the QEMU window and the terminal: **passed.**

## Notes
- Interrupts are still off, and there is no IDT yet, so any CPU fault will triple-fault. That is Phase 2.
- The splash shows for 3 seconds from Limine before the kernel text starts.
- Still not tried: UEFI boot, VirtualBox. Nothing is committed yet.
