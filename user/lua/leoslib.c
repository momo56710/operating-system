/* The "leos" library for Lua: LeOS's own system features. Sockets, DNS, ping, directories, processes,
 * memory. Compiled into the lua program next to the unmodified Lua source.
 *
 *   leos.resolve("example.com")          -> "93.184.216.34"
 *   leos.tcp(host, port [, timeout_ms])  -> socket        (connect)
 *   leos.listen(port [, backlog])        -> server socket
 *   leos.udp([port])                     -> datagram socket
 *   sock:send(data)  sock:recv([n [, timeout_ms]])  sock:recvall([limit])  sock:close()  sock:shutdown()
 *   server:accept()                      -> socket, peer_ip, peer_port
 *   sock:sendto(host, port, data)  sock:recvfrom([timeout_ms]) -> data, ip, port
 *   leos.ping(host [, timeout_ms])       -> milliseconds, or nil
 *   leos.dir(path)  leos.stat(path)  leos.getcwd()  leos.chdir(path)  leos.mkdir(path)
 *   leos.sleep(ms)  leos.uptime()  leos.netinfo()  leos.meminfo()  leos.ps()
 *   leos.poweroff()  leos.reboot()                        (do not return)
 *   leos.diskinfo()  leos.getpid()  leos.remove(path)  leos.exists(path)
 *   leos.spawn(path, arg1, ...)          -> process: run a program with its input and output on pipes
 *   p:read()   p:write(s)   p:closeinput()   p:kill()   p:pid()   p:close()
 *   leos.reap()                          -> pid, exit status of a finished child, or nil */

#include "lua.h"
#include "lauxlib.h"
#include "libc.h"

#define SOCK_META "leos.socket"

struct lsock { int fd; int type; };

static int fail(lua_State *L, const char *msg) {
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}

static struct lsock *check_sock(lua_State *L, int i) {
    struct lsock *s = luaL_checkudata(L, i, SOCK_META);
    if (s->fd < 0) luaL_error(L, "socket is closed");
    return s;
}

static struct lsock *new_sock(lua_State *L, int fd, int type) {
    struct lsock *s = lua_newuserdatauv(L, sizeof *s, 0);
    s->fd = fd; s->type = type;
    luaL_setmetatable(L, SOCK_META);
    return s;
}

static void push_ip(lua_State *L, unsigned ip) {
    char buf[16];
    ip_to_str(ip, buf);
    lua_pushstring(L, buf);
}

/* host name or dotted quad -> address, or raise a Lua error */
static unsigned need_ip(lua_State *L, int i) {
    const char *host = luaL_checkstring(L, i);
    unsigned ip = resolve(host);
    if (!ip) luaL_error(L, "cannot resolve '%s'", host);
    return ip;
}

static int l_resolve(lua_State *L) {
    unsigned ip = resolve(luaL_checkstring(L, 1));
    if (!ip) return fail(L, "no such host");
    push_ip(L, ip);
    return 1;
}

static int l_tcp(lua_State *L) {
    unsigned ip = need_ip(L, 1);
    unsigned port = (unsigned)luaL_checkinteger(L, 2);
    int fd = socket(SOCK_TCP);
    if (fd < 0) return fail(L, "no more sockets");
    if (connect(fd, ip, port) < 0) { close(fd); return fail(L, "connection failed"); }
    new_sock(L, fd, SOCK_TCP);
    return 1;
}

static int l_listen(lua_State *L) {
    unsigned port = (unsigned)luaL_checkinteger(L, 1);
    int backlog = (int)luaL_optinteger(L, 2, 4);
    int fd = socket(SOCK_TCP);
    if (fd < 0 || bind(fd, port) < 0 || listen(fd, backlog) < 0) { if (fd >= 0) close(fd); return fail(L, "cannot listen on that port"); }
    new_sock(L, fd, SOCK_TCP);
    return 1;
}

static int l_udp(lua_State *L) {
    int fd = socket(SOCK_UDP);
    if (fd < 0) return fail(L, "no more sockets");
    if (!lua_isnoneornil(L, 1) && bind(fd, (unsigned)luaL_checkinteger(L, 1)) < 0) { close(fd); return fail(L, "cannot bind that port"); }
    new_sock(L, fd, SOCK_UDP);
    return 1;
}

