#ifndef BROWSER_H
#define BROWSER_H

#include "wm.h"

extern window_t* browser_win;

void browser_init(window_t* win);
void browser_render(void);
void browser_handle_click(int mx, int my);
void browser_handle_keypress(char c);
void browser_handle_scroll(int delta);   // scroll wheel: +1 = down, -1 = up

#endif
