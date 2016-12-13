#ifndef POST_PROCESSOR_H
#define POST_PROCESSOR_H

#include <stdint.h>

#include "OpenGL.h"
#include "Textures.h"

class PostProcessor {
public:
	void init();
	void destroy();

	void doGammaCorrection(FrameBuffer * _pBuffer);

	static PostProcessor & get();

	static const uint32_t postEffectGammaCorrection = 2U;

private:
	PostProcessor() :
		m_copyProgram(0), m_gammaCorrectionProgram(0),
		m_FBO_original(0),
		m_pTextureOriginal(NULL) {}
	PostProcessor(const PostProcessor & _other);
	void _initCommon();
	void _destroyCommon();
	void _initGammaCorrection();
	void _destroyGammaCorrection();
	void _preDraw(FrameBuffer * _pBuffer);
	void _postDraw();

	GLuint m_extractBloomProgram;
	GLuint m_seperableBlurProgram;
	GLuint m_copyProgram;

	GLuint m_gammaCorrectionProgram;

	GLuint m_FBO_original;

	CachedTexture * m_pTextureOriginal;
};

#endif // POST_PROCESSOR_H
