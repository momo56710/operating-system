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
