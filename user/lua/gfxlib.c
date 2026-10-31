/* The Lua "gfx" library: a back buffer in ordinary memory that Lua draws into, copied to the real screen by
 * present(). Colours are 0xRRGGBB. Text uses the kernel's 8x16 Terminus font, optionally scaled. */
#include "lua.h"
#include "lauxlib.h"
#include "libc.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "abi.h"
#include "sys.h"

extern const unsigned char font8x16[256 * 16];

static struct gfx_info gi;
static uint32_t *screen, *back;              /* the mapped framebuffer; our back buffer */
static int W, H, stride;                     /* stride in pixels */
static int opened;
static int cx0, cy0, cx1, cy1;               /* clip rectangle: [cx0,cx1) x [cy0,cy1) */
static int dx0, dy0, dx1, dy1;               /* dirty rectangle accumulated since the last present() */
static int cur_x = -100, cur_y = -100, cur_on;
static struct input_event evq[32];
static int evn, evi;

static inline uint32_t pack(uint32_t c) {
    return (((c >> 16) & 255) << gi.red_shift) | (((c >> 8) & 255) << gi.green_shift) | ((c & 255) << gi.blue_shift);
}
static inline uint32_t unpack_ch(uint32_t px, int shift) { return (px >> shift) & 255; }

static void dirty(int x0, int y0, int x1, int y1) {
    if (x0 < dx0) dx0 = x0;
    if (y0 < dy0) dy0 = y0;
    if (x1 > dx1) dx1 = x1;
    if (y1 > dy1) dy1 = y1;
}
static void clean(void) { dx0 = W; dy0 = H; dx1 = 0; dy1 = 0; }

static void fill(int x, int y, int w, int h, uint32_t px) {
    int x1 = x + w, y1 = y + h;
    if (x < cx0) x = cx0;
    if (y < cy0) y = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x >= x1 || y >= y1) return;
    for (int j = y; j < y1; j++) {
        uint32_t *row = back + j * stride;
        for (int i = x; i < x1; i++) row[i] = px;
    }
    dirty(x, y, x1, y1);
}

static inline void plot(int x, int y, uint32_t px) {
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    back[y * stride + x] = px;
    dirty(x, y, x + 1, y + 1);
}

static int need(lua_State *L) { if (!opened) luaL_error(L, "gfx.open() has not been called"); return 0; }

static int g_open(lua_State *L) {
    if (opened) { lua_pushinteger(L, W); lua_pushinteger(L, H); return 2; }
    if (syscall3(SYS_GFX, GFX_INFO, (long)&gi, 0) < 0) return luaL_error(L, "no framebuffer");
    long m = syscall3(SYS_GFX, GFX_MAP, 0, 0);
    if (m <= 0) return luaL_error(L, "cannot map the framebuffer");
    screen = (uint32_t *)m;
    W = (int)gi.width; H = (int)gi.height; stride = (int)(gi.pitch / 4);
    back = malloc((size_t)stride * H * 4);
    if (!back) return luaL_error(L, "out of memory");
    if (syscall3(SYS_GFX, GFX_ACQUIRE, 0, 0) < 0) { free(back); return luaL_error(L, "the screen is already in use"); }
    memset(back, 0, (size_t)stride * H * 4);
    cx0 = cy0 = 0; cx1 = W; cy1 = H;
    dx0 = 0; dy0 = 0; dx1 = W; dy1 = H;
    opened = 1;
    lua_pushinteger(L, W); lua_pushinteger(L, H);
    return 2;
}

static int g_close(lua_State *L) {
    (void)L;
    if (opened) { syscall3(SYS_GFX, GFX_RELEASE, 0, 0); opened = 0; free(back); back = 0; }
    return 0;
}

static int g_size(lua_State *L) { need(L); lua_pushinteger(L, W); lua_pushinteger(L, H); return 2; }

static int g_rect(lua_State *L) {
    need(L);
    fill((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
         pack((uint32_t)luaL_checkinteger(L, 5)));
    return 0;
}

static int g_clip(lua_State *L) {
    need(L);
    if (lua_isnoneornil(L, 1)) { cx0 = cy0 = 0; cx1 = W; cy1 = H; return 0; }
    int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3), h = (int)luaL_checkinteger(L, 4);
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > W ? W : x + w; cy1 = y + h > H ? H : y + h;
    return 0;
}

