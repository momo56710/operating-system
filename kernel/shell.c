#include "shell.h"
#include "keyboard.h"
#include "kprintf.h"
#include "task.h"
#include "pmm.h"
#include "heap.h"
#include "string.h"
#include "io.h"

/* A stand-in shell that lives in the kernel. It is replaced in Phase 6 by a real
 * userspace shell that launches programs from disk. */

#define LINE_MAX 128
#define ARGS_MAX 8

static unsigned long parse_ulong(const char *s, int *ok) {
    unsigned long v = 0;
    *ok = (*s != 0);
    for (; *s; s++) {
        if (*s < '0' || *s > '9') { *ok = 0; return 0; }
        v = v * 10 + (*s - '0');
    }
    return v;
}

static void cmd_help(int argc, char **argv);

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) kprintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    kprintf("\n");
}

static void cmd_clear(int argc, char **argv) { (void)argc; (void)argv; kclear(); }

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t ms = uptime_ms();
    kprintf("up %lu.%03lu s\n", ms / 1000, ms % 1000);
}

static void cmd_sleep(int argc, char **argv) {
    int ok = 0;
    unsigned long ms = argc == 2 ? parse_ulong(argv[1], &ok) : 0;
    if (!ok) { kprintf("usage: sleep <milliseconds>\n"); return; }
    sleep_ms(ms);
    kprintf("slept %lu ms\n", ms);
}

static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t span, used, fr;
    heap_stats(&span, &used, &fr);
    kprintf("physical: %lu MiB total, %lu MiB free (%lu frames)\n",
            pmm_total_frames() * PAGE_SIZE >> 20, pmm_free_frames() * PAGE_SIZE >> 20, pmm_free_frames());
    kprintf("heap:     %lu KiB mapped span, %lu B in use, %lu B free\n", span >> 10, used, fr);
}

static void cmd_ps(int argc, char **argv) { (void)argc; (void)argv; task_print_all(); }

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("rebooting...\n");
    outb(0x64, 0xFE);                    /* pulse the CPU reset line through the keyboard controller */
    for (;;) __asm__ volatile ("hlt");
}

struct command { const char *name, *help; void (*fn)(int, char **); };

static const struct command commands[] = {
    { "help",    "list the commands",                cmd_help },
    { "echo",    "echo <text>: print the text",      cmd_echo },
    { "clear",   "clear the screen",                 cmd_clear },
    { "uptime",  "time since boot",                  cmd_uptime },
    { "sleep",   "sleep <ms>: wait, then say so",    cmd_sleep },
    { "meminfo", "physical memory and heap usage",   cmd_meminfo },
    { "ps",      "list tasks and their states",      cmd_ps },
    { "reboot",  "reset the machine",                cmd_reboot },
};
#define NCOMMANDS (sizeof commands / sizeof commands[0])

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    for (unsigned i = 0; i < NCOMMANDS; i++)
        kprintf("  %-8s %s\n", commands[i].name, commands[i].help);
}

static void run_line(char *line) {
    char *argv[ARGS_MAX];
    int argc = 0;
    for (char *p = line; *p; ) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (argc == ARGS_MAX) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (!argc) return;
    for (unsigned i = 0; i < NCOMMANDS; i++)
        if (!strcmp(argv[0], commands[i].name)) { commands[i].fn(argc, argv); return; }
    kprintf("unknown command: %s (try help)\n", argv[0]);
}

void shell_thread(void *arg) {
    (void)arg;
    char line[LINE_MAX];
    unsigned len = 0;
    kprintf("LeOS shell. Type help.\nLeOS> ");
    for (;;) {
        char c = kbd_getc();
        if (c == '\n') {
            kputc('\n');
            line[len] = 0;
            run_line(line);
            len = 0;
            kprintf("LeOS> ");
        } else if (c == '\b') {
            if (len) { len--; kputc('\b'); kputc(' '); kputc('\b'); }
        } else if (c >= ' ' && c < 127 && len < LINE_MAX - 1) {
            line[len++] = c;
            kputc(c);
        }
    }
}
