#ifndef MUSIC_H
#define MUSIC_H

#include "wm.h"

void music_init(window_t* win);
void music_process_audio(void);
void music_handle_click(window_t* win, int mx, int my);

#endif