static int s_send(lua_State *L) {
    struct lsock *s = check_sock(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    long w = s->type == SOCK_TCP ? write_all(s->fd, data, len) : sendto(s->fd, data, len, 0, 0);
    if (w < 0) return fail(L, "send failed");
    lua_pushinteger(L, (lua_Integer)len);
    return 1;
}

static int s_recv(lua_State *L) {
    struct lsock *s = check_sock(L, 1);
    lua_Integer n = luaL_optinteger(L, 2, 4096);
    int timeout = (int)luaL_optinteger(L, 3, 0);
    if (n < 1) n = 1;
    if (n > 65536) n = 65536;
    luaL_Buffer b;
    char *buf = luaL_buffinitsize(L, &b, (size_t)n);
    long r = recvfrom(s->fd, buf, (size_t)n, 0, 0, timeout);
    if (r > 0) { luaL_pushresultsize(&b, (size_t)r); return 1; }
    return fail(L, r == 0 ? "closed" : "timeout");
}

static int s_recvall(lua_State *L) {
    struct lsock *s = check_sock(L, 1);
    lua_Integer limit = luaL_optinteger(L, 2, 16 << 20);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    lua_Integer total = 0;
    while (total < limit) {
        char *p = luaL_prepbuffsize(&b, 4096);
        long r = recvfrom(s->fd, p, 4096, 0, 0, 0);
        if (r <= 0) break;
        luaL_addsize(&b, (size_t)r);
        total += r;
    }
    luaL_pushresult(&b);
    return 1;
}

static int s_sendto(lua_State *L) {
    struct lsock *s = check_sock(L, 1);
    unsigned ip = need_ip(L, 2), port = (unsigned)luaL_checkinteger(L, 3);
    size_t len;
    const char *data = luaL_checklstring(L, 4, &len);
    if (sendto(s->fd, data, len, ip, port) < 0) return fail(L, "send failed");
    lua_pushinteger(L, (lua_Integer)len);
    return 1;
}

static int s_recvfrom(lua_State *L) {
    struct lsock *s = check_sock(L, 1);
    int timeout = (int)luaL_optinteger(L, 2, 0);
    char buf[2048];
    unsigned ip, port;
    long r = recvfrom(s->fd, buf, sizeof buf, &ip, &port, timeout);
    if (r < 0) return fail(L, "timeout");
    lua_pushlstring(L, buf, (size_t)r);
    push_ip(L, ip);
    lua_pushinteger(L, port);
    return 3;
}

static int s_accept(lua_State *L) {
    struct lsock *s = check_sock(L, 1);
    unsigned ip, port;
    int fd = accept(s->fd, &ip, &port);
    if (fd < 0) return fail(L, "accept failed");
    new_sock(L, fd, SOCK_TCP);
    push_ip(L, ip);
    lua_pushinteger(L, port);
    return 3;
}

static int s_shutdown(lua_State *L) { shutdown(check_sock(L, 1)->fd); return 0; }

static int s_close(lua_State *L) {
    struct lsock *s = luaL_checkudata(L, 1, SOCK_META);
    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
    return 0;
}

static int s_tostring(lua_State *L) {
    struct lsock *s = luaL_checkudata(L, 1, SOCK_META);
    if (s->fd < 0) lua_pushliteral(L, "socket (closed)");
    else lua_pushfstring(L, "socket (%s, fd %d)", s->type == SOCK_TCP ? "tcp" : "udp", s->fd);
    return 1;
}

static int l_ping(lua_State *L) {
    struct ping_req p = { need_ip(L, 1), 1, (int)luaL_optinteger(L, 2, 1000), -1 };
    if (netctl(NET_PING, &p) < 0) return fail(L, "no reply");
    lua_pushinteger(L, p.rtt_ms);
    return 1;
}

static const char *type_name(unsigned t) { return t == FT_DIR ? "dir" : t == FT_CHAR ? "device" : t == FT_PIPE ? "pipe" : "file"; }

static int l_dir(lua_State *L) {
    const char *path = luaL_optstring(L, 1, ".");
    int fd = open(path, O_RDONLY);
    if (fd < 0) return fail(L, "cannot open directory");
    lua_newtable(L);
    struct dirent d;
    for (unsigned long i = 0; readdir(fd, i, &d) > 0; i++) {
        lua_newtable(L);
        lua_pushstring(L, d.name); lua_setfield(L, -2, "name");
        lua_pushstring(L, type_name(d.type)); lua_setfield(L, -2, "type");
        lua_pushinteger(L, (lua_Integer)d.size); lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    close(fd);
    return 1;
}

static int l_stat(lua_State *L) {
    struct stat st;
    if (stat(luaL_checkstring(L, 1), &st) < 0) return fail(L, "no such file or directory");
    lua_newtable(L);
    lua_pushstring(L, type_name(st.type)); lua_setfield(L, -2, "type");
    lua_pushinteger(L, (lua_Integer)st.size); lua_setfield(L, -2, "size");
    return 1;
}

static int l_getcwd(lua_State *L) { char b[256]; if (getcwd(b, sizeof b) < 0) return fail(L, "getcwd failed"); lua_pushstring(L, b); return 1; }
static int l_chdir(lua_State *L) { if (chdir(luaL_checkstring(L, 1)) < 0) return fail(L, "no such directory"); lua_pushboolean(L, 1); return 1; }
static int l_mkdir(lua_State *L) { if (mkdir(luaL_checkstring(L, 1)) < 0) return fail(L, "cannot create directory"); lua_pushboolean(L, 1); return 1; }
static int l_sleep(lua_State *L) { sleep_ms((unsigned long)luaL_checkinteger(L, 1)); return 0; }
static int l_uptime(lua_State *L) { lua_pushinteger(L, (lua_Integer)uptime_ms()); return 1; }

static void set_int(lua_State *L, const char *k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); }

static int l_netinfo(lua_State *L) {
    struct net_info n;
    if (netctl(NET_INFO, &n) < 0 || !n.up) return fail(L, "no network");
    lua_newtable(L);
    char mac[20];
    snprintf(mac, sizeof mac, "%02x:%02x:%02x:%02x:%02x:%02x", n.mac[0], n.mac[1], n.mac[2], n.mac[3], n.mac[4], n.mac[5]);
    lua_pushstring(L, mac); lua_setfield(L, -2, "mac");
    push_ip(L, n.ip); lua_setfield(L, -2, "ip");
    push_ip(L, n.mask); lua_setfield(L, -2, "netmask");
    push_ip(L, n.gw); lua_setfield(L, -2, "gateway");
    push_ip(L, n.dns); lua_setfield(L, -2, "dns");
    set_int(L, "rx_frames", (lua_Integer)n.rx_frames);
    set_int(L, "tx_frames", (lua_Integer)n.tx_frames);
    return 1;
}

static int l_meminfo(lua_State *L) {
    struct si_mem m;
    if (sysinfo(SI_MEM, 0, &m) < 0) return fail(L, "sysinfo failed");
    lua_newtable(L);
    set_int(L, "total_kb", (lua_Integer)m.total_kb);
    set_int(L, "free_kb", (lua_Integer)m.free_kb);
    set_int(L, "heap_used", (lua_Integer)m.heap_used);
    set_int(L, "uptime_ms", (lua_Integer)m.uptime_ms);
    return 1;
}

static int l_ps(lua_State *L) {
    static const char *states[] = { "running", "ready", "sleeping", "blocked", "zombie", "defunct" };
    struct si_task t;
    lua_newtable(L);
    for (unsigned long i = 0; sysinfo(SI_TASK, i, &t) > 0; i++) {
        lua_newtable(L);
        set_int(L, "pid", (lua_Integer)t.pid);
        set_int(L, "ppid", (lua_Integer)t.parent_pid);
        lua_pushstring(L, states[t.state < 6 ? t.state : 0]); lua_setfield(L, -2, "state");
        lua_pushstring(L, t.is_user ? "user" : "kernel"); lua_setfield(L, -2, "kind");
        lua_pushstring(L, t.name); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

static int l_diskinfo(lua_State *L) {
    struct si_disk d;
    if (sysinfo(SI_DISK, 0, &d) < 0) return fail(L, "sysinfo failed");
    lua_newtable(L);
    lua_pushboolean(L, d.present != 0); lua_setfield(L, -2, "present");
    set_int(L, "sectors", (lua_Integer)d.sectors);
    set_int(L, "cache_hits", (lua_Integer)d.cache_hits); set_int(L, "cache_misses", (lua_Integer)d.cache_misses);
    set_int(L, "sector_reads", (lua_Integer)d.sector_reads); set_int(L, "sector_writes", (lua_Integer)d.sector_writes);
    return 1;
}

static int l_poweroff(lua_State *L) { (void)L; poweroff(); }
static int l_reboot(lua_State *L) { (void)L; reboot(); }
static int l_getpid(lua_State *L) { lua_pushinteger(L, getpid()); return 1; }
static int l_remove(lua_State *L) { if (unlink(luaL_checkstring(L, 1)) < 0) return fail(L, "cannot remove"); lua_pushboolean(L, 1); return 1; }
static int l_exists(lua_State *L) { struct stat st; lua_pushboolean(L, stat(luaL_checkstring(L, 1), &st) == 0); return 1; }

/* ---- child processes with their input and output on pipes ---- */

#define PROC_META "leos.proc"
struct lproc { int pid, in, out; };          /* in: our write end of its stdin; out: our read end of its stdout and stderr */

static struct lproc *check_proc(lua_State *L, int i) { return luaL_checkudata(L, i, PROC_META); }

static int l_spawn(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int n = lua_gettop(L);
    if (n > 30) return luaL_error(L, "too many arguments");
    char *argv[32];
    argv[0] = (char *)path;
    for (int i = 2; i <= n; i++) argv[i - 1] = (char *)luaL_checkstring(L, i);
    argv[n < 1 ? 1 : n] = 0;
    int pin[2], pout[2];
    if (pipe(pin) < 0) return fail(L, "no pipes left");
    if (pipe(pout) < 0) { close(pin[0]); close(pin[1]); return fail(L, "no pipes left"); }
    int pid = fork();
    if (pid < 0) { close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]); return fail(L, "cannot fork"); }
    if (pid == 0) {
        dup2(pin[0], 0); dup2(pout[1], 1); dup2(pout[1], 2);
        for (int fd = 3; fd < 64; fd++) close(fd);
        exec(path, argv);
        _exit(127);
    }
    close(pin[0]); close(pout[1]);
    struct lproc *p = lua_newuserdatauv(L, sizeof *p, 0);
    p->pid = pid; p->in = pin[1]; p->out = pout[0];
    luaL_setmetatable(L, PROC_META);
    return 1;
}

/* p:read() -> text that has arrived (never waits), "" if nothing yet, or nil at the end of the output */
static int p_read(lua_State *L) {
    struct lproc *p = check_proc(L, 1);
    if (p->out < 0) { lua_pushnil(L); return 1; }
    long avail = ioctl(p->out, PIPE_AVAIL, 0);
    if (avail == -2) { close(p->out); p->out = -1; lua_pushnil(L); return 1; }
    if (avail <= 0) { lua_pushliteral(L, ""); return 1; }
    char buf[4096];
    long r = read(p->out, buf, avail > (long)sizeof buf ? sizeof buf : (size_t)avail);
    if (r <= 0) { lua_pushliteral(L, ""); return 1; }
    lua_pushlstring(L, buf, (size_t)r);
    return 1;
}

static int p_write(lua_State *L) {
    struct lproc *p = check_proc(L, 1);
    size_t n;
    const char *s = luaL_checklstring(L, 2, &n);
    if (p->in < 0) return fail(L, "input is closed");
    lua_pushboolean(L, write_all(p->in, s, n) >= 0);
    return 1;
}

static int p_closeinput(lua_State *L) { struct lproc *p = check_proc(L, 1); if (p->in >= 0) { close(p->in); p->in = -1; } return 0; }
static int p_kill(lua_State *L) { struct lproc *p = check_proc(L, 1); lua_pushboolean(L, kill(p->pid) == 0); return 1; }
static int p_pid(lua_State *L) { lua_pushinteger(L, check_proc(L, 1)->pid); return 1; }

static int p_close(lua_State *L) {
    struct lproc *p = check_proc(L, 1);
    if (p->in >= 0) { close(p->in); p->in = -1; }
    if (p->out >= 0) { close(p->out); p->out = -1; }
    return 0;
}

static int l_reap(lua_State *L) {
    int status = 0;
    int pid = waitpid_nohang(&status);
    if (pid <= 0) return 0;
    lua_pushinteger(L, pid); lua_pushinteger(L, status);
    return 2;
}

static const luaL_Reg proc_methods[] = {
    {"read", p_read}, {"write", p_write}, {"closeinput", p_closeinput}, {"kill", p_kill}, {"pid", p_pid}, {"close", p_close},
    {NULL, NULL}
};

static const luaL_Reg sock_methods[] = {
    {"send", s_send}, {"recv", s_recv}, {"recvall", s_recvall}, {"sendto", s_sendto}, {"recvfrom", s_recvfrom},
    {"accept", s_accept}, {"shutdown", s_shutdown}, {"close", s_close}, {NULL, NULL}
};

static const luaL_Reg funcs[] = {
    {"resolve", l_resolve}, {"tcp", l_tcp}, {"listen", l_listen}, {"udp", l_udp}, {"ping", l_ping},
    {"dir", l_dir}, {"stat", l_stat}, {"getcwd", l_getcwd}, {"chdir", l_chdir}, {"mkdir", l_mkdir},
    {"sleep", l_sleep}, {"uptime", l_uptime}, {"netinfo", l_netinfo}, {"meminfo", l_meminfo}, {"ps", l_ps},
    {"diskinfo", l_diskinfo}, {"poweroff", l_poweroff}, {"reboot", l_reboot}, {"getpid", l_getpid}, {"remove", l_remove}, {"exists", l_exists},
    {"spawn", l_spawn}, {"reap", l_reap},
    {NULL, NULL}
};

int luaopen_leos(lua_State *L) {
    luaL_newmetatable(L, SOCK_META);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, sock_methods, 0);
    lua_pushcfunction(L, s_close); lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, s_close); lua_setfield(L, -2, "__close");
    lua_pushcfunction(L, s_tostring); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
    luaL_newmetatable(L, PROC_META);
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, proc_methods, 0);
    lua_pushcfunction(L, p_close); lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
