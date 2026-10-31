# Porting Lua 5.4.7

Date: 2026-10-31
Status: **complete.** Lua runs unmodified on LeOS and passes the official test suite.

## What porting meant
Lua is portable C written for every operating system, so it expects a *standard C library* and normal system behaviour. Making it run on LeOS meant writing the C library LeOS did not have, and fixing the OS wherever Lua (and its tests) found a gap. The Lua source in `third_party/lua/src` is **unmodified** (downloaded from lua.org; its SHA-256 `9fbf5e28...` matches the value published there). Only `linit.c` is replaced by `user/lua/linit.c`, which registers the same libraries plus `leos`.

## What I built
**A real C library** (`user/lib/`, headers in `user/include/`): `stdio.h` (buffered `FILE` streams: `fopen` in all modes, `fread/fwrite/fgets/ungetc/fseek/ftell/setvbuf/tmpfile/freopen/remove/rename`), the whole `printf` family, `string.h` and `ctype.h`, `stdlib.h` (`qsort`, `bsearch`, `strtol` family, `getenv`/`setenv`, `system`, `atexit`, `exit` that flushes streams), `setjmp`/`longjmp` (assembly), `signal`/`locale` stubs, `time.h` (`time`, `gmtime`, `mktime`, `strftime` with every C99 conversion), `math.h`, and `errno`.
- **`printf` for floating point is exact.** `%f %e %g %a` convert the double to its exact decimal expansion with big-integer arithmetic and round the way glibc does (ties to even). Lua prints numbers with `%.14g`, so this matters.
- **`strtod`** parses decimal, hexadecimal-float, `inf` and `nan` through 80-bit arithmetic: exact for ordinary inputs, within one unit in the last place for extreme ones.
- **Math** (`sin cos tan asin acos atan atan2 exp log log2 log10 pow fmod sqrt sinh cosh tanh ...`) uses the x87 FPU's own instructions in 80-bit precision, with the special cases (NaN, infinity, domain errors) in C.
- **`malloc`** was rewritten: neighbouring free blocks merge, `realloc` grows in place when it can, and it uses `sbrk`.
- `printf` now buffers. stdout is line-buffered on the console and flushed before reading input and before `fork`, so prompts and output order still behave.

**Kernel and shell changes that porting forced** (each found by Lua or its tests, not guessed):
- **A wall clock**: new `time()` system call backed by the CMOS real-time clock (`os.time`, `os.date`).
- **User stacks grow on demand** up to 8 MiB (they were fixed at 128 KiB). Lua's recursion tests crashed the process; system calls handed a pointer into the not-yet-grown stack also grow it. Runaway recursion is still killed at the limit.
- **An empty path name is now an error** (ENOENT). It used to mean "the current directory", which made a Lua test believe a file named `""` existed.
- **64 open files per process** (was 16).
- `fopen` reports the right `errno` (no such file, is a directory, too many open files).
- A default environment (`PATH`, `HOME`, `TERM`, `USER`, `SHELL`) and a process-local `setenv`.
- **`sh -c "command"`** with a proper exit status, so `os.execute` works, and the shell accepts `a; b`.

**The `leos` library for Lua** (`user/lua/leoslib.c`): `leos.resolve`, `leos.tcp`, `leos.listen`, `leos.udp` (sockets with `send/recv/recvall/sendto/recvfrom/accept/shutdown/close`), `leos.ping`, `leos.dir`, `leos.stat`, `leos.getcwd/chdir/mkdir`, `leos.sleep`, `leos.uptime`, `leos.netinfo`, `leos.meminfo`, `leos.ps`. Demo scripts in `/home/lua/` on the ramdisk: `tour primes files life calc http server sysinfo dns udp`.

## Results
1. **C library differential test** (`tools/libc_difftest.sh`): one program, `user/progs/libctest.c`, compiled for the host (glibc) and for LeOS. It prints 1,878 lines: about 1,000 `printf` conversions (integers, and doubles in 25 formats), `strtod`/`strtol` parsing, 16 math functions, string functions, `qsort`, file I/O with seek/append/update, `setjmp`, `malloc`/`realloc` stress, and 11 dates through `gmtime`/`mktime`/`strftime`. **The output is byte-for-byte identical to glibc's** (CRC-32 `80029a44`, 65,270 bytes). The first run differed in 52 lines; the real bugs it caught were fixed (`%#x` of zero, the `DBL_MAX` overflow threshold, `ERANGE` for subnormals, NaN sign, `%Z`).
2. **Lua's official test suite** (`tools/lua_tests.sh`; downloaded, not stored in the repo): **26 files, all pass**: `strings literals tpack attrib locals constructs code cstack nextvar pm utf8 events vararg closure coroutine goto errors math sort bitwise calls db files gc gengc api`. They cover the whole language and library, including number parsing and printing (`math.lua`), pattern matching, coroutines, the debug library and file I/O. Running them found the real OS bugs listed above.
3. Demos: `tour.lua`; a 200,000 prime sieve in 0.15 s; file I/O; `os.execute` with exit codes; the calculator; `lua` with no arguments gives an interactive prompt. With the network: DNS lookups, an HTTP client in 15 lines of Lua fetching the real `http://example.com/` (HTTP 200, 577 bytes), and a UDP echo.
4. Regressions: memory stress, sync, kernel exception tests, stack-overflow and SMEP/SMAP behaviour, `forktest`, `tree`, `umem`, `fpdemo`, all crash modes, pipes, nano and the network. 12 rounds of Lua and process churn showed no leak (a 65 KB test file left in `/tmp` accounts for the only heap growth).

## Notes and limits
- **Not run from the official suite:** `main.lua` (needs `io.popen` and spawning the interpreter), `big/heavy/verybig` (memory-hungry), `ltests` and the `T` library (needs Lua built with its internal test hooks). `api.lua` therefore skips its C-API tests. `gc.lua` is order-sensitive by design: it must run first on a clean state, as in the official runner (run late, it failed one memory-count assertion).
- No `io.popen`, no loading of C libraries, no `scanf`, no wide characters, no real locales or time zones (everything is UTC), and no signals: Ctrl-C cannot interrupt a running script. `os.rename` copies then deletes.
- `strtod` is not guaranteed correctly rounded for extreme inputs, and the x87 math functions are accurate to about 1e-15 but not guaranteed to the last bit. In every comparison made (12 significant digits for math, 17 for parsing) they matched glibc.
- Programs get default environment variables, not a real environment passed through `exec`.
- Sockets from Lua: `recv` takes an optional timeout; there is no non-blocking mode or `select`, so one script serves one connection at a time.
- Not verified: real hardware.

## Try it
    make run
    lua /home/lua/tour.lua
    lua                                   (interactive; Ctrl-D quits)
    ifconfig dns 1.1.1.1
    lua /home/lua/http.lua http://example.com/
    lua /home/lua/sysinfo.lua
    tools/lua_tests.sh                    (the official test suite, about 5 minutes)
    tools/libc_difftest.sh                (the C library against glibc)
