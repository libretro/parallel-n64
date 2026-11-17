#include <assert.h>
#include <Graphics/Parameters.h>
#include "opengl_GLInfo.h"
#include "opengl_CachedFunctions.h"
#include "opengl_Utils.h"
#include "opengl_BufferManipulationObjectFactory.h"

#ifdef OS_ANDROID
#endif

//#define ENABLE_GL_4_5

using namespace opengl;

/*---------------CreateFramebufferObject-------------*/

class GenFramebuffer : public CreateFramebufferObject
{
public:
	graphics::ObjectHandle createFramebuffer() override
	{
		GLuint fbo;
		glGenFramebuffers(1, &fbo);
		return graphics::ObjectHandle(fbo);
	}
};

class CreateFramebuffer : public CreateFramebufferObject
{
public:
	static bool Check(const GLInfo & _glinfo) {
#ifdef ENABLE_GL_4_5
		return (_glinfo.majorVersion > 4) || (_glinfo.majorVersion == 4 && _glinfo.minorVersion >= 5);
#else
		return false;
#endif
	}

	graphics::ObjectHandle createFramebuffer() override
	{
		GLuint fbo;
		glCreateFramebuffers(1, &fbo);
		return graphics::ObjectHandle(fbo);
	}
};

/*---------------CreateRenderbuffer-------------*/

class GenRenderbuffer : public CreateRenderbuffer
{
public:
	graphics::ObjectHandle createRenderbuffer() override
	{
		GLuint renderbuffer;
		glGenRenderbuffers(1, &renderbuffer);
		return graphics::ObjectHandle(renderbuffer);
	}
};


/*---------------InitRenderbuffer-------------*/

class RenderbufferStorage : public InitRenderbuffer
{
public:
	RenderbufferStorage(CachedBindRenderbuffer * _bind) : m_bind(_bind) {}
	void initRenderbuffer(const gli_init_renderbuffer_params & _params) override
	{
		m_bind->bind(_params.target, graphics::ObjectHandle(_params.handle));
		glRenderbufferStorage(GLenum(_params.target), GLenum(_params.format), _params.width, _params.height);
	}

private:
	CachedBindRenderbuffer * m_bind;
};


/*---------------AddFramebufferTarget-------------*/

class AddFramebufferTexture2D : public AddFramebufferRenderTarget
{
public:
	AddFramebufferTexture2D(CachedBindFramebuffer * _bind) : m_bind(_bind) {}

	void addFrameBufferRenderTarget(const gli_fb_render_target & _params) override
	{
		m_bind->bind(_params.bufferTarget, graphics::ObjectHandle(_params.bufferHandle));
		if (_params.textureTarget == GLI_TT_RENDERBUFFER) {
			glFramebufferRenderbuffer(GLenum(_params.bufferTarget),
				GLenum(_params.attachment),
				GLenum(_params.textureTarget),
				GLuint(_params.textureHandle));
		} else {
			glFramebufferTexture2D(GLenum(_params.bufferTarget),
				GLenum(_params.attachment),
				GLenum(_params.textureTarget),
				GLuint(_params.textureHandle),
				0);
		}
	}

private:
	CachedBindFramebuffer * m_bind;
};

class AddNamedFramebufferTexture : public AddFramebufferRenderTarget
{
public:
	static bool Check(const GLInfo & _glinfo) {
#ifdef ENABLE_GL_4_5
		return (_glinfo.majorVersion > 4) || (_glinfo.majorVersion == 4 && _glinfo.minorVersion >= 5);
#else
		return false;
#endif
	}

	void addFrameBufferRenderTarget(const gli_fb_render_target & _params) override
	{
		glNamedFramebufferTexture(GLuint(_params.bufferHandle),
			GLenum(_params.attachment),
			GLuint(_params.textureHandle),
			0);
	}
};

/*---------------CreatePixelReadBuffer-------------*/

class PBOReadBuffer : public graphics::PixelReadBuffer
{
public:
	PBOReadBuffer(CachedBindBuffer * _bind, size_t _size)
		: m_bind(_bind)
		, m_size(_size)
	{
		glGenBuffers(1, &m_PBO);
		m_bind->bind(graphics::Parameter(GL_PIXEL_PACK_BUFFER), graphics::ObjectHandle(m_PBO));
		glBufferData(GL_PIXEL_PACK_BUFFER, m_size, nullptr, GL_DYNAMIC_READ);
		m_bind->bind(graphics::Parameter(GL_PIXEL_PACK_BUFFER), graphics::ObjectHandle::null);
	}

	~PBOReadBuffer() {
		if (m_fence != 0)
			glDeleteSync(m_fence);
		glDeleteBuffers(1, &m_PBO);
		m_PBO = 0;
	}

	void readPixels(s32 _x,s32 _y, u32 _width, u32 _height, graphics::Parameter _format, graphics::Parameter _type) override
	{
		glReadPixels(_x, _y, _width, _height, GLenum(_format), GLenum(_type), 0);
		/* polled by ready(), so the read can be left in flight */
		if (m_fence != 0)
			glDeleteSync(m_fence);
		m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	}

