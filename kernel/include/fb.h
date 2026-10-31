#pragma once
#include "limine.h"
#include "abi.h"

void fb_init(struct limine_framebuffer *fb);
void fb_putc(char c);
void fb_clear(void);
void fb_get_size(uint32_t *cols, uint32_t *rows);   /* in character cells */
int  fb_get_info(struct gfx_info *info, uint64_t *phys);   /* 0 ok, -1 no framebuffer */
void fb_set_gui(int on);        /* while on, the kernel's text console does not draw (a program owns the screen) */
