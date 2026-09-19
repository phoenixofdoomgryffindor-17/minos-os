#ifndef MINOS_APIC_H
#define MINOS_APIC_H

#include <stdint.h>

/* Initialize the BSP local APIC and its periodic timer. */
int apic_timer_init(uint32_t frequency);
void apic_eoi(void);
uint64_t timer_ticks(void);
uint32_t timer_frequency(void);

#endif
