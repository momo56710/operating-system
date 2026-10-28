# Phase 4: Time, input and multitasking

Date: 2026-10-28
Status: **complete.** Exit gate passed (see Results). Tag to apply: `v0.4-multitasking`.

## What I did
**APIC and time** (`kernel/drivers/acpi.c`, `apic.c`)
- ACPI: finds the RSDP from Limine, walks the XSDT/RSDT, and checks table checksums. It parses the MADT for the Local APIC address, I/O APICs and the interrupt source overrides (QEMU reports 5).
- Local APIC: mapped as uncached MMIO (`vmm_map` with `VMM_PCD`, since device memory is not in the HHDM), software-enabled with spurious vector 0xFF. This replaces the Phase 2 stopgap that switched the APIC off.
- I/O APIC: all pins masked, then `irq_enable(n)` routes an ISA IRQ to vector `0x20 + n`, honouring overrides (polarity and trigger). The keyboard uses it. The legacy PIC stays remapped and fully masked.
- Timer: the LAPIC timer is calibrated against PIT channel 2 over a 50 ms window (62,5xx ticks/ms on QEMU), then runs periodic at 1000 Hz on vector 0x40. `uptime_ms()` counts ticks and `sleep_ms()` sleeps on them. HPET is not used.

**Tasks and scheduler** (`kernel/sched/task.c`, `arch/x86_64/switch.asm`)
- `struct task` holds saved stack pointer, pid, state (running, ready, sleeping, blocked, zombie), wake time, time slice, stack and name.
- `switch_to` saves rbx, rbp, r12-r15, swaps stack pointers and returns into the other task. New tasks start in `task_trampoline`, which enables interrupts, calls the entry function and then `task_exit`.
- Preemptive round-robin: the timer interrupt sends the EOI first and then calls `sched_tick`. That wakes sleepers and switches tasks when the 10 ms slice is up. Each task has a 16 KiB kernel stack, fully touched at creation so a stack never page-faults on demand.
- The boot context becomes task 0 (idle). It runs only when nothing else is ready, and reaps finished tasks (frees their stacks) from its loop.
- API: `task_create`, `task_exit`, `yield`, `sleep_ms`, `uptime_ms`.

**Locks** (`spinlock.h`, `kernel/sched/sync.c`)
- Spinlocks that disable interrupts while held (`spin_lock_irqsave`).
- Semaphore: a FIFO wait queue where `sem_post` hands the token straight to the oldest waiter. It is safe to call from interrupt handlers. Mutex: a semaphore with owner checks.
- Because threads can now interrupt each other, I added locks to the PMM, VMM, heap and the print path. `kprintf` takes a lock per message so output from different threads doesn't interleave in the middle of a line. Exception and panic paths force-release it first, so a fault inside a print can't deadlock.

**Keyboard** (`kernel/drivers/keyboard.c`)
- The IRQ decodes the scancode and pushes the character into a 128-entry ring buffer, then posts a semaphore. `kbd_getc()` blocks until a key arrives.

**Unplanned additions**
- `kernel/shell.c`: a built-in kernel shell (`help echo clear uptime sleep meminfo ps reboot`), because typing into a console with nothing to read it was confusing. It is replaced in Phase 6 by a real userspace shell.
- `%-Ns` left alignment in `kprintf`, `kclear()`, `strcmp`, and a faster `memcpy`/`memmove` (`rep movsb`).

## Results
- **Preemption:** with `make iso EXTRA=-DTHREAD_DEMO`, three threads (A/B/C, printing at 300/500/700 ms) plus a CPU-bound thread that never yields run together. The busy thread still prints `*` once per second and the others keep their rates, so the timer is preempting it.
- **Exit gate, 10-minute run:** the demo ran in QEMU with a monitor thread reporting every 30 s. At 510 s of uptime: A=1700, B=1020, C=729, matching their periods exactly (A: 100 per 30 s). **No exceptions or panics.** Free frames stayed at 130441 and heap use at 98,976 B in every report, so no leak. The final figures are in the addendum below.
- **Typed keys reach a kernel buffer:** keys go through the ring buffer and are consumed by the shell thread. Verified with simulated keystrokes while the demo threads were running.
- **Sync self-test** (`make iso EXTRA=-DSYNC_TEST`), all pass:
  - shared counter without a lock: 500 of 2000 (the race is real)
  - with a mutex: 2000 of 2000
  - producer/consumer: 1000 items, checksum 500500 correct
  - 20 tasks created and exited: heap use 32,992 B before and after, free frames identical
  - `sleep_ms(100)` measured 100 ticks
- **Shell:** `help`, `echo`, `clear`, `uptime`, `sleep 700`, `meminfo`, `ps` and unknown-command handling all verified by typing through QEMU.
- **Regressions:** the 100 MiB heap test, and the exception tests (divide error, #GP, null #PF, double fault, write to kernel code) all still pass.

## Notes and limits
- `uptime_ms()` counts timer interrupts, so it falls behind real time when the host is heavily loaded (QEMU delays or drops ticks). During the long run it read about 16 s behind the wall clock while I was running other QEMU instances. A TSC-based clock would fix this later.
- Single CPU only. Spinlocks are correct for SMP, but only the boot CPU runs, and scheduling is one run queue.
- No guard pages under task stacks, so a stack overflow corrupts the neighbouring heap block instead of faulting. A guard page is cheap to add once per-task mappings exist.
- Not verified: that the double fault actually runs on its IST stack (carried over from Phase 2), and any of this on real hardware or VirtualBox.
- `reboot` pulses the keyboard-controller reset line. Under the `-no-reboot` flag QEMU exits instead of restarting.

## Test switches
`make iso EXTRA=-DTHREAD_DEMO` (the A/B/C/busy/monitor demo), `-DSYNC_TEST`, `-DMM_TEST`, `-DEXC_TEST=<1-7>`, `-DPANIC_TEST`, `-DSCROLL_TEST`.

## Addendum: final 10-minute result
The demo ran for the full 10 minutes (kernel uptime 600 s, about 628 s of wall clock including boot). QEMU was still running at the end.
- Last monitor line: `up 600s  A=2000 B=1200 C=858  spin=28072273300  free frames=130441  heap used=98976 B`
- Exceptions or panics in the whole log: **0**
- Free frames across all 20 monitor reports: a single value, 130441. Heap used: a single value, 98,976 B. **No leak.**
- Thread counts matched their periods to the unit at every report (A: 100 per 30 s).
- Exit gate "three kernel threads run concurrently for 10 minutes without a crash": **passed**, as measured under QEMU TCG.
