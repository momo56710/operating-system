# Phase 2: CPU setup and interrupts

Date: 2026-10-28
Status: **complete.** Exit gate passed. Tag to apply: `v0.2-interrupts`.

## What I did
- `arch/x86_64/gdt.c` + `gdt_flush.asm`: our own GDT with null, kernel code (0x08), kernel data (0x10), user data (0x18), user code (0x20) and a 16-byte TSS descriptor (0x28). User data comes before user code because SYSRET (Phase 5) expects that order. `gdt_flush` loads it, reloads DS/ES/SS/FS/GS, and reloads CS with a far return. The TSS has a 16 KiB IST1 stack for double faults and a `tss_set_rsp0()` hook for Phase 5.
- `arch/x86_64/isr.asm`: 256 interrupt stubs generated with NASM macros. Vectors where the CPU pushes an error code (8, 10-14, 17, 21, 29, 30) keep it, and the rest push a dummy 0, so the stack always matches `struct interrupt_frame`. A common routine saves all 15 general registers, calls C, restores them and returns with `iretq`. The stack stays 16-byte aligned at the call.
- `arch/x86_64/idt.c`: the 256-entry IDT, interrupt gates, vector 8 (double fault) on IST1.
- `kernel/interrupts.c`: `interrupt_handler()`. Exceptions (0-31) print the name, vector, error code and a full register dump (all GPRs, rip, cs, rflags, rsp, ss, cr2, cr3), then halt. Page faults also print `PAGE FAULT at 0x...` and decode the error bits (present/write/user/instruction fetch). IRQs (0x20-0x2F) go to a handler table and then get an EOI.
- `kernel/drivers/pic.c`: remaps the 8259 PIC to vectors 0x20-0x2F, masks everything, with mask/unmask/EOI and spurious IRQ7/15 detection.
- **Bonus, not in the roadmap:** `kernel/drivers/keyboard.c`, a PS/2 keyboard on IRQ1. It configures the 8042 controller, reads scancode set 1, handles shift, caps lock and backspace, and echoes to screen and serial. This lets you type into LeOS.
- `Makefile`: now also builds `arch/**/*.c` and assembles `arch/**/*.asm` with NASM.
- `main.c`: after the Phase 1 output it runs `gdt_init`, `idt_init`, `pic_init`, `keyboard_init`, then `sti`.

## Deviation from the roadmap
The roadmap puts the Local APIC, I/O APIC and ACPI MADT parsing in this phase. I moved them to Phase 4, where the APIC timer is actually needed. Until then the legacy PIC handles interrupts.

## Problem found and fixed
- The keyboard interrupt never fired, and neither did the timer IRQ0. A probe showed `IA32_APIC_BASE = 0xfee00900`: **Limine leaves the Local APIC enabled**, and while it is enabled, the legacy PIC's output is blocked.
- Reading the APIC registers also page-faulted. `0xfee00000` is not in the HHDM, and mapping it is Phase 3 (VMM) work.
- Fix: `pic_init()` globally disables the Local APIC through the MSR (clear bit 11), so interrupts go PIC to CPU. It's a stopgap, replaced in Phase 4 when the APIC is mapped and enabled properly.
- Along the way I also made `keyboard_init()` set up the 8042 controller itself (enable port 1, IRQ1, scanning) instead of trusting the bootloader's state.

## How I tested
Each test builds with `make iso EXTRA=-DEXC_TEST=n`, boots in QEMU, and reads the serial log:

| n | Trigger | Result |
|---|---|---|
| 1 | divide by zero | `EXCEPTION 0: Divide error` + register dump |
| 2 | `ud2` | `EXCEPTION 6: Invalid opcode` |
| 3 | load invalid selector 0x38 | `EXCEPTION 13`, error code 0x38 |
| 4 | read address 0 | `EXCEPTION 14`, `PAGE FAULT at 0x0 (page not present, read, kernel)` |
| 5 | page fault with #PF gate disabled | `EXCEPTION 8: Double fault` |

All five halt with a register dump and **no reboot**. Tests 4 and 5 were re-run after the APIC fix.

Keyboard: sent keys through the QEMU monitor and took a screenshot. It showed `> LeOS hello 20267`, with the backspace removing the `1`. Enter made a new line, caps lock gave `ABC`, and shift gave `!` and `?`.

## Exit gate
A null pointer dereference prints `PAGE FAULT at 0x0` with a register dump instead of rebooting: **passed.**

## Not verified
- That the double fault actually ran on the IST1 stack. The frame shows the interrupted rsp, so it can't tell. A stack-overflow test would prove it.
- Typing was tested with simulated keys through QEMU, not a physical keyboard. Try it in the QEMU window.
- No PIT or timer interrupt yet. That is Phase 4.
- Not tested: UEFI, VirtualBox. Nothing is committed or tagged yet.

## Test switches
`make iso EXTRA=-DEXC_TEST=<1-5>`, `-DPANIC_TEST`, `-DSCROLL_TEST`. A normal `make iso` builds without any of them.