/* Is the point (px,py) of the corner box within the arc of radius r? ring=1: only the 1-pixel outline. */
static int in_arc(int dx, int dy, int r, int ring) {
    long d = (long)(2 * dx - 1) * (2 * dx - 1) + (long)(2 * dy - 1) * (2 * dy - 1);
    long outer = (long)(2 * r) * (2 * r), inner = (long)(2 * r - 2) * (2 * r - 2);
    return d <= outer && (!ring || d > inner);
}

/* The outline of a rounded rectangle, 1 pixel thick (frame) or the filled shape (round). */
static void rounded(int x, int y, int w, int h, int r, uint32_t px, int ring) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) {
        if (ring) { fill(x, y, w, 1, px); fill(x, y + h - 1, w, 1, px); fill(x, y, 1, h, px); fill(x + w - 1, y, 1, h, px); }
        else fill(x, y, w, h, px);
        return;
    }
    if (ring) {
        fill(x + r, y, w - 2 * r, 1, px); fill(x + r, y + h - 1, w - 2 * r, 1, px);
        fill(x, y + r, 1, h - 2 * r, px); fill(x + w - 1, y + r, 1, h - 2 * r, px);
    } else {
        fill(x, y + r, w, h - 2 * r, px);
        fill(x + r, y, w - 2 * r, r, px);
        fill(x + r, y + h - r, w - 2 * r, r, px);
    }
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            int dx = r - i, dy = r - j;                  /* distance from the corner's centre, in pixels (1..r) */
            if (!in_arc(dx, dy, r, ring)) continue;
            plot(x + i, y + j, px);
            plot(x + w - 1 - i, y + j, px);
            plot(x + i, y + h - 1 - j, px);
            plot(x + w - 1 - i, y + h - 1 - j, px);
        }
}

