#pragma once
#include <stdint.h>
#include "abi.h"

/* Raw input for graphical programs. While a program owns the screen (gfx_acquire), the keyboard and the
 * mouse produce events here instead of feeding the text console. */
int  input_gui_active(void);
void input_key(int code, int down, int ch, uint32_t mods);
void input_mouse(int dx, int dy, int wheel, uint32_t buttons);
int  input_pop(struct input_event *e);               /* 1 if one was available */
int  input_pending(void);

int  gfx_acquire(uint32_t pid, uint32_t screen_w, uint32_t screen_h);
void gfx_release(uint32_t pid);                      /* no-op unless this process is the owner */
uint32_t gfx_owner(void);

void mouse_init(void);
