#ifndef MINOS_APIC_H
#define MINOS_APIC_H

#include <stdint.h>

/* Initialize the BSP local APIC and its periodic timer. */
int apic_timer_init(uint32_t frequency);
void apic_eoi(void);
void pic_unmask_irq(uint8_t irq);
void pic_eoi(uint8_t irq);
uint64_t timer_ticks(void);
uint32_t timer_frequency(void);

#endif
