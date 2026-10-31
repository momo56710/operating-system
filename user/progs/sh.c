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
    size_t n = strlen(argv[0]);
    if (n > 4 && !strcmp(argv[0] + n - 4, ".lua")) {          /* `primes.lua` runs the script with Lua */
        static const char *lua_dirs[] = { "", "/home/lua/", "/disk/lua/", 0 };
        static char script[256];
        struct stat st;
        for (int i = 0; lua_dirs[i]; i++) {
            strcpy(script, lua_dirs[i]);
            strcat(script, argv[0]);
            if (stat(script, &st) < 0 || st.type != FT_FILE) continue;
            char *nargv[MAX_ARGS + 2];
            int k = 0;
            nargv[k++] = "lua";
            nargv[k++] = script;
            for (int j = 1; argv[j] && k < MAX_ARGS + 1; j++) nargv[k++] = argv[j];
            nargv[k] = 0;
            exec("/bin/lua", nargv);
        }
    }
    dprintf(2, "sh: %s: command not found\n", argv[0]);
    exit(127);
}

/* ---- aliases: your own names for commands ---- */

#define MAX_ALIASES 32
static struct alias { char name[32]; char value[128]; } aliases[MAX_ALIASES];
static int nalias;

static struct alias *find_alias(const char *name, size_t len) {
    for (int i = 0; i < nalias; i++)
        if (strlen(aliases[i].name) == len && !strncmp(aliases[i].name, name, len)) return &aliases[i];
    return 0;
}

static void set_alias(const char *name, const char *value) {
    struct alias *a = find_alias(name, strlen(name));
    if (!a) {
        if (nalias == MAX_ALIASES) { dprintf(2, "alias: too many aliases\n"); return; }
        a = &aliases[nalias++];
    }
    strncpy(a->name, name, sizeof a->name - 1);
    strncpy(a->value, value, sizeof a->value - 1);
}

/* Replace the first word of every command on the line (the start, and after each '|') by its alias.
 * Done once, as text, so an alias may contain pipes and options, and `alias ls='ls -l'` works. */
static int expand_aliases(const char *in, char *out, size_t max) {
    size_t o = 0;
    int at_start = 1;
    char quote = 0;
    for (const char *p = in; *p; ) {
        if (at_start) {
            while (*p == ' ' || *p == '\t') { if (o + 1 >= max) return -1; out[o++] = *p++; }
            const char *start = p;
            while (*p && !strchr(" \t\n|<>\"'", *p)) p++;
            size_t len = (size_t)(p - start);
            struct alias *a = len ? find_alias(start, len) : 0;
            const char *text = a ? a->value : start;
            size_t tl = a ? strlen(a->value) : len;
            if (o + tl + 1 >= max) return -1;
            memcpy(out + o, text, tl);
            o += tl;
            at_start = 0;
            continue;
        }
        char c = *p++;
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '|') at_start = 1;
        if (o + 1 >= max) return -1;
        out[o++] = c;
    }
    out[o] = 0;
    return 0;
}

static void execute_line(const char *line);

/* Run every line of a file in this shell: blank lines and lines starting with # are skipped. */
static int run_script(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char line[256];
    long n;
    while ((n = read_line(fd, line, sizeof line)) > 0) {
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != '#') execute_line(p);
    }
    close(fd);
    return 0;
}

