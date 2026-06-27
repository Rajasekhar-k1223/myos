#ifndef SB16_H
#define SB16_H

#include <stdint.h>
#include "idt.h"

// ─── Core init / IRQ ─────────────────────────────────────────────────────────
void sb16_init(void);
void sb16_handler(struct registers* regs);

// ─── Legacy double-buffer helpers (used by music.c etc.) ─────────────────────
void sb16_set_sample_rate(uint16_t hz);
void sb16_start_playback(void);
void sb16_stop_playback(void);

extern volatile int     sb16_needs_data;
extern volatile uint8_t* sb16_next_buffer;

// ─── Simple one-shot / looping PCM playback ───────────────────────────────────
// Play raw 8-bit unsigned PCM at 22050 Hz.
// loop: 1 = repeat forever, 0 = play once.
// Returns 1 on success, 0 if SB16 not present.
int  sb16_play_pcm(const uint8_t* samples, uint32_t num_samples, int loop);

// Stop current playback.
void sb16_stop(void);

// Returns 1 if currently playing.
int  sb16_is_playing(void);

#endif
