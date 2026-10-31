# Phase 10: the graphical desktop

LeOS now has a desktop that looks like https://momodev.cc: black background, white monospace text, thin rounded
borders, underlined links, the photo, the pixel cat. Start it from the shell with `desktop`.

## What you get

| Piece | What it is |
|---|---|
| Top bar | `momo@leos:~$`, the nav links (about, skills, experience, projects, contact), clock, `off` |
| Icons | about, skills, experience, projects, contact, terminal, files, notes, settings, monitor |
| Hero panel | your name and title, typed out like the site's typewriter |
| Bottom bar | the animated pixel cat (click it), one tab per open window, gh / in / ig buttons |
| Windows | rounded, draggable by the title bar, raised on click, scrollable (wheel or arrow keys), closed with `x` |
| about / skills / experience / projects / contact | the content of the portfolio, same wording |
| terminal | `help ls cd cat pwd echo uname uptime date mem ps ip neofetch`, anything else runs as Lua |
| files | browse every folder, click a file to open it in notes |
| notes | a text editor (Ctrl+S saves to `/disk/notes.txt`, or `/tmp/notes.txt` without a disk) |
| settings | accent colour, wallpaper (plain/dots/grid), cat on/off, 12/24 h clock, time zone, system info |
| monitor | uptime, memory and heap bars, the process table |

Keys: `Ctrl+Alt+Q` leaves the desktop, `Alt+Tab` switches window, `Alt+Esc` closes the focused window.
Settings are saved to `/disk/desktop.cfg` when a disk is attached (without one they last until shutdown).

Links (resume PDFs, GitHub, projects...) show their address in a message box: LeOS has no web browser yet.

## How it works

The desktop is a Lua program (`initrd/home/desktop/desktop.lua`) drawing through a new C library, `gfx`.
The kernel got four new foundations:

1. **Mouse driver** (`kernel/drivers/mouse.c`): PS/2 mouse on IRQ 12, 3-byte packets, plus the scroll wheel when the
   mouse supports the "IntelliMouse" handshake.
2. **Raw input events** (`kernel/drivers/input.c`): while a program owns the screen, the keyboard and mouse produce
   events (`key down/up with scancode, character and modifiers`, `mouse position, buttons, wheel`) instead of feeding
   the text console. Pure mouse movement is coalesced into one event so a fast mouse cannot flood the queue.
3. **Direct framebuffer access**: new system call `SYS_GFX` (`GFX_INFO`, `GFX_MAP`, `GFX_ACQUIRE`, `GFX_RELEASE`,
   `GFX_POLL`). `GFX_MAP` maps the screen's memory into the program (write-combined). Those pages carry a new
   `VMM_DEVICE` bit so the kernel never frees them with the process and `fork` shares them instead of copying.
4. **Screen ownership**: `GFX_ACQUIRE` silences the kernel's text console; `process_exit` hands everything back, so a
   crashed desktop cannot leave you with a dead screen (the console comes back cleared).

The `gfx` library (`user/lua/gfxlib.c`) draws into an ordinary back buffer, tracks the changed rectangle, and
`present()` copies only that to the screen. The mouse pointer is drawn on the screen itself, so moving the mouse never
redraws the desktop. It has rectangles, rounded frames and fills, lines, text (the kernel's Terminus 8x16 font,
scalable), alpha-blended images, clipping and the event queue. Images are converted on the host by
`tools/mkimage.py` (photo 200x200, cat 4 frames); icons are drawn from shapes in code.

## Verification

All in QEMU, driven by `tools/leos_drive.py` (new `<MON:...>` line type injects mouse moves and clicks):

* `gfxtest` (C): painted the screen, position after injected moves was exactly the expected 820,610, click and keys
  counted, console restored on exit.
* Every app opened by clicking its icon, screenshots checked; window drag, Settings accent colour, `off` button, typing
  into terminal and notes.
* Settings persisted across two launches on a disk copy (`cat /disk/desktop.cfg`).
* Regression: forktest, primes, Lua tour still pass; free memory identical before and after a GUI session.

## Running it

* QEMU: `make run`, then type `desktop`. The mouse works once the QEMU window has grabbed it (click inside it).
* VirtualBox (no Guest Additions): the PS/2 mouse is only captured after you click in the VM window; release with the
  host key (right Ctrl). Without Additions the pointer is "relative", which is exactly what this driver expects.

