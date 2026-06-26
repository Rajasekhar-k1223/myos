#ifndef SB16_H
#define SB16_H

#include <stdint.h>
#include "idt.h"

void sb16_init(void);
void sb16_handler(struct registers* regs);

#endif
