#pragma once
#include <stdint.h>

#define IRQ_BASE 0x20   /* IRQ 0-15 become vectors 0x20-0x2F */

void pic_init(void);
void pic_mask(int irq);
void pic_unmask(int irq);
void pic_eoi(int irq);
int  pic_is_spurious(int irq);
