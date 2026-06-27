#pragma once
#include <stdint.h>

#define MIXER_CHANNELS 4
#define MIXER_BUF_SIZE 4096

typedef struct {
    const uint8_t* samples;
    uint32_t       len;
    uint32_t       pos;
    int            active;
    uint8_t        volume;  // 0-255
} mixer_channel_t;

// Initialise the mixer (call after sb16_init).
void mixer_init(void);

// Start playing samples on any free channel.
// Returns the channel index (0..MIXER_CHANNELS-1), or -1 if all busy.
int  mixer_play(const uint8_t* samples, uint32_t len, uint8_t volume);

// Stop a specific channel.
void mixer_stop(int channel);

// Mix active channels and feed the result to SB16.
// Call from the PIT tick handler or a periodic callback.
void mixer_tick(void);
