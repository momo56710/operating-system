#include "libc.h"

/* nano: a small text editor in the style of GNU nano, for the LeOS console.
 *   Arrows, Home, End, PgUp, PgDn  move       Ctrl-A / Ctrl-E  start / end of line
 *   Backspace, Delete, Enter, Tab  edit       Ctrl-K / Ctrl-U  cut line / paste
 *   Ctrl-O  write the file out                Ctrl-W           search
 *   Ctrl-X  exit (asks to save)               Ctrl-G  help     Ctrl-C  show position
 * It switches the console to raw mode (every key delivered at once) and draws with ANSI escape
 * sequences, which the LeOS framebuffer console understands. */

enum { K_UP = 1001, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END, K_DEL, K_PGUP, K_PGDN, K_INS };
#define CTRL(c) ((c) & 0x1F)
#define TABW 4
#define MAX_LINES 200000

struct line { char *s; int len, cap; };

static struct line *lines;
static int nlines, lines_cap;
static int cx, cy;                 /* cursor: character index in line cy */
static int top, xoff;              /* first visible line, first visible screen column */
static int rows = 25, cols = 80, text_h;
static char filename[256];
static int modified;
static char status[200];
static char last_search[100];
static struct line *cutbuf;
static int ncut, cut_cap;
static int last_was_cut;

/* ---- output buffering: one write per screen update ---- */

static char ob[32768];
static int on;

static void flush(void) { if (on) write_all(1, ob, on); on = 0; }
static void add(const char *s, int n) {
    if (on + n > (int)sizeof ob) flush();
    if (n > (int)sizeof ob) { write_all(1, s, n); return; }
    memcpy(ob + on, s, n);
    on += n;
}
static void adds(const char *s) { add(s, (int)strlen(s)); }
static void goto_rc(int r, int c) { char b[24]; add(b, snprintf(b, sizeof b, "\x1b[%d;%dH", r, c)); }

/* ---- the text ---- */

static void line_reserve(struct line *l, int need) {
    if (need + 1 <= l->cap) return;
    int cap = l->cap * 2 > need + 1 ? l->cap * 2 : need + 1;
    if (cap < 16) cap = 16;
    l->s = realloc(l->s, cap);
    l->cap = cap;
}

static void lines_reserve(int need) {
    if (need <= lines_cap) return;
    int cap = lines_cap * 2 > need ? lines_cap * 2 : need;
    if (cap < 64) cap = 64;
    lines = realloc(lines, cap * sizeof *lines);
    lines_cap = cap;
}

static void insert_line(int at, const char *s, int len) {
    lines_reserve(nlines + 1);
    memmove(&lines[at + 1], &lines[at], (nlines - at) * sizeof *lines);
    lines[at].s = 0; lines[at].len = 0; lines[at].cap = 0;
    line_reserve(&lines[at], len);
    memcpy(lines[at].s, s, len);
    lines[at].s[len] = 0;
    lines[at].len = len;
    nlines++;
}

static void delete_line(int at) {
    free(lines[at].s);
    memmove(&lines[at], &lines[at + 1], (nlines - at - 1) * sizeof *lines);
    nlines--;
}

static int display_col(const struct line *l, int idx) {
    int col = 0;
    for (int i = 0; i < idx && i < l->len; i++) col += l->s[i] == '\t' ? TABW - col % TABW : 1;
    return col;
}

/* the character index in `l` whose screen column is closest to `col` without passing it */
static int idx_for_col(const struct line *l, int col) {
    int c = 0, i = 0;
    while (i < l->len) {
        int w = l->s[i] == '\t' ? TABW - c % TABW : 1;
        if (c + w > col) break;
        c += w; i++;
    }
    return i;
}

/* ---- file I/O ---- */

static int load_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[1024];
    long n;
    struct line cur = { 0, 0, 0 };
    line_reserve(&cur, 0);
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (long i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                if (cur.len && cur.s[cur.len - 1] == '\r') cur.len--;          /* accept DOS line endings */
                if (nlines < MAX_LINES) insert_line(nlines, cur.s, cur.len);
                cur.len = 0;
            } else {
                line_reserve(&cur, cur.len + 1);
                cur.s[cur.len++] = buf[i];
            }
        }
    }
    if (cur.len && nlines < MAX_LINES) insert_line(nlines, cur.s, cur.len);
    free(cur.s);
    close(fd);
    return 0;
}

