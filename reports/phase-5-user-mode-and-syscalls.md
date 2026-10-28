# Phase 5: User mode and system calls

Date: 2026-10-28
Status: **complete.** Exit gate passed. Tag to apply: `v0.5-usermode`.

## What I did
**Per-process address spaces** (`kernel/mm/vmm.c`)
- Every process has its own PML4. The upper half (kernel, direct map, heap) is copied from the kernel's PML4 and shared; the lower half is private.
- Since a copy only sees what existed at copy time, `vmm_init` now creates a table for all 256 kernel-half slots up front (about 1 MiB), so anything the kernel maps later appears in every address space.
- New functions: `vmm_new_space`, `vmm_free_space` (frees every user page and table), `vmm_clone_space` (eager page copy for fork), `vmm_map_in`/`vmm_unmap_in`/`vmm_lookup` (work on any PML4, `vmm_lookup` also returns the permission flags).
- The scheduler loads the next task's page tables on every switch (only if they differ) and sets the TSS `rsp0` and the syscall kernel stack to that task's kernel stack.
- The page fault handler never lazily maps for user-mode accesses.

**Entering ring 3** (`arch/x86_64/switch.asm`)
- `enter_user` builds an `iretq` frame (user SS/RSP/RFLAGS/CS/RIP, selectors 0x1B/0x23) and zeroes every register so no kernel values leak. `resume_user` restores a saved frame, which is how a forked child returns to user mode.

**System calls** (`arch/x86_64/syscall.asm`, `syscall.c`, `kernel/syscall.c`)
- `syscall_init` sets EFER.SCE, STAR (kernel CS 0x08, SYSRET base 0x10), LSTAR and SFMASK (clears IF, DF, TF, AC on entry).
- The entry stub switches to the task's kernel stack and builds the same frame an interrupt does, then runs a C dispatcher, so one `iretq` return path serves every way into the kernel. Interrupts are re-enabled inside the call, so blocking calls can be preempted.
- Calls: `write`, `exit`, `getpid`, `yield`, `sleep`, `brk`, `mmap`, `munmap`, `fork`, `exec`, `wait`, `uptime`. Numbers are shared between `kernel/include/syscall.h` and `user/include/sys.h`.
- **Every user pointer is validated** before use: in the user half, every page present, user-accessible, and writable where the kernel writes. Strings are length-limited. A bad pointer returns -1; it never faults the kernel.

**ELF loader** (`kernel/elf.c`)
- Checks the header (64-bit, little-endian, `ET_EXEC`, x86-64), program-header bounds, `filesz <= memsz`, file bounds, no wrap-around, and that segments stay in the user half above page 0. Maps each `PT_LOAD` with the right permissions (W^X: `PF_W` gives writable, no `PF_X` gives NX), zero-fills BSS, and merges permissions when two segments share a page.

**Processes** (`kernel/process.c`)
- `spawn` (used by the shell), `fork` (clone the address space and copy the interrupt frame, child gets rax=0), `exec` (builds the new image first, so a failure leaves the old one intact), `wait`, `exit`.
- Stack layout follows System V: `rsp -> argc, argv[], NULL, envp NULL`, 128 KiB stack with an unmapped page above and below. `brk` heap up to 3.75 GiB, `mmap` with a guard page after every mapping.
- Exit keeps the status in a "defunct" task until the parent waits. Orphans are cleaned up when their parent exits.
- A CPU exception from ring 3 prints a message and kills only that process (exit status 1000 + vector). Kernel-mode faults still halt as before.

**Programs and libc** (`user/`)
- Built as separate static ELF files with `-ffreestanding -mno-sse`, linked at 0x400000 by `user/user.ld`.
- Tiny libc: syscall wrappers, `printf`, `puts`, string functions, `atoi`, and `malloc`/`free` (first-fit over `sbrk`, no coalescing).
- Programs: `hello`, `args`, `crash` (9 modes), `forktest`, `umem`.
- There is no filesystem yet, so the programs are Limine boot modules (`kernel/modules.c`). The Makefile builds them, copies them into the ISO and writes the `module_path:` lines into the boot config automatically. Phase 6 replaces this with files on disk.

**Shell**: `progs` lists the programs, `run <program> [args]` starts one, waits, and prints how it ended.