static int g_frame(lua_State *L) {
    need(L);
    rounded((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
            (int)luaL_optinteger(L, 6, 0), pack((uint32_t)luaL_checkinteger(L, 5)), 1);
    return 0;
}

static int g_round(lua_State *L) {
    need(L);
    rounded((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
            (int)luaL_checkinteger(L, 5), pack((uint32_t)luaL_checkinteger(L, 6)), 0);
    return 0;
}

static int g_line(lua_State *L) {
    need(L);
    int x0 = (int)luaL_checkinteger(L, 1), y0 = (int)luaL_checkinteger(L, 2);
    int x1 = (int)luaL_checkinteger(L, 3), y1 = (int)luaL_checkinteger(L, 4);
    uint32_t px = pack((uint32_t)luaL_checkinteger(L, 5));
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0, px);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return 0;
}

static int g_pixel(lua_State *L) {
    need(L);
    plot((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), pack((uint32_t)luaL_checkinteger(L, 3)));
    return 0;
}

/* ---- text ---- */

static int draw_text(int x, int y, const char *s, size_t n, uint32_t px, int scale, int draw) {
    int x0 = x;
    for (size_t k = 0; k < n; k++) {
        unsigned char ch = (unsigned char)s[k];
        if (draw && x < cx1 && x + 8 * scale > cx0 && y < cy1 && y + 16 * scale > cy0) {
            const unsigned char *g = &font8x16[ch * 16];
            for (int j = 0; j < 16; j++) {
                unsigned char bits = g[j];
                if (!bits) continue;
                for (int i = 0; i < 8; i++) {
                    if (!(bits & (0x80 >> i))) continue;
                    int px0 = x + i * scale, py0 = y + j * scale;
                    for (int v = 0; v < scale; v++) {
                        int yy = py0 + v;
                        if (yy < cy0 || yy >= cy1) continue;
                        for (int u = 0; u < scale; u++) {
                            int xx = px0 + u;
                            if (xx >= cx0 && xx < cx1) back[yy * stride + xx] = px;
                        }
                    }
                }
            }
            int gx1 = x + 8 * scale, gy1 = y + 16 * scale;
            dirty(x < cx0 ? cx0 : x, y < cy0 ? cy0 : y, gx1 > cx1 ? cx1 : gx1, gy1 > cy1 ? cy1 : gy1);
        }
        x += 8 * scale;
    }
    return x - x0;
}

static int g_text(lua_State *L) {
    need(L);
    size_t n;
    const char *s = luaL_checklstring(L, 3, &n);
    int w = draw_text((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), s, n,
                      pack((uint32_t)luaL_checkinteger(L, 4)), (int)luaL_optinteger(L, 5, 1), 1);
    lua_pushinteger(L, w);
    return 1;
}

static int g_textwidth(lua_State *L) {
    size_t n;
    luaL_checklstring(L, 1, &n);
    lua_pushinteger(L, (lua_Integer)n * 8 * luaL_optinteger(L, 2, 1));
    return 1;
}

/* ---- images: a file of width, height (32-bit each) then width*height pixels as 0xAARRGGBB ---- */

struct image { int w, h; uint32_t *px; };
#define IMG_META "leos.image"

static int g_loadimage(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    FILE *f = fopen(path, "rb");
    if (!f) { lua_pushnil(L); lua_pushfstring(L, "cannot open %s", path); return 2; }
    uint32_t hdr[2];
    if (fread(hdr, 4, 2, f) != 2 || hdr[0] == 0 || hdr[1] == 0 || hdr[0] > 4096 || hdr[1] > 4096) { fclose(f); lua_pushnil(L); lua_pushstring(L, "bad image"); return 2; }
    size_t n = (size_t)hdr[0] * hdr[1];
    uint32_t *px = malloc(n * 4);
    if (!px || fread(px, 4, n, f) != n) { free(px); fclose(f); lua_pushnil(L); lua_pushstring(L, "short image"); return 2; }
    fclose(f);
    struct image *im = lua_newuserdatauv(L, sizeof *im, 0);
    im->w = (int)hdr[0]; im->h = (int)hdr[1]; im->px = px;
    luaL_setmetatable(L, IMG_META);
    return 1;
}

static int img_gc(lua_State *L) { struct image *im = luaL_checkudata(L, 1, IMG_META); free(im->px); im->px = 0; return 0; }
static int img_size(lua_State *L) { struct image *im = luaL_checkudata(L, 1, IMG_META); lua_pushinteger(L, im->w); lua_pushinteger(L, im->h); return 2; }

/* gfx.image(img, x, y [, dw, dh [, sx, sy, sw, sh]]): draw img (scaled to dw x dh, nearest neighbour), alpha-blended. */
static int g_image(lua_State *L) {
    need(L);
    struct image *im = luaL_checkudata(L, 1, IMG_META);
    int x = (int)luaL_checkinteger(L, 2), y = (int)luaL_checkinteger(L, 3);
    int sx = (int)luaL_optinteger(L, 6, 0), sy = (int)luaL_optinteger(L, 7, 0);
    int sw = (int)luaL_optinteger(L, 8, im->w - sx), sh = (int)luaL_optinteger(L, 9, im->h - sy);
    int dw = (int)luaL_optinteger(L, 4, sw), dh = (int)luaL_optinteger(L, 5, sh);
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || sx < 0 || sy < 0 || sx + sw > im->w || sy + sh > im->h) return 0;
    int jy0 = y < cy0 ? cy0 - y : 0, jy1 = y + dh > cy1 ? cy1 - y : dh;
    int ix0 = x < cx0 ? cx0 - x : 0, ix1 = x + dw > cx1 ? cx1 - x : dw;
    for (int j = jy0; j < jy1; j++) {
        const uint32_t *src = im->px + (size_t)(sy + j * sh / dh) * im->w + sx;
        uint32_t *dst = back + (y + j) * stride + x;
        for (int i = ix0; i < ix1; i++) {
            uint32_t p = src[i * sw / dw], a = p >> 24;
            if (a == 0) continue;
            if (a == 255) { dst[i] = pack(p); continue; }
            uint32_t d = dst[i];
            uint32_t r = (((p >> 16) & 255) * a + unpack_ch(d, gi.red_shift) * (255 - a)) / 255;
            uint32_t gg = (((p >> 8) & 255) * a + unpack_ch(d, gi.green_shift) * (255 - a)) / 255;
            uint32_t b = ((p & 255) * a + unpack_ch(d, gi.blue_shift) * (255 - a)) / 255;
            dst[i] = pack((r << 16) | (gg << 8) | b);
        }
    }
    if (ix0 < ix1 && jy0 < jy1) dirty(x + ix0, y + jy0, x + ix1, y + jy1);
    return 0;
}

/* ---- the mouse pointer: drawn on the screen itself, so moving it never needs a redraw of the desktop ---- */

#define CUR_W 12
#define CUR_H 19
static const char *cursor_shape[CUR_H] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ", "X......X    ",
    "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ",
    "      X..X  ", "      X..X  ", "       XX   "
};

