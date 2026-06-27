#pragma once
#include <stdint.h>

typedef struct {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    const uint8_t* data;
    uint32_t data_len;
} wav_info_t;

/* Parse a WAV file in memory. Returns 0 on success, -1 on error. */
int wav_parse(const uint8_t* file_data, uint32_t file_len, wav_info_t* info);

/* Play parsed WAV using sb16 or ac97 (whichever is available).
 * Returns 1 on success, 0 if no audio hardware available. */
int wav_play(const wav_info_t* info);
