#include "fb.h"
#include "string.h"

#define GLYPH_W 8
#define GLYPH_H 16
#define MAX_COLS 256
#define MAX_ROWS 100

extern const uint8_t font8x16[256 * 16];

static volatile uint8_t *fb_base;
static uint64_t fb_w, fb_h, fb_pitch;
static uint32_t cols, rows, cur_x, cur_y;
static uint32_t fg, bg;
static int fb_ok;

/* What is on screen, so the cursor can be drawn and erased without losing the character under it. */
static uint8_t cell_ch[MAX_ROWS][MAX_COLS];
static uint8_t cell_inv[MAX_ROWS][MAX_COLS];

static int inverse;          /* current text attribute: reverse video */
static int wrap_pending;     /* the last column was just filled; wrap only when another character comes */
static int cursor_visible = 1;

enum { S_NORMAL, S_ESC, S_CSI };
static int pstate = S_NORMAL;
static int params[4], nparams;

static uint32_t pack(struct limine_framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb->red_mask_shift) |
           ((uint32_t)g << fb->green_mask_shift) |
           ((uint32_t)b << fb->blue_mask_shift);
}

static inline void put_px(uint32_t x, uint32_t y, uint32_t color) {
    *(volatile uint32_t *)(fb_base + y * fb_pitch + x * 4) = color;
}

static void draw_glyph(uint32_t col, uint32_t row, unsigned char ch, int inv) {
    const uint8_t *g = &font8x16[ch * GLYPH_H];
    uint32_t f = inv ? bg : fg, b = inv ? fg : bg;
    uint32_t px = col * GLYPH_W, py = row * GLYPH_H;
    for (uint32_t y = 0; y < GLYPH_H; y++)
        for (uint32_t x = 0; x < GLYPH_W; x++)
            put_px(px + x, py + y, (g[y] & (0x80 >> x)) ? f : b);
}

static void redraw_cell(uint32_t x, uint32_t y) {
    draw_glyph(x, y, cell_ch[y][x], cell_inv[y][x]);
}

static void put_cell(uint32_t x, uint32_t y, uint8_t ch, int inv) {
    cell_ch[y][x] = ch;
    cell_inv[y][x] = inv;
    redraw_cell(x, y);
}

/* The cursor is an underline under the cell it is on. */
static void draw_cursor(int on) {
    if (cur_x >= cols || cur_y >= rows) return;
    redraw_cell(cur_x, cur_y);
    if (on && cursor_visible) {
        uint32_t px = cur_x * GLYPH_W, py = cur_y * GLYPH_H + GLYPH_H - 2;
        uint32_t color = cell_inv[cur_y][cur_x] ? bg : fg;
        for (uint32_t y = 0; y < 2; y++)
            for (uint32_t x = 0; x < GLYPH_W; x++) put_px(px + x, py + y, color);
    }
}

/* Blank cells [x0, x1) of one row with the normal background. */
static void clear_cells(uint32_t y, uint32_t x0, uint32_t x1) {
    for (uint32_t x = x0; x < x1; x++) { cell_ch[y][x] = ' '; cell_inv[y][x] = 0; }
    for (uint32_t py = 0; py < GLYPH_H; py++)
        for (uint32_t px = x0 * GLYPH_W; px < x1 * GLYPH_W; px++)
            put_px(px, y * GLYPH_H + py, bg);
}

static void scroll(void) {
    uint64_t row_bytes = (uint64_t)GLYPH_H * fb_pitch;
    memmove((void *)fb_base, (const void *)(fb_base + row_bytes), row_bytes * (rows - 1));
    memmove(cell_ch, cell_ch[1], sizeof cell_ch[0] * (rows - 1));
    memmove(cell_inv, cell_inv[1], sizeof cell_inv[0] * (rows - 1));
    clear_cells(rows - 1, 0, cols);
    cur_y = rows - 1;
}

void fb_init(struct limine_framebuffer *fb) {
    if (!fb || fb->bpp != 32) return;   /* only 32-bit modes for now */
    fb_base = fb->address;
    fb_w = fb->width; fb_h = fb->height; fb_pitch = fb->pitch;
    cols = fb_w / GLYPH_W;
    rows = fb_h / GLYPH_H;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    fg = pack(fb, 0xff, 0xf4, 0xe0);   /* warm white, matches the splash */
    bg = pack(fb, 0x0d, 0x0a, 0x1c);   /* deep purple */
    for (uint32_t r = 0; r < rows; r++) clear_cells(r, 0, cols);
    cur_x = cur_y = 0;
    fb_ok = 1;
    draw_cursor(1);
}

