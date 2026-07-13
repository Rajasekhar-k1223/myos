#pragma once
#include <stdint.h>

/* Load and start playing an .els file from the initrd.
   Video is scaled to current screen resolution automatically.
   Returns 0 on success, -1 if file not found or invalid. */
int  els_play(const char* filename);

/* Like els_play() but loops indefinitely until els_stop(). */
int  els_play_loop(const char* filename);

/* Advance one video frame after els_hold render iterations.
   Call exactly once per vesa_swap_buffers() call. Returns 1 if a new
   frame was blitted, 0 if still holding the current frame. */
int  els_tick(void);

/* 1 while video is still playing. */
int  els_is_playing(void);

/* Stop playback and silence audio. */
void els_stop(void);

/* Set how many render iterations each video frame is held for.
   hold=1 → every swap advances a frame (fastest).
   hold=2 → each frame shown twice (half-speed).
   Default: 1. */
void els_set_hold(int hold);

/* Approximate fps-to-hold mapping. Kept for API compatibility.
   Prefer els_set_hold() for precise control. */
void els_set_fps(int fps);

/* No-op. Kept for API compatibility (timing no longer PIT-based). */
void els_reset_tick(void);
