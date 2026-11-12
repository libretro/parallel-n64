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
	bool toggleFullscreen;
	bool resizeWindow;
	bool fullscreen;
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

/* DisplayWindowGpu.cpp */
void gli_window_gpu_start(void);
void gli_window_gpu_stop(void);
void gli_drawer_init_data(void);
void gli_drawer_destroy_data(void);
bool gli_drawer_is_drawing(void);
void gli_gl_reset_error(void);

#ifdef __cplusplus
}

#include "GraphicsDrawer.h"

class DisplayWindow
{
public:
	void swapBuffers() { gli_window_swap_buffers(); }
	void updateScale();

	f32 getScaleX() const { return g_window.scaleX; }
	f32 getScaleY() const { return g_window.scaleY; }
	f32 getAdjustScale() const { return g_window.adjustScale; }
	u32 getBuffersSwapCount() const { return g_window.buffersSwapCount; }
	u32 getWidth() const { return g_window.width; }
	u32 getHeight() const { return g_window.height; }
	u32 getScreenWidth() const { return g_window.screenWidth; }
	u32 getScreenHeight() const { return g_window.screenHeight; }
	u32 getHeightOffset() const { return g_window.heightOffset; }
	bool isFullscreen() const { return g_window.fullscreen; }
	bool isAdjustScreen() const { return g_window.adjustScreen; }

	GraphicsDrawer & getDrawer() { return m_drawer; }

	static DisplayWindow & get();

private:
	GraphicsDrawer m_drawer;
};

inline
DisplayWindow & dwnd()
{
	return DisplayWindow::get();
}
#endif

#endif /* DISPLAYWINDOW_H */
