#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void     pit_init(uint32_t hz);
uint32_t pit_get_ticks(void);
uint32_t pit_get_seconds(void);

#endif
