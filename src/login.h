#ifndef LOGIN_H
#define LOGIN_H

#include "wm.h"

void login_init(window_t* desktop_win);
int  login_is_authenticated(void);
void login_pump(void);

#endif