static void restore_rect(int x, int y, int w, int h) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < W) screen[j * stride + i] = back[j * stride + i];
    }
}

static void draw_cursor(void) {
    if (!cur_on) return;
    uint32_t white = pack(0xffffff), black = pack(0x000000);
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            char c = cursor_shape[j][i];
            int x = cur_x + i, y = cur_y + j;
            if (c == ' ' || x < 0 || y < 0 || x >= W || y >= H) continue;
            screen[y * stride + x] = c == 'X' ? black : white;
        }
}

static int g_cursor(lua_State *L) {
    need(L);
    int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    if (cur_on && x == cur_x && y == cur_y) return 0;
    if (cur_on) restore_rect(cur_x - 1, cur_y - 1, CUR_W + 2, CUR_H + 2);
    cur_x = x; cur_y = y; cur_on = 1;
    draw_cursor();
    return 0;
}

/* gfx.present(): copy what changed since the last call to the screen, then put the pointer back on top. */
static int g_present(lua_State *L) {
    need(L);
    int x0 = dx0 < 0 ? 0 : dx0, y0 = dy0 < 0 ? 0 : dy0, x1 = dx1 > W ? W : dx1, y1 = dy1 > H ? H : dy1;
    if (x0 < x1 && y0 < y1) {
        for (int j = y0; j < y1; j++) memcpy(screen + j * stride + x0, back + j * stride + x0, (size_t)(x1 - x0) * 4);
        if (cur_on && x1 > cur_x && x0 < cur_x + CUR_W && y1 > cur_y && y0 < cur_y + CUR_H) draw_cursor();
    }
    clean();
    return 0;
}

static int g_invalidate(lua_State *L) { need(L); dx0 = dy0 = 0; dx1 = W; dy1 = H; (void)L; return 0; }

/* ---- input ---- */

/* gfx.next(timeout_ms): the next keyboard or mouse event as a table, or nil if none arrived in time. */
static int g_next(lua_State *L) {
    need(L);
    int timeout = (int)luaL_optinteger(L, 1, 0);
    if (evi >= evn) {
        struct gfx_poll p = { (unsigned long)evq, 32, timeout, 0, 0 };
        evi = evn = 0;
        if (syscall3(SYS_GFX, GFX_POLL, (long)&p, 0) < 0) return 0;
        evn = (int)p.count;
    }
    if (evi >= evn) return 0;
    struct input_event *e = &evq[evi++];
    lua_createtable(L, 0, 8);
    if (e->type == EV_KEY) {
        lua_pushstring(L, "key"); lua_setfield(L, -2, "type");
        lua_pushinteger(L, e->code); lua_setfield(L, -2, "code");
        lua_pushboolean(L, e->value); lua_setfield(L, -2, "down");
        if (e->ch) { char c = (char)e->ch; lua_pushlstring(L, &c, 1); lua_setfield(L, -2, "ch"); }
    } else {
        lua_pushstring(L, "mouse"); lua_setfield(L, -2, "type");
        lua_pushinteger(L, e->wheel); lua_setfield(L, -2, "wheel");
        lua_pushinteger(L, e->buttons); lua_setfield(L, -2, "buttons");
    }
    lua_pushinteger(L, e->x); lua_setfield(L, -2, "x");
    lua_pushinteger(L, e->y); lua_setfield(L, -2, "y");
    lua_pushinteger(L, e->mods); lua_setfield(L, -2, "mods");
    return 1;
}

static int g_pending(lua_State *L) { lua_pushboolean(L, evi < evn); return 1; }

static const luaL_Reg funcs[] = {
    {"open", g_open}, {"close", g_close}, {"size", g_size}, {"rect", g_rect}, {"frame", g_frame}, {"round", g_round},
    {"line", g_line}, {"pixel", g_pixel}, {"clip", g_clip}, {"text", g_text}, {"textwidth", g_textwidth},
    {"loadimage", g_loadimage}, {"image", g_image}, {"cursor", g_cursor}, {"present", g_present},
    {"invalidate", g_invalidate}, {"next", g_next}, {"pending", g_pending},
    {NULL, NULL}
};

int luaopen_gfx(lua_State *L) {
    luaL_newmetatable(L, IMG_META);
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, img_gc); lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, img_size); lua_setfield(L, -2, "size");
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
