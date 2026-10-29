# Phase 6: Filesystem, storage and shell

Date: 2026-10-28
Status: **complete.** Exit gate passed. Tag to apply: `v1.0-shell`.

## What I did
**Virtual filesystem** (`kernel/fs/vfs.c`, `kernel/include/vfs.h`)
- A vnode interface (`read write lookup readdir create unlink truncate getsize release close`) that every filesystem implements, plus a mount table, path normalisation (`.`, `..`, repeated slashes, relative paths from the caller's working directory) and an open-file layer with offsets.
- Mount points win over whatever is underneath, so `/dev`, `/tmp` and `/disk` work even when the directory below is empty.
- Per-process: 16 file descriptors, a working directory. fork and spawn inherit both (shared open files share their offset), exit closes everything, and `exec` keeps descriptors, so redirection works.

**Filesystems**
- **Ramdisk** (`memfs.c`, `tar.c`): the boot module `initrd.tar` is unpacked into an in-memory tree, mounted read-only on `/`. File contents point straight into the archive; nothing is copied. It holds `/bin` (all programs), `/etc/motd`, `/home/readme.txt`.
- **tmpfs** (`memfs.c`, writable): mounted on `/tmp`. Files grow by doubling.
- **devfs** (`devfs.c`): `/dev/console`, `/dev/null`, `/dev/zero`. The console reads whole edited lines (echo, backspace, Ctrl-D as end of input) and understands `ESC [ 2 J` to clear the screen.
- **FAT32** (`fat32.c`): reads, writes, creates, deletes and truncates files and directories, with long file names (read and create) and 8.3 names with correct lower-case flags. Directories grow when full, deleted slots are reused, chains are extended on write, writes past the end zero-fill the gap. Mounted on `/disk`.

**Storage**
- `pci.c`: legacy PCI configuration access and a bus scan.
- `virtio_blk.c`: a virtio-blk driver over the legacy I/O-port interface (what `-drive if=virtio` gives). One virtqueue, requests of up to 8 sectors, submitted and polled one at a time. Rings and buffers use physically contiguous memory (new `pmm_alloc_contiguous`).
- `bcache.c`: a 1024-sector (512 KiB) direct-mapped **write-through** block cache with hit/miss counters. Misses are fetched in runs of up to 8 sectors per request.

**Pipes and redirection** (`pipe.c`, shell)
- 4 KiB pipes with blocking readers and writers, end-of-file when the last writer closes, broken-pipe errors when the last reader closes. New `waitq` primitive in the scheduler for blocking.
- New system calls: `open close read lseek stat readdir dup2 pipe chdir getcwd mkdir unlink sysinfo`. Numbers and structures now live in one shared header, `abi/abi.h`, used by both kernel and user programs. `exec` takes a path and up to 31 arguments.

**Userspace** (`user/`)
- `sh`: prompt with the working directory, quotes, `|` pipelines of up to 8 commands, `<` `>` `>>`, built-ins `cd pwd exit clear help`. Searches `/bin`, then `/disk/bin`, then the current directory. Reports a process killed by an exception.
- Utilities, each a separate program: `ls` (`-l`) `cat` `echo` `cp` `rm` `mkdir` `touch` `wc` `grep` `head` `ps` `mem` `sleep` `uptime`, plus `greet`, which lives only on the disk.
- libc: file, directory and pipe calls, `dprintf`, `read_line`, `write_all`, more string functions, `-` (left-align) in `printf`.
- `init` (kernel task) opens the console as stdin/stdout/stderr and keeps `/bin/sh` running, restarting it if it exits. The old kernel shell remains available with `make iso EXTRA=-DKSHELL`.

**Tools and build**
- `tools/mkfat32.py` builds the FAT32 image (there is no `mkfs.fat` here). `tools/fatcheck.py` checks an image for consistency.
- `make disk` / `make disk-reset` build `disk.img` from `diskfiles/` plus `/bin/greet`. It is built only if missing, so files you save on it persist. `make run` attaches it as a virtio disk and now boots the CD first (`-boot order=d`).
- The ramdisk is built from the programs and `initrd/`.

## Results
All by typing into the shell under QEMU and reading the serial log.
- **Exit gate:** boot to a prompt; `ls /disk`, `cat /disk/hello.txt` and `greet` (a program that exists only on the disk image) all work.
- **Files and pipes (tmpfs):** create, append (`>>`), truncate (`>`), copy, `wc`, `mkdir`, `cd`, relative paths, `rm`, quoting, `cat < file`, 2- and 3-stage pipelines. Writing to the read-only ramdisk and opening a missing file fail with proper messages. `cat > file` followed by typing and Ctrl-D creates a file from the keyboard.
- **FAT32 reading:** long names (`A longer file name.txt`), mixed case (`ReadMe.TXT`), nested directories.
- **FAT32 writing:** new directory, create, append, truncate, long and mixed-case names, a 43 KB binary copied onto the disk and **run from the disk**; a 45-file directory (growing past three clusters), deleting 15 of them, reusing slots, long names alongside.
- **Persistence:** two separate boots of one image. The `greet` counter went 1, 2, then 3 after the reboot; a file, a directory and a copied 47 KB program were all there on the second boot and the program ran.
- **Consistency:** `fatcheck.py` found no problems on three images (freshly built; after the persistence test; after the many-files test): FAT copies identical, no cross-linked or leaked clusters, valid long-name checksums, sizes matching chains. `file(1)` independently reads the built image as 32-bit FAT with the right geometry.
- **Block cache:** reading a 47 KB file a second and third time added 2 misses each (reading from the cache, 184 hits each).
- **No leaks:** free memory and kernel heap were identical (518368 KiB free, 56,576 B heap) before and after 30 pipeline and crash runs. Earlier, 130 processes did the same.
- **Regressions:** the 100 MiB memory test, the sync self-test and five kernel exception tests still pass, and the kernel shell still boots with `-DKSHELL`.

## Notes and limits
- **The shell and its programs come from the ramdisk inside the ISO**, not from the disk. The disk is mounted at `/disk` and holds your files and extra programs. Booting with the disk as the root is a further step (it needs a boot-time choice of root).
- **The FAT32 code has only been checked against my own image builder and my own checker.** `mkfs.fat`, `fsck.fat` and `mtools` are not installed here. To cross-check with real tools: `sudo pacman -S dosfstools mtools`, then `fsck.fat -n disk.img` and `mdir -i disk.img ::`. Please run them once; I'd like to know if they disagree.
- The disk image has no partition table, 512-byte clusters and a 64 MiB size. No timestamps are written (a fixed date is stored). No file permissions.
- Open files on FAT32 keep a private copy of the size, so two handles open on the same file at once can disagree until reopened. In memory (`/tmp`) they share one structure.
- FAT32 uses one big lock. The cache is write-through and the driver polls, so writes are slow but safe. There is no interrupt-driven I/O and no `sync` (nothing needs it).
- Console input: one reader at a time (the foreground program). There is no job control, no background `&`, no environment variables, no wildcards (`*`) and no history.
- Kernel stacks are still 16 KiB without guard pages; the disk path uses about 6 KiB of it during `exec`.
- Not verified: real hardware, VirtualBox (virtio-blk is QEMU specific; real machines need AHCI), UEFI boot, and whether the double fault really runs on its IST stack.

## How to try it
    make disk        # builds disk.img the first time
    make run         # then, at the prompt:
    ls /disk
    cat /disk/hello.txt
    greet            # run it again after a reboot: the count goes up
    echo hi > /disk/mine.txt
    ls /bin | grep s | wc

## Addendum: the nano editor and naming your own commands (2026-10-29)

### nano (`user/progs/nano.c`)
GNU nano itself needs ncurses, terminfo and a full C library, so I wrote a nano-style editor for LeOS. Same keys and layout: an inverse-video title bar with `[Modified]`, the text, a centred message line, and two rows of shortcuts.

| Keys | Action |
|---|---|
| Arrows, Home, End, PgUp, PgDn | move (Ctrl-A / Ctrl-E: start / end of line) |
| Backspace, Delete, Enter, Tab | edit |
| Ctrl-K / Ctrl-U | cut a line (repeat to cut several) / paste |
| Ctrl-W | search (Enter alone repeats the last search; wraps around) |
| Ctrl-O | write out (prompt is prefilled with the file name) |
| Ctrl-X | exit; asks Y / N / Ctrl-C if there are unsaved changes |
| Ctrl-G, Ctrl-C, Ctrl-L | help screen, show position, redraw |

What this needed in the kernel:
- **Keyboard:** the 0xE0-prefixed keys (arrows, Home, End, Delete, PgUp, PgDn, Insert, keypad Enter, right Ctrl) now arrive as the usual ANSI sequences (Up = `ESC [ A`, Delete = `ESC [ 3 ~`).
- **Raw console mode:** new system call `ioctl(fd, request, arg)` with `TTY_RAW` (every key delivered at once, no echo or line editing) and `TTY_SIZE` (rows and columns). The console leaves raw mode automatically when any process exits, so a crashed editor cannot leave the shell unusable. In normal line mode arrow-key sequences are now ignored instead of landing in the typed text.
- **Framebuffer console rewritten:** it keeps a shadow copy of the screen, so the cursor no longer erases the character under it, and it understands ANSI escapes: cursor positioning (`ESC[r;cH`, `A B C D`), clear screen / line (`J`, `K`), reverse video (`7m`, `0m`), and cursor hide/show. Writing in the last column now waits to wrap until another character arrives, like a real terminal, so a full-screen program cannot scroll the screen by accident. The serial port gets the same bytes, so a real terminal on the serial line works too.
- **Library:** `realloc`, `snprintf`, `strdup`, `ioctl`.

### Naming commands your way
- Shell built-ins `alias`, `unalias`, `which` and `source` (`.` also works). An alias replaces the first word of every command (at the start of a line and after each `|`), once, as text. So aliases can contain options and pipes, and `alias ls='ls -l'` works.
- The shell runs `/etc/shrc` (ramdisk defaults: `ll dir cls type md del copy move`) and then `/disk/shrc` (your own, persists) at startup. Blank lines and `#` comments are allowed. `source /disk/shrc` applies changes without restarting.
- `mv` (files only; copies then removes, so it also moves between filesystems; moves into a directory if the destination is one).
- `diskfiles/shrc` is a commented template that lands on a freshly built disk. An existing disk gets one by running `nano /disk/shrc`.

### Verified
All by typing real keystrokes into QEMU and screenshotting or reading the serial log:
- nano: title bar, text, shortcut bars and prompts render correctly (screenshots). Typing, Enter, arrows, End, Home, Delete, cut (Ctrl-K), paste (Ctrl-U), search (Ctrl-W), write out (Ctrl-O, prefilled name), exit (Ctrl-X). The saved file reads back as expected. Answering N to the exit prompt discards changes (file unchanged). A file saved under `/disk` was there afterwards. `ps` showed no leftover processes.
- Aliases: the default ones work (`ll`, `cls`, `type`); `alias hi='echo hello there'` then `hi Momo`; an alias containing a pipe (`bins sh`); `alias ls='ls -l'`; `which` (alias, built-in, program path, not found); `unalias`. After a reboot on the same disk, aliases written to `/disk/shrc` were defined at startup.
- `mv` renamed a file within `/tmp` and moved it onto `/disk`.
- Regressions after the framebuffer rewrite: scrolling, clear screen, the file and pipe commands, process crashes, `tree`, `umem`, the 100 MiB memory test, and a kernel page-fault dump all still work.

### Limits
- nano: no undo, no replace, no line numbers, no "read file into buffer", no mouse, no syntax colours. Long lines scroll sideways instead of wrapping. Text is ASCII only; tabs show 4 columns wide. The Esc key by itself is not used. Up to 200,000 lines.
- Aliases take no arguments in the middle (`alias x='echo $1'` is not supported; there are no variables) and are not expanded inside other aliases.
- Tested with simulated keystrokes through QEMU, not a physical keyboard.
