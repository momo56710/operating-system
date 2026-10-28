# Phase 0: Setup

Date: 2026-10-28
Status: **complete.** Exit gate passed.

## What I did
- Created the repo layout from the roadmap: `kernel/{drivers,mm,sched,fs}`, `arch/x86_64`, `user`, `reports`, and ran `git init`.
- `linker.ld`: places the kernel in the higher half at `0xffffffff80000000`, with separate text (r-x), rodata (r--) and data (rw-) segments. This is what Limine expects.
- `limine.conf`: Limine protocol, boots `/boot/kernel.elf`, serial output on.
- `kernel/main.c`: a stub `kmain()` that writes a line to COM1 (port 0x3F8) and halts in an `hlt` loop. It is only here to prove the bootloader hands off to our code. Phase 1 replaces it with the real serial driver and `kprintf`.
- `Makefile` with the targets `kernel.elf`, `iso`, `run`, `debug`, `trace` and `clean`. It uses `x86_64-elf-gcc` if it exists and falls back to host `gcc` and `ld` otherwise.
- `.gitignore` for build output.

## How
- Compile flags follow the roadmap: `-ffreestanding -mno-red-zone -mcmodel=kernel`, no PIE, and no SSE, because the kernel must not touch FPU or SIMD state yet.
- Checked the output with `readelf -lW kernel.elf`. My first build had an empty data segment placed at address 0, which Limine would reject. I added a tiny `boot_marker` variable so the rw- segment is real. It is now at `0xffffffff80002000`.
- `nm` confirms `kmain` is at the start of the higher half.

## Verified
- `make kernel.elf` builds with no warnings.
- The ELF has three valid LOAD segments in the higher half.

## Follow-up (same day)
- Installed `nasm`, `qemu-system-x86`, `limine` and `libisoburn`. The first attempt failed on a stale mirror (404 on a `.sig` file) and was fixed by refreshing the database.
- Renamed the project from `myos` to **LeOS** in `limine.conf` and the boot message.
- `make iso` builds `os.iso` (BIOS and UEFI bootable). The Arch Limine paths in the Makefile were correct.
- Booted the ISO headless in QEMU with serial sent to a file. Output:

      limine: Loading executable `boot():/boot/kernel.elf`...
      LeOS: kernel reached, halting

- **Exit gate passed:** the bootloader hands off to our code, which prints over serial and halts.

## Notes
- Still using host `gcc` as a stand-in for the cross compiler (`x86_64-elf-gcc` is AUR-only). Fine for now.
- Not yet tested: UEFI boot and VirtualBox (planned at the end of each phase).
- Nothing committed yet. Next commit and tag would be `v0.0-setup`.