static int save_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    char buf[2048];
    int n = 0, ok = 1;
    for (int i = 0; i < nlines && ok; i++) {
        for (int j = 0; j <= lines[i].len; j++) {
            if (n == (int)sizeof buf) { ok = write_all(fd, buf, n) >= 0; n = 0; if (!ok) break; }
            buf[n++] = j < lines[i].len ? lines[i].s[j] : '\n';
        }
    }
    if (ok && n) ok = write_all(fd, buf, n) >= 0;
    close(fd);
    return ok ? 0 : -1;
}

/* ---- screen ---- */

static int getkey(void) {
    unsigned char c;
    if (read(0, &c, 1) <= 0) return -1;
    if (c != 0x1b) return c;
    unsigned char a, b;
    if (read(0, &a, 1) <= 0 || a != '[' || read(0, &b, 1) <= 0) return 0x1b;
    if (b >= '0' && b <= '9') {
        unsigned char t;
        read(0, &t, 1);                                    /* the closing '~' */
        return b == '3' ? K_DEL : b == '5' ? K_PGUP : b == '6' ? K_PGDN : b == '2' ? K_INS : 0;
    }
    return b == 'A' ? K_UP : b == 'B' ? K_DOWN : b == 'C' ? K_RIGHT : b == 'D' ? K_LEFT :
           b == 'H' ? K_HOME : b == 'F' ? K_END : 0;
}

static void scroll_to_cursor(void) {
    if (cy < top) top = cy;
    if (cy >= top + text_h) top = cy - text_h + 1;
    int dc = display_col(&lines[cy], cx);
    if (dc < xoff) xoff = dc;
    if (dc >= xoff + cols) xoff = dc - cols + 1;
}

static void draw_text_row(int idx) {
    if (idx >= nlines) return;
    const struct line *l = &lines[idx];
    int col = 0;
    for (int i = 0; i < l->len && col < xoff + cols; i++) {
        unsigned char c = (unsigned char)l->s[i];
        int w = c == '\t' ? TABW - col % TABW : 1;
        for (int k = 0; k < w; k++, col++)
            if (col >= xoff && col < xoff + cols) { char ch = c == '\t' ? ' ' : (c < 32 || c > 126) ? '?' : (char)c; add(&ch, 1); }
    }
}

static void shortcut(const char *key, const char *label, int *used) {
    int need = (int)(strlen(key) + 1 + strlen(label)) + 2;
    if (*used + need > cols - 1) return;                   /* never touch the last cell of the last row */
    adds("\x1b[7m"); adds(key); adds("\x1b[0m"); adds(" "); adds(label); adds("  ");
    *used += need;
}

static void draw_screen(void) {
    scroll_to_cursor();
    adds("\x1b[?25l");
    goto_rc(1, 1);
    char title[300];
    int tl = snprintf(title, sizeof title, "  LeOS nano 1.0    File: %s%s", filename[0] ? filename : "(new buffer)", modified ? "   [Modified]" : "");
    adds("\x1b[7m");
    for (int i = 0; i < cols; i++) add(i < tl ? &title[i] : " ", 1);
    adds("\x1b[0m");
    for (int r = 0; r < text_h; r++) {
        goto_rc(r + 2, 1);
        draw_text_row(top + r);
        adds("\x1b[K");
    }
    goto_rc(rows - 2, 1);
    if (status[0]) {                                       /* centred message in reverse video, like nano */
        int sl = (int)strlen(status), pad = (cols - sl) / 2;
        adds("\x1b[K");
        goto_rc(rows - 2, pad > 0 ? pad + 1 : 1);
        adds("\x1b[7m"); adds(status); adds("\x1b[0m");
    } else {
        adds("\x1b[K");
    }
    int used = 0;
    goto_rc(rows - 1, 1); adds("\x1b[K");
    shortcut("^G", "Help", &used); shortcut("^O", "Write Out", &used); shortcut("^W", "Where Is", &used);
    shortcut("^K", "Cut Line", &used); shortcut("^C", "Cur Pos", &used);
    used = 0;
    goto_rc(rows, 1); adds("\x1b[K");
    shortcut("^X", "Exit", &used); shortcut("^U", "Paste", &used); shortcut("^A", "Line Start", &used);
    shortcut("^E", "Line End", &used); shortcut("^L", "Refresh", &used);
    goto_rc(cy - top + 2, display_col(&lines[cy], cx) - xoff + 1);
    adds("\x1b[?25h");
    flush();
}

/* Read a line of text on the message row. Returns 1 on Enter, 0 if cancelled with Ctrl-C. */
static int prompt(const char *label, char *buf, int max) {
    int n = (int)strlen(buf);
    for (;;) {
        adds("\x1b[?25l");
        goto_rc(rows - 2, 1); adds("\x1b[K");
        adds("\x1b[7m"); adds(label); adds("\x1b[0m "); adds(buf);
        adds("\x1b[?25h");
        flush();
        int k = getkey();
        if (k == '\n' || k == CTRL('M')) return 1;
        if (k == CTRL('C') || k == -1) return 0;
        if ((k == 8 || k == 127) && n) buf[--n] = 0;
        else if (k >= 32 && k < 127 && n < max - 1) { buf[n++] = (char)k; buf[n] = 0; }
    }
}

