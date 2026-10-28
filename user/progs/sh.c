#include "libc.h"

/* The LeOS shell: runs programs, with pipes (a | b), redirection (< > >>), quotes and a few built-ins. */

#define MAX_CMDS 8
#define MAX_ARGS 31

struct cmd {
    char *argv[MAX_ARGS + 1];
    int   argc;
    char *in, *out;
    int   append;
};

static const char *search_dirs[] = { "/bin", "/disk/bin", 0 };

/* Split a line into commands separated by '|'. Words may be quoted. Each word is copied into
 * `pool`, so the line itself is never modified (terminating a word in place would overwrite
 * the space or operator right after it). */
static char pool[1024];

static int parse(const char *line, struct cmd *cmds) {
    int n = 0;
    size_t used = 0;
    memset(&cmds[0], 0, sizeof cmds[0]);
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        struct cmd *c = &cmds[n];
        if (*p == '|') {
            if (!c->argc) { dprintf(2, "sh: nothing before '|'\n"); return -1; }
            if (++n == MAX_CMDS) { dprintf(2, "sh: too many commands in a pipeline\n"); return -1; }
            memset(&cmds[n], 0, sizeof cmds[n]);
            p++;
            continue;
        }
        char **target = 0;
        if (*p == '<') { target = &c->in; p++; }
        else if (*p == '>') { c->append = p[1] == '>'; p += c->append ? 2 : 1; target = &c->out; }
        while (target && (*p == ' ' || *p == '\t')) p++;

        char *word = &pool[used];
        size_t len = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '|' && *p != '<' && *p != '>') {
            if (used + len + 2 >= sizeof pool) { dprintf(2, "sh: line too long\n"); return -1; }
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                while (*p && *p != q) word[len++] = *p++;
                if (*p) p++;
            } else {
                word[len++] = *p++;
            }
        }
        word[len] = 0;
        used += len + 1;
        if (target) {
            if (!len) { dprintf(2, "sh: missing file name\n"); return -1; }
            *target = word;
        } else if (len) {
            if (c->argc == MAX_ARGS) { dprintf(2, "sh: too many arguments\n"); return -1; }
            c->argv[c->argc++] = word;
        }
    }
    if (n == 0 && !cmds[0].argc) return 0;
    if (!cmds[n].argc) { dprintf(2, "sh: nothing after '|'\n"); return -1; }
    return n + 1;
}

static void exec_program(char **argv) {
    char path[256];
    if (strchr(argv[0], '/')) {
        exec(argv[0], argv);
    } else {
        for (int i = 0; search_dirs[i]; i++) {
            strcpy(path, search_dirs[i]);
            strcat(path, "/");
            strcat(path, argv[0]);
            exec(path, argv);                      /* returns only if the file does not exist or is not a program */
        }
        char here[256];                            /* then the current directory */
        if (getcwd(here, sizeof here) == 0) {
            strcpy(path, here);
            if (strcmp(here, "/")) strcat(path, "/");
            strcat(path, argv[0]);
            exec(path, argv);
        }
    }
    dprintf(2, "sh: %s: command not found\n", argv[0]);
    exit(127);
}

static int builtin(struct cmd *c) {
    char **a = c->argv;
    if (!strcmp(a[0], "cd")) {
        const char *dir = c->argc > 1 ? a[1] : "/";
        if (chdir(dir) < 0) dprintf(2, "cd: %s: no such directory\n", dir);
        return 1;
    }
    if (!strcmp(a[0], "pwd")) { char b[128]; if (getcwd(b, sizeof b) == 0) printf("%s\n", b); return 1; }
    if (!strcmp(a[0], "exit")) exit(c->argc > 1 ? atoi(a[1]) : 0);
    if (!strcmp(a[0], "clear")) { write(1, "\x1b[2J", 4); return 1; }
    if (!strcmp(a[0], "help")) {
        printf("Built-ins: cd pwd exit clear help\n");
        printf("Programs:  ls cat echo cp rm mkdir touch wc grep head ps mem sleep uptime\n");
        printf("           hello args count primes fib sortdemo tree life mandel bench forktest umem crash\n");
        printf("Pipes:     ls / | grep bin        Redirect: echo hi > /tmp/a.txt, cat < file, echo more >> file\n");
        printf("Input:     cat > /tmp/notes.txt   then type lines and press Ctrl-D\n");
        printf("Disk:      /disk is a FAT32 volume that survives reboots: ls /disk, cat /disk/hello.txt, greet\n");
        printf("Places:    /bin programs (ramdisk)  /tmp scratch (memory)  /dev console null zero  /disk disk\n");
        return 1;
    }
    return 0;
}

static void run_pipeline(struct cmd *cmds, int n) {
    int prev_read = -1;
    int children = 0;
    for (int i = 0; i < n; i++) {
        int fds[2] = { -1, -1 };
        if (i + 1 < n && pipe(fds) < 0) { dprintf(2, "sh: cannot create a pipe\n"); break; }
        int pid = fork();
        if (pid < 0) { dprintf(2, "sh: fork failed\n"); break; }
        if (pid == 0) {
            if (prev_read >= 0) { dup2(prev_read, 0); close(prev_read); }
            if (fds[1] >= 0)    { dup2(fds[1], 1); close(fds[1]); close(fds[0]); }
            if (cmds[i].in) {
                int fd = open(cmds[i].in, O_RDONLY);
                if (fd < 0) { dprintf(2, "sh: cannot open %s\n", cmds[i].in); exit(1); }
                dup2(fd, 0); close(fd);
            }
            if (cmds[i].out) {
                int fd = open(cmds[i].out, O_WRONLY | O_CREAT | (cmds[i].append ? O_APPEND : O_TRUNC));
                if (fd < 0) { dprintf(2, "sh: cannot write %s\n", cmds[i].out); exit(1); }
                dup2(fd, 1); close(fd);
            }
            exec_program(cmds[i].argv);
        }
        children++;
        if (prev_read >= 0) close(prev_read);          /* the parent must not keep pipe ends open, or readers never see EOF */
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
    }
    if (prev_read >= 0) close(prev_read);
    while (children--) {
        int status = 0;
        if (wait(&status) < 0) break;
        if (status >= 1000) dprintf(2, "[killed by exception %d]\n", status - 1000);
    }
}

int main(void) {
    char line[256];
    struct cmd cmds[MAX_CMDS];
    for (;;) {
        char cwd[128];
        if (getcwd(cwd, sizeof cwd) < 0) strcpy(cwd, "?");
        printf("LeOS:%s$ ", cwd);
        long n = read_line(0, line, sizeof line);
        if (n == 0) { printf("exit\n"); return 0; }          /* Ctrl-D */
        int count = parse(line, cmds);
        if (count <= 0) continue;
        if (count == 1 && !cmds[0].in && !cmds[0].out && builtin(&cmds[0])) continue;
        run_pipeline(cmds, count);
    }
}
