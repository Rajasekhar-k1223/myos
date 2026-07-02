#include "mixer.h"
#include "sb16.h"
#include "string.h"

// ─── State ───────────────────────────────────────────────────────────────────

static mixer_channel_t channels[MIXER_CHANNELS];

// Intermediate mix buffer: 32-bit signed accumulator per sample.
// We work in signed space (subtract 128 from each 8-bit unsigned sample,
// sum, clamp, re-add 128) to get proper DC-centred mixing.
static int32_t mix_acc[MIXER_BUF_SIZE];

// Tick counter used to rate-limit feeding SB16 (not every PIT tick needs audio).
static uint32_t tick_counter = 0;
uint8_t master_volume = 255;

void mixer_set_volume(uint8_t vol) {
    master_volume = vol;
}

// How many PIT ticks between mixer feeds.
// PIT runs at 100 Hz by default; aim for ~10 ms blocks → 1 tick per feed.
#define MIXER_FEED_INTERVAL 1

// ─── Init ─────────────────────────────────────────────────────────────────────

void mixer_init(void) {
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        channels[i].samples = 0;
        channels[i].len     = 0;
        channels[i].pos     = 0;
        channels[i].active  = 0;
        channels[i].volume  = 255;
    }
    tick_counter = 0;
}

// ─── Channel management ───────────────────────────────────────────────────────

int mixer_play(const uint8_t* samples, uint32_t len, uint8_t volume) {
    if (!samples || len == 0) return -1;

    // Find a free slot
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        if (!channels[i].active) {
            channels[i].samples = samples;
            channels[i].len     = len;
            channels[i].pos     = 0;
            channels[i].volume  = volume;
            channels[i].active  = 1;
            return i;
        }
    }
    return -1;  // All channels busy
}

void mixer_stop(int channel) {
    if (channel < 0 || channel >= MIXER_CHANNELS) return;
    channels[channel].active = 0;
    channels[channel].pos    = 0;
}

// ─── Mixing ───────────────────────────────────────────────────────────────────

// Mix MIXER_BUF_SIZE samples from all active channels into a temporary
// 8-bit output buffer, then pass it to sb16_play_pcm (one-shot, no loop)
// to queue for DMA output.
//
// For efficiency we use sb16_play_pcm with loop=0 each tick to push a fresh
// block.  Because SB16 hardware auto-init keeps running, the mixer fills the
// DMA buffer on demand via the IRQ.  In practice this means: whenever
// sb16_needs_data is set (by the IRQ handler), mixer_tick copies fresh audio
// into sb16_next_buffer directly, which is the same pattern music.c uses.

static uint8_t out_buf[MIXER_BUF_SIZE];

void mixer_tick(void) {
    tick_counter++;
    if (tick_counter < MIXER_FEED_INTERVAL) return;
    tick_counter = 0;

    // Check whether there are any active channels
    int any_active = 0;
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        if (channels[i].active) { any_active = 1; break; }
    }

    if (!any_active) {
        // Nothing to mix: if SB16 is waiting for data, feed silence
        if (sb16_needs_data && sb16_next_buffer) {
            memset((void*)sb16_next_buffer, 128, MIXER_BUF_SIZE / 2);
            sb16_needs_data = 0;
        }
        return;
    }

    // Determine how many output samples to produce.
    // We fill either what SB16 is waiting for (half the DMA buffer) or
    // MIXER_BUF_SIZE, whichever is smaller.
    uint32_t n = MIXER_BUF_SIZE;

    // Zero the accumulator
    memset(mix_acc, 0, n * sizeof(int32_t));

    // Mix each channel
    for (int ch = 0; ch < MIXER_CHANNELS; ch++) {
        mixer_channel_t* c = &channels[ch];
        if (!c->active || !c->samples) continue;

        for (uint32_t s = 0; s < n; s++) {
            if (c->pos >= c->len) {
                c->active = 0;
                break;
            }
            // Convert unsigned 8-bit (0-255, silence=128) to signed (-128..+127)
            int32_t sample = (int32_t)c->samples[c->pos] - 128;
            // Apply volume (0-255 scale)
            sample = (sample * (int32_t)c->volume) / 255;
            sample = (sample * (int32_t)master_volume) / 255;
            mix_acc[s] += sample;
            c->pos++;
        }
    }

    // Convert accumulator to unsigned 8-bit output with clamping
    for (uint32_t s = 0; s < n; s++) {
        int32_t v = mix_acc[s];
        if (v >  127) v =  127;
        if (v < -128) v = -128;
        out_buf[s] = (uint8_t)(v + 128);
    }

    // Feed to SB16
    // If the SB16 double-buffer mechanism is waiting for data, copy directly.
    if (sb16_needs_data && sb16_next_buffer) {
        // Half-buffer size the legacy API uses
        uint32_t half = n / 2;
        uint8_t* dst  = (uint8_t*)sb16_next_buffer;
        for (uint32_t s = 0; s < half; s++)
            dst[s] = out_buf[s];
        sb16_needs_data = 0;
    }

    // Also offer the full block via the simple PCM API (non-looping).
    // This lets users who call mixer_play() without also calling
    // sb16_start_playback() still get audio output.
    // We only do this if sb16 is not already playing (to avoid stomping on
    // music.c's usage of the legacy double-buffer path).
    if (!sb16_is_playing()) {
        sb16_play_pcm(out_buf, n, 0);
    }
}
