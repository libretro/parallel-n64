#ifndef OPENGL_H
#define OPENGL_H

#include <stdint.h>

#include <vector>

#include <glsm/glsmsym.h>

#if defined(HAVE_OPENGLES2) || defined(GLIDEN64ES)

#undef  GL_DRAW_FRAMEBUFFER
#undef  GL_READ_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER RARCH_GL_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER RARCH_GL_FRAMEBUFFER
#define GLESX

#elif defined(HAVE_OPENGLES3)

#define GLESX
#define GL_UNIFORMBLOCK_SUPPORT

#elif defined(HAVE_OPENGLES_3_1)

#define GLESX
#define GL_IMAGE_TEXTURES_SUPPORT
#define GL_MULTISAMPLING_SUPPORT
#define GL_UNIFORMBLOCK_SUPPORT

#else
#define GL_IMAGE_TEXTURES_SUPPORT
#define GL_MULTISAMPLING_SUPPORT
#define GL_UNIFORMBLOCK_SUPPORT

#endif

#ifdef GLESX
#define GET_PROGRAM_BINARY_EXTENSION "GL_OES_get_program_binary"
#else
#define GET_PROGRAM_BINARY_EXTENSION "GL_ARB_get_program_binary"
#endif

#ifndef GL_EXT_texture_filter_anisotropic
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#include "gSP.h"

#define INDEXMAP_SIZE 80U
#define VERTBUFF_SIZE 256U
#define ELEMBUFF_SIZE 1024U

class OGLRender
{
public:
	void addTriangle(int _v0, int _v1, int _v2);
	void drawTriangles();
	void drawLLETriangle(uint32_t _numVtx);
	void drawDMATriangles(uint32_t _numVtx);
	void drawLine(int _v0, int _v1, float _width);
	void drawRect(int _ulx, int _uly, int _lrx, int _lry, float * _pColor);
	struct TexturedRectParams
	{
		float ulx, uly, lrx, lry;
		float uls, ult, lrs, lrt;
		bool flip;
		TexturedRectParams(float _ulx, float _uly, float _lrx, float _lry, float _uls, float _ult, float _lrs, float _lrt, bool _flip) :
			ulx(_ulx), uly(_uly), lrx(_lrx), lry(_lry), uls(_uls), ult(_ult), lrs(_lrs), lrt(_lrt), flip(_flip)
		{}
	};
	void drawTexturedRect(const TexturedRectParams & _params);
	void drawText(const char *_pText, float x, float y);
	void clearDepthBuffer(uint32_t _uly, uint32_t _lry);
	void clearColorBuffer( float * _pColor );

	int getTrianglesCount() const {return triangles.num;}
	bool isClipped(int32_t _v0, int32_t _v1, int32_t _v2) const
	{
		return (triangles.vertices[_v0].clip & triangles.vertices[_v1].clip & triangles.vertices[_v2].clip) != 0;
	}
	bool isImageTexturesSupported() const {return m_bImageTexture;}
	SPVertex & getVertex(uint32_t _v) {return triangles.vertices[_v];}
	void setDMAVerticesSize(uint32_t _size) { if (triangles.dmaVertices.size() < _size) triangles.dmaVertices.resize(_size); }
	SPVertex * getDMAVerticesData() { return triangles.dmaVertices.data(); }
	void updateScissor(FrameBuffer * _pBuffer) const;

	enum RENDER_STATE {
		rsNone = 0,
		rsTriangle = 1,
		rsRect = 2,
		rsTexRect = 3,
		rsLine = 4
	};
	RENDER_STATE getRenderState() const {return m_renderState;}

	enum OGL_RENDERER {
		glrOther,
		glrAdreno
	};
	OGL_RENDERER getRenderer() const { return m_oglRenderer; }

	void dropRenderState() {m_renderState = rsNone;}

private:
	OGLRender() : m_oglRenderer(glrOther), m_bImageTexture(false), m_bFlatColors(false) {}
	OGLRender(const OGLRender &);
	friend class OGLVideo;

	void _initExtensions();
	void _initStates();
	void _initData();
	void _destroyData();

	void _setSpecialTexrect() const;

	void _setColorArray() const;
	void _setTexCoordArrays() const;
	void _setBlendMode() const;
	void _updateCullFace() const;
	void _updateViewport() const;
	void _updateDepthUpdate() const;
	void _updateStates(RENDER_STATE _renderState) const;
	void _prepareDrawTriangle(bool _dma);
	bool _canDraw() const;

