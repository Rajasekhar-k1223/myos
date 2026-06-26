#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#include "idt.h"

void rtl8139_init(void);
void rtl8139_handler(struct registers* regs);
void rtl8139_send_packet(uint8_t* payload, uint32_t length);
uint8_t* rtl8139_get_mac(void);

#endif
