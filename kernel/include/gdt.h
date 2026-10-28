#pragma once
#include <stdint.h>

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_DS   0x1B   /* 0x18 | RPL 3 */
#define USER_CS   0x23   /* 0x20 | RPL 3 */

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);
