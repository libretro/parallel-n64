#pragma once
#include <Graphics/OpenGLContext/opengl_GLInfo.h>

namespace graphics {
	class ShaderProgram;
}

/* the context's program cache (opengl_CachedFunctions.h) */
struct gli_gl_cache1;

namespace glsl {

	class ShaderPart;

	class SpecialShadersFactory
	{
	public:
		SpecialShadersFactory(const opengl::GLInfo & _glinfo,
			gli_gl_cache1 * _useProgram,
			const ShaderPart * _vertexHeader,
			const ShaderPart * _fragmentHeader,
			const ShaderPart * _fragmentEnd);

		graphics::ShaderProgram * createShadowMapShader() const;

		graphics::TexrectDrawerShaderProgram * createTexrectDrawerDrawShader() const;

		graphics::ShaderProgram * createTexrectDrawerClearShader() const;

		graphics::ShaderProgram * createTexrectCopyShader() const;

		graphics::ShaderProgram * createGammaCorrectionShader() const;

		graphics::ShaderProgram * createFXAAShader() const;


	private:
		const opengl::GLInfo & m_glinfo;
		const ShaderPart * m_vertexHeader;
		const ShaderPart * m_fragmentHeader;
		const ShaderPart * m_fragmentEnd;
		gli_gl_cache1 * m_useProgram;
	};

}
