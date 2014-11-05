#ifndef _GLITCH_WGL_H
#define _GLITCH_WGL_H

int isWglExtensionSupported(const char *extension);

int WGL_LookupSymbols(void);

void wgl_init(HWND hWnd);

void wgl_deinit(unsigned long fullscreen);

#endif