	bool ready(bool _wait) override
	{
		if (m_fence == 0)
			return true;
		GLenum res = glClientWaitSync(m_fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
		while (_wait && res == GL_TIMEOUT_EXPIRED)
			res = glClientWaitSync(m_fence, 0, 1000000000);
		if (res == GL_TIMEOUT_EXPIRED)
			return false;
		glDeleteSync(m_fence);
		m_fence = 0;
		return true;
	}

	void * getDataRange(u32 _offset, u32 _range) override
	{
		if (_range > m_size)
			_range = static_cast<u32>(m_size);
		return glMapBufferRange(GL_PIXEL_PACK_BUFFER, _offset, _range, GL_MAP_READ_BIT);
	}

	void closeReadBuffer() override
	{
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	}

	void bind() override {
		m_bind->bind(graphics::Parameter(GL_PIXEL_PACK_BUFFER), graphics::ObjectHandle(m_PBO));
	}

	void unbind() override {
		m_bind->bind(graphics::Parameter(GL_PIXEL_PACK_BUFFER), graphics::ObjectHandle::null);
	}

private:
	CachedBindBuffer * m_bind;
	size_t m_size;
	GLuint m_PBO;
	GLsync m_fence = 0;
};

template<typename T>
class CreatePixelReadBufferT : public CreatePixelReadBuffer
{
public:
	CreatePixelReadBufferT(CachedBindBuffer * _bind)
		: m_bind(_bind) {
	}

	graphics::PixelReadBuffer * createPixelReadBuffer(size_t _sizeInBytes) override
	{
		return new T(m_bind, _sizeInBytes);
	}

private:
	CachedBindBuffer * m_bind;
};

/*---------------BlitFramebuffers-------------*/

class BlitFramebuffersImpl : public BlitFramebuffers
{
public:
	static bool Check(const GLInfo & _glinfo) {
		return !_glinfo.isGLES2;
	}

	BlitFramebuffersImpl(CachedBindFramebuffer * _bind,
		CachedEnable * _enableScissor,
		Renderer _renderer)
		: m_bind(_bind)
		, m_enableScissor(_enableScissor)
		, m_renderer(_renderer) {
	}

	bool blitFramebuffers(const gli_blit_params & _params) override
	{
		m_bind->bind(graphics::bufferTarget::READ_FRAMEBUFFER, graphics::ObjectHandle(_params.readBuffer));
		m_bind->bind(graphics::bufferTarget::DRAW_FRAMEBUFFER, graphics::ObjectHandle(_params.drawBuffer));

		const s32 adrenoCoordFix = (m_renderer == Renderer::Adreno) ? 1 : 0;

		m_enableScissor->enable(false);

		glBlitFramebuffer(
			adrenoCoordFix + _params.srcX0, _params.srcY0, _params.srcX1, _params.srcY1,
			adrenoCoordFix + _params.dstX0, _params.dstY0, _params.dstX1, _params.dstY1,
			GLbitfield(_params.mask), GLenum(_params.filter)
		);
		m_enableScissor->enable(true);

		return !Utils::isGLError();
	}

private:
	CachedBindFramebuffer * m_bind;
	CachedEnable * m_enableScissor;
	Renderer m_renderer;
};

class DummyBlitFramebuffers: public BlitFramebuffers
{
public:
	bool blitFramebuffers(const gli_blit_params & _params) override
	{
		return false;
	}
};

/*---------------BufferManipulationObjectFactory-------------*/

BufferManipulationObjectFactory::BufferManipulationObjectFactory(const GLInfo & _info,
	CachedFunctions & _cachedFunctions)
	: m_glInfo(_info)
	, m_cachedFunctions(_cachedFunctions)
{
}


BufferManipulationObjectFactory::~BufferManipulationObjectFactory()
{
}

CreateFramebufferObject * BufferManipulationObjectFactory::getCreateFramebufferObject() const
{
	if (CreateFramebuffer::Check(m_glInfo))
		return new CreateFramebuffer;

	return new GenFramebuffer;
}

CreateRenderbuffer * BufferManipulationObjectFactory::getCreateRenderbuffer() const
{
	return new GenRenderbuffer;
}

InitRenderbuffer * BufferManipulationObjectFactory::getInitRenderbuffer() const
{
	return new RenderbufferStorage(m_cachedFunctions.getCachedBindRenderbuffer());
}

AddFramebufferRenderTarget * BufferManipulationObjectFactory::getAddFramebufferRenderTarget() const
{
	if (AddNamedFramebufferTexture::Check(m_glInfo))
		return new AddNamedFramebufferTexture;

	return new AddFramebufferTexture2D(m_cachedFunctions.getCachedBindFramebuffer());
}

BlitFramebuffers * BufferManipulationObjectFactory::getBlitFramebuffers() const
{
	if (BlitFramebuffersImpl::Check(m_glInfo))
		return new BlitFramebuffersImpl(m_cachedFunctions.getCachedBindFramebuffer(),
										m_cachedFunctions.getCachedEnable(graphics::enable::SCISSOR_TEST),
										m_glInfo.renderer);

	return new DummyBlitFramebuffers;
}

CreatePixelReadBuffer * BufferManipulationObjectFactory::createPixelReadBuffer() const
{
	if (m_glInfo.isGLES2)
		return nullptr;

	return new CreatePixelReadBufferT<PBOReadBuffer>(m_cachedFunctions.getCachedBindBuffer());
}

void BufferManipulationObjectFactory::getFramebufferTextureFormats(gli_fb_formats & _formats) const
{
	if (!m_glInfo.isGLESX)
		gli_fb_formats_gl(&_formats, GLI_FB_GL, 0);
	else if (!m_glInfo.isGLES2)
		gli_fb_formats_gl(&_formats, GLI_FB_GLES3, m_glInfo.renderer == Renderer::Adreno530 ? GLI_FB_ADRENO530 : 0);
	else
		gli_fb_formats_gl(&_formats, GLI_FB_GLES2,
			(Utils::isExtensionSupported(m_glInfo, "GL_OES_depth_texture") ? GLI_FB_OES_DEPTH : 0) |
			(Utils::isExtensionSupported(m_glInfo, "GL_OES_rgb8_rgba8") ? GLI_FB_OES_RGBA8 : 0));
}
