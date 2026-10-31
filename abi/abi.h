#pragma once
/* The contract between the kernel and user programs: system call numbers and the
 * structures that cross the boundary. Included by both sides (-Iabi). */
#include <stdint.h>

#define SYS_WRITE   1   /* write(fd, buf, len) -> bytes or -1 */
#define SYS_EXIT    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SLEEP   5   /* sleep(ms) */
#define SYS_BRK     6
#define SYS_MMAP    7
#define SYS_MUNMAP  8
#define SYS_FORK    9
#define SYS_EXEC    10  /* exec(path, argv) */
#define SYS_WAIT    11
#define SYS_UPTIME  12
#define SYS_OPEN    13  /* open(path, flags) -> fd */
#define SYS_CLOSE   14
#define SYS_READ    15  /* read(fd, buf, len) -> bytes, 0 at end of file, -1 on error */
#define SYS_LSEEK   16  /* lseek(fd, offset, whence) -> new offset */
#define SYS_STAT    17  /* stat(path, struct stat *) */
#define SYS_READDIR 18  /* readdir(fd, index, struct dirent *) -> 1 got one, 0 end, -1 error */
#define SYS_DUP2    19
#define SYS_PIPE    20  /* pipe(int fds[2]) */
#define SYS_CHDIR   21
#define SYS_GETCWD  22  /* getcwd(buf, size) */
#define SYS_MKDIR   23
#define SYS_UNLINK  24
#define SYS_SYSINFO 25  /* sysinfo(what, index, buf) */
#define SYS_IOCTL   26  /* ioctl(fd, request, arg): terminal control on the console */

#define SYS_SOCKET   27 /* socket(type) -> fd */
#define SYS_BIND     28 /* bind(fd, port) */
#define SYS_CONNECT  29 /* connect(fd, struct sockaddr_in *) */
#define SYS_LISTEN   30 /* listen(fd, backlog) */
#define SYS_ACCEPT   31 /* accept(fd, struct sockaddr_in *peer) -> new fd */
#define SYS_SENDTO   32 /* sendto(fd, struct sockmsg *) -> bytes */
#define SYS_RECVFROM 33 /* recvfrom(fd, struct sockmsg *) -> bytes, 0 at end of stream, -1 on error or timeout */
#define SYS_NETCTL   34 /* netctl(op, struct *) */
#define SYS_SHUTDOWN 35 /* shutdown(fd): no more sending; reading continues */
#define SYS_TIME     36 /* seconds since 1970-01-01 00:00:00 UTC */
#define SYS_KILL     38 /* kill(pid): stop a child process */
#define SYS_GFX      37 /* gfx(op, arg1, arg2): direct screen access and raw keyboard/mouse input */

/* gfx operations */
#define GFX_INFO     1  /* arg1: struct gfx_info * */
#define GFX_MAP      2  /* map the framebuffer into this process; returns its address */
#define GFX_ACQUIRE  3  /* take over the screen and the input devices (the text console goes quiet) */
#define GFX_RELEASE  4  /* give them back */
#define GFX_POLL     5  /* arg1: struct gfx_poll *: wait for input events */

/* ioctl requests */
#define TTY_RAW  1      /* arg 1: deliver every key as it is typed, no echo or line editing; 0: normal line mode */
#define TTY_SIZE 2      /* returns (rows << 16) | columns */
#define PIPE_AVAIL 3    /* on a pipe: bytes that can be read right now; 0 = none yet; -2 = empty and every writer is gone */

#define WNOHANG 1       /* wait(status, WNOHANG): return 0 at once if no child has finished */

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FT_FILE 1
#define FT_DIR  2
#define FT_CHAR 3
#define FT_PIPE 4
#define FT_SOCK 5

#define NAME_MAX_LEN 56
#define PATH_MAX_LEN 256

struct dirent {
    char     name[NAME_MAX_LEN];
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

struct stat {
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
};

/* sysinfo(what, index, buf) */
#define SI_MEM   0   /* buf: struct si_mem */
#define SI_TASK  1   /* buf: struct si_task for the index-th task; returns 1, or 0 past the end */
#define SI_DISK  2   /* buf: struct si_disk (block cache and driver counters) */

struct si_mem {
    uint64_t total_kb, free_kb, heap_used, heap_free, uptime_ms;
};

struct si_task {
    uint64_t pid, parent_pid;
    uint32_t state;           /* 0 running, 1 ready, 2 sleeping, 3 blocked, 4 zombie, 5 defunct */
    uint32_t is_user;
    char     name[16];
};

struct si_disk {
    uint64_t present;         /* a disk driver is active */
    uint64_t sectors;
    uint64_t cache_hits, cache_misses, sector_reads, sector_writes;
};

/* ---- networking ---- IP addresses and ports are in host byte order. */
#define SOCK_UDP 1
#define SOCK_TCP 2

struct sockaddr_in { uint32_t ip; uint16_t port; uint16_t reserved; };

struct sockmsg {                  /* one datagram or one chunk of a stream, in either direction */
    uint64_t buf;
    uint64_t len;
    uint32_t ip;                  /* sendto: destination (0 = the connected peer); recvfrom: sender */
    uint16_t port;
    uint16_t reserved;
    int32_t  timeout_ms;          /* recvfrom: give up after this long (<= 0: wait forever) */
    int32_t  flags;
};

#define NET_INFO 1                /* netctl(NET_INFO, struct net_info *) */
#define NET_PING 2                /* netctl(NET_PING, struct ping_req *) */
#define NET_SETDNS 3              /* netctl(NET_SETDNS, uint32_t *ip): choose the DNS server */

struct net_info {
    uint8_t  mac[6];
    uint16_t up;
    uint32_t ip, mask, gw, dns;
    uint64_t rx_frames, tx_frames, rx_bytes, tx_bytes, rx_dropped;
};

struct ping_req {
    uint32_t ip;
    uint32_t seq;
    int32_t  timeout_ms;
    int32_t  rtt_ms;              /* out: round-trip time, or -1 */
};

/* ---- graphics and input ---- */
struct gfx_info {
    uint32_t width, height, pitch, bpp;       /* pitch in bytes */
    uint32_t red_shift, green_shift, blue_shift, reserved;
    uint64_t size;                            /* bytes of framebuffer memory */
};

#define EV_KEY   1
#define EV_MOUSE 2
#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4
#define MOD_CAPS  8

struct input_event {
    uint32_t type;                            /* EV_KEY or EV_MOUSE */
    int32_t  code;                            /* EV_KEY: scancode (+0x100 for the grey extended keys such as the arrows) */
    int32_t  value;                           /* EV_KEY: 1 pressed, 0 released */
    int32_t  ch;                              /* EV_KEY: the character typed (shift and caps applied), or 0 */
    int32_t  x, y;                            /* EV_MOUSE: pointer position in pixels */
    int32_t  wheel;                           /* EV_MOUSE: scroll steps (+ = away from you) */
    uint32_t buttons;                         /* EV_MOUSE: bit 0 left, 1 right, 2 middle */
    uint32_t mods;                            /* MOD_* at the time of the event */
    uint32_t reserved;
};

struct gfx_poll {
    uint64_t buf;                             /* struct input_event array */
    uint32_t max;
    int32_t  timeout_ms;                      /* 0: do not wait; < 0: wait forever */
    uint32_t count;                           /* out: events stored */
    uint32_t reserved;
};