## Results
All run by typing into the shell under QEMU and reading the serial log.
- `run hello`: printed from ring 3 through `write`, and exit code 42 reached the parent.
- `run args one two three`: argc = 4 with the correct argv strings.
- **Misbehaving programs** (`run crash N`), each killed with the right exception while the kernel kept running:

  | Mode | What it does | Result |
  |---|---|---|
  | 1 | write to address 0 | page fault at 0 |
  | 2 | `hlt` | general protection fault |
  | 3 | read kernel memory | page fault at ffffffff80000000 |
  | 4 | divide by zero | divide error |
  | 5 | endless recursion | page fault just below the 128 KiB stack |
  | 6 | `cli` | general protection fault |
  | 7 | write to own read-only code | page fault (protection) |
  | 8 | jump to unmapped memory | page fault with rip = target |
  | 9 | give `write` a kernel pointer and a bad low pointer | both return -1, kernel unaffected |

  `hello` ran correctly after all of them.
- `forktest`: the child's copy of memory is independent (101 vs 100), `wait` returns the child's pid and status, `fork`+`exec` runs `hello` and returns 42, `exec` of a missing program returns -1 and leaves the caller intact, 8 simultaneous children are all reaped with the right codes, and `wait` with no children returns -1.
- `umem`: malloc/free stress with content checks, an 8 MiB `sbrk` touched page by page, `mmap`/`munmap` of 1 MiB, and `sleep_ms(200)` measured 200 ms.
- **No leak:** free frames and heap bytes were identical before and after `forktest` and `umem`, and again before and after 40 consecutive process lifetimes (20 normal, 20 crashed): 130139 frames and 16,560 B both times. `ps` showed only `idle` and `shell` afterwards.
- **Regressions:** the 100 MiB memory test, the sync self-test (counter race, mutex, producer/consumer, task reaping, sleep timing) and the kernel exception tests (divide, null page fault, double fault, write to kernel code) all still pass.

## Exit gate
A user program written in C, compiled separately, loads as an ELF, prints text through `write`, and a crashing user program is killed without taking down the kernel: **passed.**

## Notes and limits
- **No FPU/SSE state is saved** on context switches, so user programs are built with `-mno-sse` and cannot use floating point. A program built with SSE would silently corrupt other tasks' registers. Saving that state is a future task.
- **fork copies pages eagerly.** Copy-on-write is the usual optimisation (the roadmap calls it "optional but rewarding").
- SMEP and SMAP are not enabled, so the kernel could in principle run or touch user memory by mistake.
- The syscall stub keeps the user stack pointer in one global while it switches stacks. That is correct for one CPU and must become per-CPU data (swapgs) before multicore.
- The user stack is a fixed 128 KiB and does not grow. Task kernel stacks (16 KiB) still have no guard pages.
- Returning to user mode always uses `iretq` and not `sysret`. It is slower but simpler and avoids sysret's non-canonical address pitfalls.
- There is no way yet for a user program to read the keyboard or open files. That is Phase 6 (device files and file descriptors).
- The user `malloc` never merges free blocks.
- Not verified: real hardware or VirtualBox, and whether the double fault really runs on its IST stack.

## Test commands
`make programs` builds only the user programs. In the shell: `progs`, `run hello`, `run args a b c`, `run crash <1-9>`, `run forktest`, `run umem`.

## Addendum: demo programs (same day)
Eight more programs in `user/progs/`, all built into the ISO. Since version 5 the shell also accepts a bare program name (`primes 1000000`), not only `run primes 1000000`.

| Program | What it shows | Verified output |
|---|---|---|
| `count [n] [ms]` | sleeping, arguments, uptime | ticks 400 ms apart |
| `primes [limit]` | `malloc` of a large array, 64-bit maths | 9592 primes up to 100000, 78498 up to 1000000 (both correct) |
| `fib [n]` | recursion, 64-bit overflow limits | fib(27) = 196418, fib(90) = 2880067194370816120 |
| `sort [n]` | quicksort on 50k-200k numbers, self-checking | `ORDER OK`, 12 ms for 50k |
| `tree [depth]` | recursive fork, many processes, wait/exit codes | depth 3: 15 processes, depth 4: 31, counts match |
| `life [gens]` | Game of Life, a glider moving | 5 cells alive every generation |
| `mandel` | Mandelbrot in ASCII with 16.16 fixed-point maths (no floats) | the expected shape |
| `bench` | CPU loop, syscall rate, fork+wait, malloc, brk | 50M iterations 147 ms, 100k getpid 37 ms, 200 fork+exit+wait 293 ms |

Memory stayed flat through repeated `tree 3` and `tree 4` runs (free frames identical every time). The kernel heap's address range grows to match the largest burst of tasks (768 KiB after `tree 4`) but its pages are returned.

Timings use the kernel's own tick counter under QEMU's emulation, so they say how the pieces compare to each other, not how fast real hardware would be.
