#ifndef _FAKE_SDL_H_
#define _FAKE_SDL_H_

#include <stdio.h>
#include <string.h>

#define SDL_VERSION_ATLEAST(...) 0

typedef int SDL_Surface;

#define SDL_Quit()

#define SDL_GetTicks() 100
#define SDL_PumpEvents()
#define SDL_Delay(x)

#define SDL_CreateMutex() 0
#define SDL_DestroyMutex(x)
#define SDL_LockMutex(x)
#define SDL_UnlockMutex(x)
typedef void* SDL_mutex;

#endif