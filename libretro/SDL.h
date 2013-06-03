#ifndef _FAKE_SDL_H_
#define _FAKE_SDL_H

#define SDL_GetTicks() 100

#define SDL_CreateMutex() 0
#define SDL_DestroyMutex(x)
#define SDL_LockMutex(x)
#define SDL_UnlockMutex(x)
typedef void* SDL_mutex;

#endif