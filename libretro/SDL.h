#ifndef _FAKE_SDL_H_
#define _FAKE_SDL_H_

#include <stdio.h>
#include <string.h>

extern unsigned int FAKE_SDL_TICKS;

#define SDL_GetTicks() FAKE_SDL_TICKS

#endif
