#pragma once
#include <stdint.h>
#include "interrupt.h"


void pic_init(void);
void pic_mask(int irq);
void pic_unmask(int irq);
void pic_eoi(int irq);
int  pic_is_spurious(int irq);
