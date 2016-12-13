#include <stdio.h>

#include "../OpenGL.h"
#include "../gDP.h"
#include "../Config.h"
#include "../Log.h"

class OGLVideoMupenPlus : public OGLVideo
{
public:
	OGLVideoMupenPlus() {}

private:
	void _setAttributes();
	void _getDisplaySize();

	virtual bool _start();
	virtual void _stop();
	virtual void _swapBuffers();
	virtual bool _resizeWindow();
	virtual void _changeWindow();
};

OGLVideo & OGLVideo::get()
{
	static OGLVideoMupenPlus video;
	return video;
}

void OGLVideoMupenPlus::_setAttributes()
{
}

bool OGLVideoMupenPlus::_start()
{
	//CoreVideo_Init();
	_setAttributes();

	m_bFullscreen = 1;
	m_screenWidth = 640;
	m_screenHeight = 480;
	_getDisplaySize();
	_setBufferSize();

	LOG(LOG_VERBOSE, "[gles2GlideN64]: Create setting videomode %dx%d\n", m_screenWidth, m_screenHeight);

	return true;
}

void OGLVideoMupenPlus::_stop()
{
	//CoreVideo_Quit();
}

void OGLVideoMupenPlus::_swapBuffers()
{
#if 0
	// if emulator defined a render callback function, call it before buffer swap
	if (renderCallback != NULL)
   {
		glUseProgram(0);
		if (config.frameBufferEmulation.N64DepthCompare == 0) {
			glViewport(0, getHeightOffset(), getScreenWidth(), getScreenHeight());
			gSP.changed |= CHANGED_VIEWPORT;
		}
		gDP.changed |= CHANGED_COMBINE;
		(*renderCallback)((gDP.changed&CHANGED_CPU_FB_WRITE) == 0 ? 1 : 0);
	}
#endif

	//CoreVideo_GL_SwapBuffers();
}

bool OGLVideoMupenPlus::_resizeWindow()
{
	_setAttributes();

	m_bFullscreen = false;
	m_width = m_screenWidth = m_resizeWidth;
	m_height = m_screenHeight = m_resizeHeight;

	_setBufferSize();
	isGLError(); // reset GL error.
	return true;
}

void OGLVideoMupenPlus::_changeWindow()
{
	//CoreVideo_ToggleFullScreen();
}

void OGLVideoMupenPlus::_getDisplaySize()
{
}