void fb_get_size(uint32_t *c, uint32_t *r) {
    *c = fb_ok ? cols : 80;
    *r = fb_ok ? rows : 25;
}

void fb_clear(void) {
    if (!fb_ok) return;
    for (uint32_t r = 0; r < rows; r++) clear_cells(r, 0, cols);
    cur_x = cur_y = 0;
    wrap_pending = 0;
    draw_cursor(1);
}

/* parameter i of the current escape sequence, or `def` if it was omitted or zero */
static int param(int i, int def) { return i <= nparams && params[i] ? params[i] : def; }

static void csi_dispatch(char final) {
    wrap_pending = 0;
    switch (final) {
    case 'H': case 'f': {                               /* move to row;column (1-based) */
        int r = param(0, 1) - 1, c = param(1, 1) - 1;
        cur_y = r < (int)rows ? (uint32_t)r : rows - 1;
        cur_x = c < (int)cols ? (uint32_t)c : cols - 1;
        break; }
    case 'A': cur_y = cur_y >= (uint32_t)param(0, 1) ? cur_y - param(0, 1) : 0; break;
    case 'B': cur_y = cur_y + param(0, 1) < rows ? cur_y + param(0, 1) : rows - 1; break;
    case 'C': cur_x = cur_x + param(0, 1) < cols ? cur_x + param(0, 1) : cols - 1; break;
    case 'D': cur_x = cur_x >= (uint32_t)param(0, 1) ? cur_x - param(0, 1) : 0; break;
    case 'J':
        if (params[0] == 2) { for (uint32_t r = 0; r < rows; r++) clear_cells(r, 0, cols); cur_x = cur_y = 0; }
        else { clear_cells(cur_y, cur_x, cols); for (uint32_t r = cur_y + 1; r < rows; r++) clear_cells(r, 0, cols); }
        break;
    case 'K':
        if (params[0] == 2) clear_cells(cur_y, 0, cols);
        else if (params[0] == 1) clear_cells(cur_y, 0, cur_x + 1);
        else clear_cells(cur_y, cur_x, cols);
        break;
    case 'm':
        for (int i = 0; i <= nparams; i++) {
            if (params[i] == 0 || params[i] == 27) inverse = 0;
            else if (params[i] == 7) inverse = 1;
        }
        break;
    case 'h': case 'l':
        if (params[0] == 25) cursor_visible = final == 'h';        /* ESC[?25h / ESC[?25l */
        break;
    }
}

void fb_putc(char c) {
    if (!fb_ok) return;
    draw_cursor(0);

    if (pstate == S_ESC) {
        if (c == '[') { pstate = S_CSI; nparams = 0; params[0] = params[1] = params[2] = params[3] = 0; }
        else pstate = S_NORMAL;
    } else if (pstate == S_CSI) {
        if (c >= '0' && c <= '9') params[nparams] = params[nparams] * 10 + (c - '0');
        else if (c == ';') { if (nparams < 3) params[++nparams] = 0; }
        else if (c == '?') { }
        else { csi_dispatch(c); pstate = S_NORMAL; }
    } else if (c == 0x1b) {
        pstate = S_ESC;
    } else {
        switch (c) {
        case '\n': cur_x = 0; cur_y++; wrap_pending = 0; break;
        case '\r': cur_x = 0; wrap_pending = 0; break;
        case '\b': if (cur_x) cur_x--; wrap_pending = 0; break;
        case '\t': cur_x = (cur_x + 8) & ~7u; if (cur_x >= cols) cur_x = cols - 1; wrap_pending = 0; break;
        default:
            if (wrap_pending) { cur_x = 0; cur_y++; wrap_pending = 0; if (cur_y >= rows) scroll(); }
            put_cell(cur_x, cur_y, (unsigned char)c, inverse);
            if (cur_x + 1 >= cols) wrap_pending = 1; else cur_x++;
        }
        if (cur_y >= rows) scroll();
    }
    draw_cursor(1);
}
