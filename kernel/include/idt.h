#pragma once
#include <stdint.h>

void idt_init(void);
void idt_set_present(int vector, int present);
