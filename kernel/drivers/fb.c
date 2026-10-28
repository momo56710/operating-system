#include "fb.h"
#include "string.h"

#define GLYPH_W 8
#define GLYPH_H 16

extern const uint8_t font8x16[256 * 16];

static volatile uint8_t *fb_base;
static uint64_t fb_w, fb_h, fb_pitch;
static uint32_t cols, rows, cur_x, cur_y;
static uint32_t fg, bg;
static int fb_ok;

static uint32_t pack(struct limine_framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb->red_mask_shift) |
           ((uint32_t)g << fb->green_mask_shift) |
           ((uint32_t)b << fb->blue_mask_shift);
}

static inline void put_px(uint32_t x, uint32_t y, uint32_t color) {
    *(volatile uint32_t *)(fb_base + y * fb_pitch + x * 4) = color;
}

static void draw_glyph(uint32_t col, uint32_t row, unsigned char ch, uint32_t f, uint32_t b) {
    const uint8_t *g = &font8x16[ch * GLYPH_H];
    uint32_t px = col * GLYPH_W, py = row * GLYPH_H;
    for (uint32_t y = 0; y < GLYPH_H; y++)
        for (uint32_t x = 0; x < GLYPH_W; x++)
            put_px(px + x, py + y, (g[y] & (0x80 >> x)) ? f : b);
}

static void draw_cursor(int on) {
    if (cur_x < cols && cur_y < rows)
        draw_glyph(cur_x, cur_y, '_', on ? fg : bg, bg);
}

static void clear_row(uint32_t row) {
    for (uint32_t y = 0; y < GLYPH_H; y++)
        for (uint32_t x = 0; x < cols * GLYPH_W; x++)
            put_px(x, row * GLYPH_H + y, bg);
}

static void scroll(void) {
    uint64_t row_bytes = (uint64_t)GLYPH_H * fb_pitch;
    memmove((void *)fb_base, (const void *)(fb_base + row_bytes), row_bytes * (rows - 1));
    clear_row(rows - 1);
    cur_y = rows - 1;
}

void fb_init(struct limine_framebuffer *fb) {
    if (!fb || fb->bpp != 32) return;   /* only 32-bit modes for now */
    fb_base = fb->address;
    fb_w = fb->width; fb_h = fb->height; fb_pitch = fb->pitch;
    cols = fb_w / GLYPH_W;
    rows = fb_h / GLYPH_H;
    fg = pack(fb, 0xff, 0xf4, 0xe0);   /* warm white, matches the splash */
    bg = pack(fb, 0x0d, 0x0a, 0x1c);   /* deep purple */
    for (uint32_t r = 0; r < rows; r++) clear_row(r);
    cur_x = cur_y = 0;
    fb_ok = 1;
    draw_cursor(1);
}

void fb_putc(char c) {
    if (!fb_ok) return;
    draw_cursor(0);
    switch (c) {
    case '\n': cur_x = 0; cur_y++; break;
    case '\r': cur_x = 0; break;
    case '\b': if (cur_x) cur_x--; break;
    case '\t': cur_x = (cur_x + 8) & ~7u; break;
    default:
        draw_glyph(cur_x, cur_y, (unsigned char)c, fg, bg);
        cur_x++;
    }
    if (cur_x >= cols) { cur_x = 0; cur_y++; }
    if (cur_y >= rows) scroll();
    draw_cursor(1);
}

void fb_clear(void) {
    if (!fb_ok) return;
    for (uint32_t r = 0; r < rows; r++) clear_row(r);
    cur_x = cur_y = 0;
    draw_cursor(1);
}
