#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#include "idt.h"

void rtl8139_init(void);
void rtl8139_handler(struct registers* regs);

#endif
