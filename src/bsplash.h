#pragma once
#include <stdint.h>

/* BMP-frame splash player — plays bsplash.bin from initrd.
 * Call bsplash_tick() each render iteration.
 * Call bsplash_reset_tick() AFTER vesa_swap_buffers() to prevent cascade. */

int  bsplash_play(void);          /* load bsplash.bin and start, 0=ok -1=fail */
int  bsplash_tick(void);          /* advance frame if timer expired, returns 1 on blit */
void bsplash_reset_tick(void);    /* reset frame timer — call AFTER vesa_swap_buffers() */
int  bsplash_is_playing(void);    /* 1 while active */
void bsplash_stop(void);          /* stop and silence audio */
void bsplash_set_fps(int fps);    /* override playback FPS (0 = use file header) */
