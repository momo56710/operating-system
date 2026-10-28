#pragma once
#include <stdint.h>

void     apic_init(void);                  /* Local APIC + I/O APICs; uses ACPI MADT when present */
void     irq_enable(int isa_irq);          /* route an ISA IRQ (0-15) to its vector 0x20+irq */
void     irq_eoi(void);                    /* end of interrupt */
void     lapic_timer_start(unsigned hz);   /* calibrate against the PIT, then tick at hz */