	struct {
		SPVertex vertices[VERTBUFF_SIZE];
		std::vector<SPVertex> dmaVertices;
		GLubyte elements[ELEMBUFF_SIZE];
		int num;
		uint32_t indexmap[INDEXMAP_SIZE];
		uint32_t indexmapinv[VERTBUFF_SIZE];
		uint32_t indexmap_prev;
		uint32_t indexmap_nomap;
	} triangles;

	struct GLVertex
	{
		float x, y, z, w;
		float s0, t0, s1, t1;
	};

	RENDER_STATE m_renderState;
	OGL_RENDERER m_oglRenderer;
	GLVertex m_rect[4];
	bool m_bImageTexture;
	bool m_bFlatColors;
};

class OGLVideo
{
public:
	void start();
	void stop();
	void restart();
	void swapBuffers();
	bool changeWindow();
	bool resizeWindow();
	void setWindowSize(uint32_t _width, uint32_t _height);
	void setToggleFullscreen() {m_bToggleFullscreen = true;}
	void readScreen(void **_pDest, long *_pWidth, long *_pHeight );
	void readScreen2(void * _dest, int * _width, int * _height, int _front);

	void updateScale();
	float getScaleX() const {return m_scaleX;}
	float getScaleY() const {return m_scaleY;}
	float getAdjustScale() const {return m_adjustScale;}
	uint32_t getBuffersSwapCount() const {return m_buffersSwapCount;}
	uint32_t getWidth() const { return m_width; }
	uint32_t getHeight() const {return m_height;}
	uint32_t getScreenWidth() const {return m_screenWidth;}
	uint32_t getScreenHeight() const {return m_screenHeight;}
	uint32_t getHeightOffset() const {return m_heightOffset;}
	bool isFullscreen() const {return m_bFullscreen;}
	bool isAdjustScreen() const {return m_bAdjustScreen;}
	bool isResizeWindow() const {return m_bResizeWindow;}

	OGLRender & getRender() {return m_render;}

	static OGLVideo & get();
	static bool isExtensionSupported(const char * extension);

protected:
	OGLVideo() :
		m_bToggleFullscreen(false), m_bResizeWindow(false), m_bFullscreen(false), m_bAdjustScreen(false),
		m_width(0), m_height(0), m_heightOffset(0),
		m_screenWidth(0), m_screenHeight(0), m_resizeWidth(0), m_resizeHeight(0),
		m_scaleX(0), m_scaleY(0), m_adjustScale(0)
	{}

	void _setBufferSize();

	bool m_bToggleFullscreen;
	bool m_bResizeWindow;
	bool m_bFullscreen;
	bool m_bAdjustScreen;

	uint32_t m_buffersSwapCount;
	uint32_t m_width, m_height, m_heightOffset;
	uint32_t m_screenWidth, m_screenHeight;
	uint32_t m_resizeWidth, m_resizeHeight;
	float m_scaleX, m_scaleY;
	float m_adjustScale;

private:
	OGLRender m_render;

	virtual bool _start() = 0;
	virtual void _stop() = 0;
	virtual void _swapBuffers() = 0;
	virtual void _changeWindow() = 0;
	virtual bool _resizeWindow() = 0;
};

struct FBOTextureFormats
{
	GLint colorInternalFormat;
	GLenum colorFormat;
	GLenum colorType;
	uint32_t colorFormatBytes;

	GLint monochromeInternalFormat;
	GLenum monochromeFormat;
	GLenum monochromeType;
	uint32_t monochromeFormatBytes;

	GLint depthInternalFormat;
	GLenum depthFormat;
	GLenum depthType;
	uint32_t depthFormatBytes;

	GLint depthImageInternalFormat;
	GLenum depthImageFormat;
	GLenum depthImageType;
	uint32_t depthImageFormatBytes;

	GLint lutInternalFormat;
	GLenum lutFormat;
	GLenum lutType;
	uint32_t lutFormatBytes;

	void init();
};

extern FBOTextureFormats fboFormats;

inline
OGLVideo & video()
{
	return OGLVideo::get();
}

class TextureFilterHandler
{
public:
	TextureFilterHandler() : m_inited(0), m_options(0) {}
	// It's not safe to call shutdown() in destructor, because texture filter has its own static objects, which can be destroyed first.
	~TextureFilterHandler() { m_inited = m_options = 0; }
	void init();
	void shutdown();
	bool isInited() const { return m_inited != 0; }
	bool optionsChanged() const { return _getConfigOptions() != m_options; }
private:
	uint32_t _getConfigOptions() const;
	uint32_t m_inited;
	uint32_t m_options;
};

extern TextureFilterHandler TFH;

void initGLFunctions();
bool checkFBO();
bool isGLError();

void displayLoadProgress(const wchar_t *format, ...);

#endif
