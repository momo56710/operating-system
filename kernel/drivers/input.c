#include "input.h"
#include "spinlock.h"
#include "fb.h"
#include "string.h"

#define EVQ 256

static struct input_event q[EVQ];
static unsigned head, tail;
static spinlock_t lock;
static uint32_t owner;                    /* pid of the program that owns the screen; 0 = nobody */
static int mx, my, sw = 1280, sh = 800;
static uint32_t cur_buttons;

int input_gui_active(void) { return owner != 0; }
uint32_t gfx_owner(void) { return owner; }

static void push(const struct input_event *e) {       /* lock held */
    unsigned next = (head + 1) % EVQ;
    if (next == tail) return;                          /* full: drop */
    q[head] = *e;
    head = next;
}

void input_key(int code, int down, int ch, uint32_t mods) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = EV_KEY; e.code = code; e.value = down; e.ch = ch; e.mods = mods;
    e.x = mx; e.y = my; e.buttons = cur_buttons;
    uint64_t f = spin_lock_irqsave(&lock);
    push(&e);
    spin_unlock_irqrestore(&lock, f);
}

void input_mouse(int dx, int dy, int wheel, uint32_t buttons) {
    uint64_t f = spin_lock_irqsave(&lock);
    mx += dx; my += dy;
    if (mx < 0) mx = 0;
    if (mx >= sw) mx = sw - 1;
    if (my < 0) my = 0;
    if (my >= sh) my = sh - 1;
    uint32_t prev = cur_buttons;
    cur_buttons = buttons;
    if (!owner) { spin_unlock_irqrestore(&lock, f); return; }        /* track the pointer, but nobody is listening */

    if (head != tail && !wheel && buttons == prev) {                 /* coalesce a run of pure movement into one event */
        struct input_event *last = &q[(head + EVQ - 1) % EVQ];
        if (last->type == EV_MOUSE && !last->wheel && last->buttons == buttons) {
            last->x = mx; last->y = my;
            spin_unlock_irqrestore(&lock, f);
            return;
        }
    }
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = EV_MOUSE; e.x = mx; e.y = my; e.wheel = wheel; e.buttons = buttons;
    push(&e);
    spin_unlock_irqrestore(&lock, f);
}

int input_pop(struct input_event *e) {
    uint64_t f = spin_lock_irqsave(&lock);
    int ok = head != tail;
    if (ok) { *e = q[tail]; tail = (tail + 1) % EVQ; }
    spin_unlock_irqrestore(&lock, f);
    return ok;
}

int input_pending(void) { return head != tail; }

int gfx_acquire(uint32_t pid, uint32_t screen_w, uint32_t screen_h) {
    uint64_t f = spin_lock_irqsave(&lock);
    if (owner && owner != pid) { spin_unlock_irqrestore(&lock, f); return -1; }   /* someone else has it */
    owner = pid;
    sw = (int)screen_w; sh = (int)screen_h;
    mx = sw / 2; my = sh / 2;
    head = tail = 0;
    spin_unlock_irqrestore(&lock, f);
    fb_set_gui(1);
    return 0;
}

void gfx_release(uint32_t pid) {
    uint64_t f = spin_lock_irqsave(&lock);
    if (owner != pid) { spin_unlock_irqrestore(&lock, f); return; }
    owner = 0;
    head = tail = 0;
    spin_unlock_irqrestore(&lock, f);
    fb_set_gui(0);
}
