#ifndef SB16_H
#define SB16_H

#include <stdint.h>
#include "idt.h"

void sb16_init(void);
void sb16_handler(struct registers* regs);
void sb16_set_sample_rate(uint16_t hz);
void sb16_start_playback(void);
void sb16_stop_playback(void);

extern volatile int sb16_needs_data;
extern volatile uint8_t* sb16_next_buffer;

#endif
