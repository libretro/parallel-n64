#ifndef DISPLAYWINDOW_H
#define DISPLAYWINDOW_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The output window: its size, the image's size in it and the scales */
typedef struct gli_window
{
	bool resizeWindow;
	bool adjustScreen;

	u32 buffersSwapCount;
	u32 width;
	u32 height;
	u32 heightOffset;
	u32 screenWidth;
	u32 screenHeight;
	u32 resizeWidth;
	u32 resizeHeight;
	f32 scaleX;
	f32 scaleY;
	f32 adjustScale;
} gli_window;

extern gli_window g_window;

void gli_window_swap_buffers(void);
void gli_window_set_size_now(u32 _width, u32 _height);

/* DisplayWindowGpu.c; the drawer's in GraphicsDrawer.c */
void gli_window_gpu_start(void);
void gli_window_gpu_stop(void);
void gli_drawer_init_data(void);
void gli_drawer_destroy_data(void);
bool gli_drawer_is_drawing(void);
/* whether the drawer draws triangles (not rects or lines) */
bool gli_drawer_is_triangle(void);
void gli_gl_reset_error(void);

#ifdef __cplusplus
}

#include "GraphicsDrawer.h"
#endif

#endif /* DISPLAYWINDOW_H */