/* ---- commands ---- */

static void insert_char(int c) {
    struct line *l = &lines[cy];
    line_reserve(l, l->len + 1);
    memmove(l->s + cx + 1, l->s + cx, l->len - cx);
    l->s[cx++] = (char)c;
    l->s[++l->len] = 0;
    modified = 1;
}

static void split_line(void) {
    char *rest = lines[cy].s + cx;
    int rl = lines[cy].len - cx;
    insert_line(cy + 1, rest, rl);                         /* may move `lines`, so index again afterwards */
    lines[cy].len = cx;
    lines[cy].s[cx] = 0;
    cy++; cx = 0;
    modified = 1;
}

static void join_with_next(void) {                         /* append line cy+1 to line cy */
    struct line *a = &lines[cy], *b = &lines[cy + 1];
    line_reserve(a, a->len + b->len);
    memcpy(a->s + a->len, b->s, b->len);
    a->len += b->len;
    a->s[a->len] = 0;
    delete_line(cy + 1);
    modified = 1;
}

static void backspace(void) {
    if (cx > 0) {
        struct line *l = &lines[cy];
        memmove(l->s + cx - 1, l->s + cx, l->len - cx + 1);
        l->len--; cx--;
        modified = 1;
    } else if (cy > 0) {
        cy--; cx = lines[cy].len;
        join_with_next();
    }
}

static void delete_char(void) {
    struct line *l = &lines[cy];
    if (cx < l->len) { memmove(l->s + cx, l->s + cx + 1, l->len - cx); l->len--; modified = 1; }
    else if (cy < nlines - 1) join_with_next();
}

static void cut_line(void) {
    if (!last_was_cut) {                                   /* a new cut replaces the buffer; repeated cuts add up */
        for (int i = 0; i < ncut; i++) free(cutbuf[i].s);
        ncut = 0;
    }
    if (ncut == cut_cap) { cut_cap = cut_cap ? cut_cap * 2 : 8; cutbuf = realloc(cutbuf, cut_cap * sizeof *cutbuf); }
    cutbuf[ncut].len = lines[cy].len;
    cutbuf[ncut].cap = lines[cy].len + 1;
    cutbuf[ncut].s = malloc(lines[cy].len + 1);
    memcpy(cutbuf[ncut].s, lines[cy].s, lines[cy].len + 1);
    ncut++;
    if (nlines == 1) { lines[0].len = 0; lines[0].s[0] = 0; }
    else { delete_line(cy); if (cy >= nlines) cy = nlines - 1; }
    cx = 0;
    modified = 1;
}

static void paste(void) {
    if (!ncut) { snprintf(status, sizeof status, "[ Cut buffer is empty ]"); return; }
    for (int i = 0; i < ncut; i++) insert_line(cy + i, cutbuf[i].s, cutbuf[i].len);
    cy += ncut;
    if (cy >= nlines) cy = nlines - 1;
    cx = 0;
    modified = 1;
}

static int write_out(void) {                               /* 1 if saved */
    char name[256];
    strncpy(name, filename, sizeof name - 1);
    name[sizeof name - 1] = 0;
    if (!prompt("File Name to Write:", name, sizeof name) || !name[0]) { snprintf(status, sizeof status, "[ Cancelled ]"); return 0; }
    if (save_file(name) < 0) { snprintf(status, sizeof status, "[ Error writing %s ]", name); return 0; }
    strcpy(filename, name);
    modified = 0;
    snprintf(status, sizeof status, "[ Wrote %d line%s to disk ]", nlines, nlines == 1 ? "" : "s");
    return 1;
}

static void search(void) {
    char pat[100];
    pat[0] = 0;
    if (!prompt("Search:", pat, sizeof pat)) { snprintf(status, sizeof status, "[ Cancelled ]"); return; }
    if (!pat[0]) strcpy(pat, last_search);
    if (!pat[0]) return;
    strcpy(last_search, pat);
    for (int n = 0; n <= nlines; n++) {                    /* from the cursor to the end, wrapping around */
        int i = (cy + n) % nlines;
        int from = n == 0 ? cx + 1 : 0;
        if (from > lines[i].len) continue;
        char *hit = strstr(lines[i].s + from, pat);
        if (hit) {
            cy = i; cx = (int)(hit - lines[i].s);
            if (n == nlines || (cy < top)) { }
            return;
        }
    }
    snprintf(status, sizeof status, "[ \"%s\" not found ]", pat);
}

