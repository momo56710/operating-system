#pragma once
#include "limine.h"

void fb_init(struct limine_framebuffer *fb);
void fb_putc(char c);
void fb_clear(void);
void fb_get_size(uint32_t *cols, uint32_t *rows);   /* in character cells */
