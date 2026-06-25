#pragma once
#include <stdint.h>

void speaker_play(uint32_t nFrequence);
void speaker_stop(void);
void speaker_beep(uint32_t freq, uint32_t duration_ms);