static void help_screen(void) {
    static const char *text[] = {
        "LeOS nano: keys", "",
        "  Arrows, Home, End, PgUp, PgDn   move the cursor",
        "  Ctrl-A / Ctrl-E                  start / end of the line",
        "  Backspace, Delete, Enter, Tab    edit text",
        "  Ctrl-K                           cut the current line (repeat to cut several)",
        "  Ctrl-U                           paste the cut lines",
        "  Ctrl-W                           search (Enter alone repeats the last search)",
        "  Ctrl-O                           write the file out (asks for a name)",
        "  Ctrl-X                           exit, asking to save if there are changes",
        "  Ctrl-C                           show the cursor position",
        "  Ctrl-L                           redraw the screen", "",
        "Press any key to return.", 0 };
    adds("\x1b[2J");
    for (int i = 0; text[i]; i++) { goto_rc(i + 2, 2); adds(text[i]); }
    flush();
    getkey();
}

int main(int argc, char **argv) {
    long size = ioctl(0, TTY_SIZE, 0);
    if (size < 0) { dprintf(2, "nano: standard input is not the console\n"); return 1; }
    rows = (int)(size >> 16); cols = (int)(size & 0xFFFF);
    if (rows < 8) rows = 25;
    if (cols < 40) cols = 80;
    text_h = rows - 4;

    if (argc > 1) strncpy(filename, argv[1], sizeof filename - 1);
    if (filename[0]) {
        if (load_file(filename) == 0) snprintf(status, sizeof status, "[ Read %d line%s ]", nlines, nlines == 1 ? "" : "s");
        else snprintf(status, sizeof status, "[ New File ]");
    }
    if (!nlines) insert_line(0, "", 0);

    ioctl(0, TTY_RAW, 1);
    adds("\x1b[2J");
    for (;;) {
        draw_screen();
        int k = getkey();
        status[0] = 0;
        int cut = 0;
        struct line *l = &lines[cy];
        if (k == -1) break;
        switch (k) {
        case K_UP:    if (cy > 0) { int dc = display_col(l, cx); cy--; cx = idx_for_col(&lines[cy], dc); } break;
        case K_DOWN:  if (cy < nlines - 1) { int dc = display_col(l, cx); cy++; cx = idx_for_col(&lines[cy], dc); } break;
        case K_LEFT:  if (cx > 0) cx--; else if (cy > 0) { cy--; cx = lines[cy].len; } break;
        case K_RIGHT: if (cx < l->len) cx++; else if (cy < nlines - 1) { cy++; cx = 0; } break;
        case K_HOME: case CTRL('A'): cx = 0; break;
        case K_END:  case CTRL('E'): cx = l->len; break;
        case K_PGUP:  cy = cy - text_h > 0 ? cy - text_h : 0; if (cx > lines[cy].len) cx = lines[cy].len; break;
        case K_PGDN:  cy = cy + text_h < nlines ? cy + text_h : nlines - 1; if (cx > lines[cy].len) cx = lines[cy].len; break;
        case K_DEL: case CTRL('D'): delete_char(); break;
        case 8: case 127: backspace(); break;
        case '\n': case CTRL('M'): split_line(); break;
        case '\t': insert_char('\t'); break;
        case CTRL('K'): cut_line(); cut = 1; break;
        case CTRL('U'): paste(); break;
        case CTRL('O'): write_out(); break;
        case CTRL('W'): search(); break;
        case CTRL('G'): help_screen(); break;
        case CTRL('L'): adds("\x1b[2J"); break;
        case CTRL('C'):
            snprintf(status, sizeof status, "[ line %d/%d, col %d ]", cy + 1, nlines, display_col(l, cx) + 1);
            break;
        case CTRL('X'):
            if (!modified) goto done;
            for (;;) {
                goto_rc(rows - 2, 1); adds("\x1b[K\x1b[7mSave modified buffer?  (Y)es  (N)o  ^C Cancel\x1b[0m"); flush();
                int a = getkey();
                if (a == 'y' || a == 'Y') { if (write_out()) goto done; break; }
                if (a == 'n' || a == 'N') goto done;
                if (a == CTRL('C') || a == -1) { snprintf(status, sizeof status, "[ Cancelled ]"); break; }
            }
            break;
        default:
            if (k >= 32 && k < 127) insert_char(k);
        }
        last_was_cut = cut;
    }
done:
    ioctl(0, TTY_RAW, 0);
    adds("\x1b[2J");
    flush();
    return 0;
}
