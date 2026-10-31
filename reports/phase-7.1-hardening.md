# Hardening pass (after the network milestone)

Date: 2026-10-31
Status: **complete.** Closes the gaps that the earlier reports listed as known limits.

## What I did
**1. Floating point and SSE for user programs** (`arch/x86_64/cpu.c`, `kernel/sched/task.c`)
- `fpu_init()` enables SSE (CR0.EM off, CR0.MP on, CR4.OSFXSR and OSXMMEXCPT) and records a clean register state.
- Every user task has a 512-byte FXSAVE area. The scheduler saves the outgoing user task's registers and restores the incoming one's on every context switch. The kernel itself is still built without SSE, so it never touches those registers and interrupts or system calls cannot corrupt them.
- `fork` copies the parent's live registers to the child; `exec` resets them to the clean state.
- User programs are no longer built with `-mno-sse`. `printf` gained `%f` (with width and precision).

**2. Guard pages under kernel stacks** (`kernel/sched/kstack.c`)
- Task kernel stacks no longer come from the heap. They live in their own virtual region, 16 KiB each, with one unmapped guard page below every stack. Pages are mapped up front and returned when the task is reaped. (User stacks already had an unmapped page on both sides.)
- The exception handler recognises a hit on a guard page and says so.

**3. SMEP and SMAP** (`arch/x86_64/cpu.c`, `kernel/syscall.c`)
- Detected with CPUID and enabled in CR4 when the CPU has them. SMEP stops the kernel from executing user pages; SMAP stops it from reading or writing user memory except inside a `stac`/`clac` window.
- Every kernel access to user memory goes through `copy_from_user`, `copy_to_user` or `copy_str_from_user`, which now open the window only around the copy.
- QEMU's default CPU model lacks both features, so `make run` now uses `-cpu max`.

**4. The kernel image alias** (`kernel/mm/vmm.c`): the direct map no longer gives write access to the memory holding the kernel image and boot modules (the writable alias of the kernel's code is gone).

**5. The double-fault stack:** the handler now reports which stack it runs on, so the long-standing "not verified" note can be settled by a test.

## Results
Each protection was proven by a test that triggers the bug it guards against:

| Test | What it triggers | Result |
|---|---|---|
| `EXC_TEST=8` | a kernel thread recurses until its stack is exhausted | double fault, **"running on the dedicated double-fault (IST1) stack"**, and "cause: KERNEL STACK OVERFLOW: the guard page of kernel stack 1 was hit" |
| `EXC_TEST=5` | a page fault that cannot be delivered | double fault on the IST1 stack |
| `EXC_TEST=9` | write to kernel code through the direct map | page fault, protection violation, write |
| `SMAP_TEST` (window never opened) | the kernel's first read of user memory | page fault at `0x404214`, protection violation, kernel mode |
| `SMEP_TEST` | the kernel jumps to user code at `0x400000` | page fault, protection violation, **instruction fetch** |
| `fpdemo` | 4 processes, preempted constantly, exact sums on doubles and x87 long doubles, plus a register value inherited through fork | PASSED, 14 runs out of 14 |

Regressions: the 100 MiB memory test, the sync self-test (20 tasks created and reaped, free frames identical, so kernel stacks are freed correctly), divide, null-fault and kernel-code-write exceptions, `forktest`, `tree 4`, `umem`, all nine `crash` modes, pipes, `nano`, `greet` from the disk, and the network (ping, UDP, a 300 KB download and upload with matching CRC). After a warm-up, 12 rounds of wget, UDP, pipelines, crashes and `fpdemo` (60 forks) left free memory and kernel heap byte-for-byte identical.

## Notes and limits
- `printf("%.0f", 2.5)` prints `3` (round half up); the C library prints `2` (round half to even). Cosmetic.
- FPU state is saved eagerly for user tasks on every switch (simple and correct, a little slower than lazy saving). AVX and wider registers are not saved: user programs must stay with SSE2, which is what the compiler produces by default.
- SMEP and SMAP are only active when the CPU offers them. On a CPU or QEMU model without them the kernel says "not supported" and runs unprotected.
- Kernel stacks are 16 KiB, so deep recursion in the kernel still faults. The guard page now turns that into a clear message instead of corruption.
- `printf` has `%f` only. `%g` and `%e` are not implemented.
- Still unverified: real hardware and VirtualBox.

## Test switches
`make iso EXTRA=-DEXC_TEST=8` (stack overflow), `-DEXC_TEST=9` (kernel code alias), `-DSMAP_TEST`, `-DSMEP_TEST`; then boot and run `hello`. Run `fpdemo` in the shell.