static int find_program(const char *name, char *path, size_t max) {
    struct stat st;
    for (int i = 0; search_dirs[i]; i++) {
        snprintf(path, max, "%s/%s", search_dirs[i], name);
        if (stat(path, &st) == 0 && st.type == FT_FILE) return 1;
    }
    return 0;
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
    if (!strcmp(a[0], "alias")) {
        if (c->argc == 1)
            for (int i = 0; i < nalias; i++) printf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
        for (int i = 1; i < c->argc; i++) {
            char *eq = strchr(a[i], '=');
            if (eq) { *eq = 0; set_alias(a[i], eq + 1); }
            else {
                struct alias *al = find_alias(a[i], strlen(a[i]));
                if (al) printf("alias %s='%s'\n", al->name, al->value); else dprintf(2, "alias: %s: not found\n", a[i]);
            }
        }
        return 1;
    }
    if (!strcmp(a[0], "unalias")) {
        for (int i = 1; i < c->argc; i++) {
            if (!strcmp(a[i], "-a")) { nalias = 0; continue; }
            struct alias *al = find_alias(a[i], strlen(a[i]));
            if (!al) { dprintf(2, "unalias: %s: not found\n", a[i]); continue; }
            *al = aliases[--nalias];
        }
        return 1;
    }
    if (!strcmp(a[0], "which")) {
        for (int i = 1; i < c->argc; i++) {
            char path[256];
            struct alias *al = find_alias(a[i], strlen(a[i]));
            if (al) printf("%s: alias for '%s'\n", a[i], al->value);
            else if (!strcmp(a[i], "cd") || !strcmp(a[i], "pwd") || !strcmp(a[i], "exit") || !strcmp(a[i], "clear") ||
                     !strcmp(a[i], "alias") || !strcmp(a[i], "unalias") || !strcmp(a[i], "source") || !strcmp(a[i], "which") ||
                     !strcmp(a[i], "help")) printf("%s: shell built-in\n", a[i]);
            else if (find_program(a[i], path, sizeof path)) printf("%s\n", path);
            else printf("%s: not found\n", a[i]);
        }
        return 1;
    }
    if (!strcmp(a[0], "source") || !strcmp(a[0], ".")) {
        if (c->argc != 2) { dprintf(2, "usage: source file\n"); return 1; }
        char path[256];
        strncpy(path, a[1], sizeof path - 1);                  /* parsing the script reuses the word buffer */
        path[sizeof path - 1] = 0;
        if (run_script(path) < 0) dprintf(2, "source: cannot open %s\n", path);
        return 1;
    }
    if (!strcmp(a[0], "help")) {
        printf("Built-ins: cd pwd exit clear help alias unalias which source\n");
        printf("Programs:  ls cat echo cp mv rm mkdir touch wc grep head ps mem sleep uptime nano lua\n");
        printf("           hello args count primes fib sortdemo tree life mandel bench forktest umem crash\n");
        printf("Network:   ifconfig  ping 10.0.2.2  nslookup name  wget http://host/path  nc  echoserv  cksum\n");
        printf("Pipes:     ls / | grep bin        Redirect: echo hi > /tmp/a.txt, cat < file, echo more >> file\n");
        printf("Lua:       lua /home/lua/tour.lua    lua -e \"print(2^10)\"    lua    (an interactive prompt; Ctrl-D quits)\n");
        printf("           more scripts in /home/lua: primes files life calc http server sysinfo dns udp\n");
        printf("Editor:    nano /disk/notes.txt   (Ctrl-O saves, Ctrl-X exits, Ctrl-G shows all keys)\n");
        printf("Names:     alias ll='ls -l'   then type ll.  Put aliases in /disk/shrc to keep them.\n");
        printf("Disk:      /disk is a FAT32 volume that survives reboots: ls /disk, cat /disk/hello.txt, greet\n");
        printf("Places:    /bin programs (ramdisk)  /tmp scratch (memory)  /dev console null zero  /disk disk\n");
        return 1;
    }
    return 0;
}

static int last_status;                            /* exit status of the most recent command (1000+N: killed by exception N) */

static void run_pipeline(struct cmd *cmds, int n) {
    int prev_read = -1;
    int children = 0;
    int last_pid = -1;
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
        last_pid = pid;
        if (prev_read >= 0) close(prev_read);          /* the parent must not keep pipe ends open, or readers never see EOF */
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
    }
    if (prev_read >= 0) close(prev_read);
    while (children--) {
        int status = 0;
        int done = wait(&status);
        if (done < 0) break;
        if (done == last_pid) last_status = status;          /* a pipeline's status is that of its last command */
        if (status >= 1000) dprintf(2, "[killed by exception %d]\n", status - 1000);
    }
}

static void execute_one(const char *line);

/* A line may hold several commands separated by ';' (outside quotes). */
static void execute_line(const char *line) {
    char part[512];
    size_t n = 0;
    char quote = 0;
    for (const char *p = line; ; p++) {
        if (*p == '"' || *p == '\'') { if (!quote) quote = *p; else if (quote == *p) quote = 0; }
        if ((*p == ';' && !quote) || !*p) {
            part[n] = 0;
            execute_one(part);
            n = 0;
            if (!*p) break;
            continue;
        }
        if (n + 1 < sizeof part) part[n++] = *p;
    }
}

static void execute_one(const char *line) {
    last_status = 0;
    char expanded[512];
    struct cmd cmds[MAX_CMDS];
    if (expand_aliases(line, expanded, sizeof expanded) < 0) { dprintf(2, "sh: line too long after expanding aliases\n"); return; }
    int count = parse(expanded, cmds);
    if (count <= 0) return;
    if (count == 1 && !cmds[0].in && !cmds[0].out && builtin(&cmds[0])) return;
    run_pipeline(cmds, count);
}

int main(int argc, char **argv) {
    char line[256];
    if (argc >= 3 && !strcmp(argv[1], "-c")) {            /* sh -c "command": run one command line and exit with its status */
        execute_line(argv[2]);
        return last_status;
    }
    run_script("/etc/shrc");                       /* system defaults on the ramdisk */
    run_script("/disk/shrc");                      /* your own, kept on the disk */
    for (;;) {
        char cwd[128];
        if (getcwd(cwd, sizeof cwd) < 0) strcpy(cwd, "?");
        printf("LeOS:%s$ ", cwd);
        long n = read_line(0, line, sizeof line);
        if (n == 0) { printf("exit\n"); return 0; }          /* Ctrl-D */
        execute_line(line);
    }
}
