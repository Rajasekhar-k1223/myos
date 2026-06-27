#pragma once
#include <stdint.h>

int  ac97_init(void);        /* returns 1 if found, 0 otherwise */
int  ac97_play_pcm(const uint8_t* samples, uint32_t num_samples); /* 8-bit unsigned, 22050 Hz mono */
void ac97_stop(void);
int  ac97_is_playing(void);
void ac97_set_volume(uint8_t vol); /* 0-255 */
