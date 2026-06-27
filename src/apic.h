#ifndef APIC_H
#define APIC_H

#include <stdint.h>

void apic_init(void);
void apic_send_init(uint8_t apic_id);
void apic_send_sipi(uint8_t apic_id, uint8_t vector);
uint8_t apic_get_id(void);
void apic_timer_init(void);
void apic_eoi(void);

#endif