## Known limits

* No window resizing, no minimise button (use the taskbar tab to raise, `x` to close).
* Text is the fixed 8x16 bitmap font (and scaled copies), not the site's proportional font.
* Without a mouse everything is reachable only through the keyboard shortcuts above.
* Links cannot be opened until there is a browser.


---

# Phase 10b: Lua on the desktop

Scripts now run from the desktop, there are 45 of them, and the desktop got a round of polish.

## Running scripts

* **Every script is an icon.** `/home/lua/*.lua`, `/disk/lua/*.lua` appear as icons on the desktop (title and
  description come from `--@ title:` / `--@ about:` lines at the top of the file; `--@ args:` makes the window ask for
  arguments first). "refresh" rescans; icons page when there are many (mouse wheel too).
* **They run as real processes.** `leos.spawn()` starts a program with its stdin/stdout on pipes; the desktop reads the
  output without ever blocking and shows it in a window with a small terminal emulator (`initrd/home/desktop/emu.lua`:
  `\r`, backspace, tabs, clear screen, cursor moves). Input typed in the window goes to the program. Stop / Ctrl+C
  kills it, so an endless script can no longer hang the desktop.
* **The terminal is real now.** It runs `sh -c`, so `ls /bin | wc`, `primes.lua 2000`, `ps | grep lua`, redirects and
  every program in `/bin` work, plus `cd`, `=2^10` (Lua), Tab completion and history.
* **notes writes and runs Lua**: New, name it, type, Ctrl+R. Saving into a script folder makes a new icon.
* **Window apps**: `/home/desktop/apps/*.lua` draw inside desktop windows: calculator, snake, paint, clock with
  stopwatch, minesweeper. Writing your own is documented in `initrd/home/desktop/README.txt`.
* **In the shell**: `primes.lua`, `life.lua 20` now work without typing `lua`. (This is probably why scripts "did not
  work" before: `udp.lua: command not found`.)
* `desktop open=snake` starts the desktop with apps or scripts already open.

## The 45 scripts

Original: calc, dns, files, http, life, primes, server, sysinfo, tour, udp.
New: hello, fizzbuzz, fib, factors, collatz, hanoi, queens, mandel, pi, sort, guess, todo, wc, codec (rot13, base64,
hex, crc32), roman, bf, oop, coro, strings, matrix, dice, tree, du, netscan, clock, maze, json, memtest, ps, diskinfo,
units, hexdump, stats, uptime, spiral. All were run in QEMU and checked (pi digits, factorisations, N-Queens counts...).

## New library functions

`leos.spawn` (+ process methods `read write closeinput kill pid close`), `leos.reap`, `leos.diskinfo`, `leos.getpid`,
`leos.remove`, `leos.exists`.

## Kernel and libc changes

* `kill(pid)` system call (parent kills its child; a flag checked on return to user mode and on the timer tick).
* `wait(&status, WNOHANG)`, `ioctl(fd, PIPE_AVAIL)` (how many bytes a pipe holds, or end of file): the pieces that make
  non-blocking pipe reads possible.
* stdout is line-buffered when it is a pipe, so output appears live instead of at exit.
* `sh` runs `name.lua` through Lua.

## Desktop polish

Windows can be minimised (taskbar tab brings them back), maximised and resized with the corner grip; the taskbar
tabs toggle; apps pause while not in front; the monitor's bar layout fixed; notes has click-to-place cursor, auto
indent, comment colouring, Save as; files has a "run" button next to `.lua` files; settings persist to the disk.

## Verification

All through QEMU with injected mouse and keyboard: script icon runs and streams output; interactive `guess.lua`
answers typed in the window; terminal pipes and `primes.lua`; Ctrl+C on the running clock script ends it ("stopped") and
`ps` afterwards shows no leftover process; notes: New, write, Ctrl+R; minesweeper clicks; paint drag; snake keys;
maximise, resize, minimise; regression: forktest, pipes, Lua tour, libctest.

Known limits: full-screen terminal programs (nano) need a real console, so use notes; a process blocked inside the
kernel cannot be killed before its call returns (the desktop closes its pipes, which releases it).
