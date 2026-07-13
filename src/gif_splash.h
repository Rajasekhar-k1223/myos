#pragma once
#include <stdint.h>

/* GIF89a animated splash player.
 * Loads "splash.gif" from initrd and plays it frame-by-frame using
 * PIT-based timing derived from each frame's GCE delay field.
 * Press any key to skip. */

int  gif_splash_play(void);       /* load splash.gif and start, 0=ok -1=fail */
int  gif_splash_tick(void);       /* render next due frame. returns 1 on blit */
void gif_splash_reset_tick(void); /* call AFTER vesa_swap_buffers() */
int  gif_splash_is_playing(void); /* 1 while active */
void gif_splash_stop(void);       /* stop */
