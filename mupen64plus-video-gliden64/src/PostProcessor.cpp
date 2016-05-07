#include <assert.h>

#include "N64.h"
#include "gSP.h"
#include "PostProcessor.h"
#include "FrameBuffer.h"
#include "GLSLCombiner.h"
#include "ShaderUtils.h"
#include "Config.h"

#include "Gfx_1.3.h"

#if defined(HAVE_OPENGLES_3_1)
#define SHADER_VERSION "#version 310 es \n"
#elif defined(HAVE_OPENGLES3)
#define SHADER_VERSION "#version 300 es \n"
#else
#define SHADER_VERSION "#version 100 \n"
#endif

#ifdef HAVE_OPENGLES2
#define FRAGMENT_SHADER_END "  gl_FragColor = fragColor; \n"
#else
#define FRAGMENT_SHADER_END "\n"
#endif

static const char * vertexShader =
SHADER_VERSION
"#if (__VERSION__ > 120)						\n"
"# define IN in									\n"
"# define OUT out								\n"
"#else											\n"
"# define IN attribute							\n"
"# define OUT varying							\n"
"#endif // __VERSION							\n"
"IN highp vec2 aPosition;								\n"
"IN highp vec2 aTexCoord;								\n"
"OUT mediump vec2 vTexCoord;							\n"
"void main(){                                           \n"
"gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0);\n"
"vTexCoord = aTexCoord;                                 \n"
"}                                                      \n"
;

static const char* gammaCorrectionShader =
SHADER_VERSION
"#if (__VERSION__ > 120)													\n"
"# define IN in																\n"
"# define OUT out															\n"
"# define texture2D texture													\n"
"#else																		\n"
"# define IN varying														\n"
"# define OUT																\n"
"#endif // __VERSION __														\n"
"IN mediump vec2 vTexCoord;													\n"
"uniform sampler2D Sample0;													\n"
"uniform lowp float uGammaCorrectionLevel;									\n"
"OUT lowp vec4 fragColor;													\n"
"																			\n"
"void main()																\n"
"{																			\n"
"    fragColor = texture2D(Sample0, vTexCoord);								\n"
"    fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / uGammaCorrectionLevel));	\n"
FRAGMENT_SHADER_END
"}																			\n"
;

static
GLuint _createShaderProgram(const char * _strVertex, const char * _strFragment)
{
	GLuint vertex_shader_object = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader_object, 1, &_strVertex, nullptr);
	glCompileShader(vertex_shader_object);
	assert(checkShaderCompileStatus(vertex_shader_object));

	GLuint fragment_shader_object = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader_object, 1, &_strFragment, nullptr);
	glCompileShader(fragment_shader_object);
	assert(checkShaderCompileStatus(fragment_shader_object));

	GLuint program = glCreateProgram();
	glBindAttribLocation(program, SC_POSITION, "aPosition");
	glBindAttribLocation(program, SC_TEXCOORD0, "aTexCoord");
	glAttachShader(program, vertex_shader_object);
	glAttachShader(program, fragment_shader_object);
	glLinkProgram(program);
	glDeleteShader(vertex_shader_object);
	glDeleteShader(fragment_shader_object);
	assert(checkProgramLinkStatus(program));
	return program;
}

static
CachedTexture * _createTexture()
{
	CachedTexture * pTexture = textureCache().addFrameBufferTexture();
	pTexture->format = G_IM_FMT_RGBA;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->realWidth = video().getWidth();
	pTexture->realHeight = video().getHeight();
	pTexture->textureBytes = pTexture->realWidth * pTexture->realHeight * 4;
	textureCache().addFrameBufferTextureSize(pTexture->textureBytes);
	glBindTexture(GL_TEXTURE_2D, pTexture->glName);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pTexture->realWidth, pTexture->realHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	return pTexture;
}

static
GLuint _createFBO(CachedTexture * _pTexture)
{
	GLuint FBO;
	glGenFramebuffers(1, &FBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FBO);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _pTexture->glName, 0);
	assert(checkFBO());
	return FBO;
}

void PostProcessor::_initCommon()
{
	m_pTextureOriginal = _createTexture();
	m_FBO_original = _createFBO(m_pTextureOriginal);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void PostProcessor::_initGammaCorrection()
{
	m_gammaCorrectionProgram = _createShaderProgram(vertexShader, gammaCorrectionShader);
	glUseProgram(m_gammaCorrectionProgram);
	int loc = glGetUniformLocation(m_gammaCorrectionProgram, "Sample0");
	assert(loc >= 0);
	glUniform1i(loc, 0);
	loc = glGetUniformLocation(m_gammaCorrectionProgram, "uGammaCorrectionLevel");
	assert(loc >= 0);
	const float gammaLevel = (config.gammaCorrection.force != 0) ? config.gammaCorrection.level : 2.0f;
	glUniform1f(loc, gammaLevel);
	glUseProgram(0);
}

void PostProcessor::init()
{
	_initCommon();
	_initGammaCorrection();
}

void PostProcessor::_destroyCommon()
{
	if (m_copyProgram != 0)
		glDeleteProgram(m_copyProgram);
	m_copyProgram = 0;

	if (m_FBO_original != 0)
		glDeleteFramebuffers(1, &m_FBO_original);
	m_FBO_original = 0;

	if (m_pTextureOriginal != nullptr)
		textureCache().removeFrameBufferTexture(m_pTextureOriginal);
}

void PostProcessor::_destroyGammaCorrection()
{
	if (m_gammaCorrectionProgram != 0)
		glDeleteProgram(m_gammaCorrectionProgram);
	m_gammaCorrectionProgram = 0;
}


void PostProcessor::destroy()
{
	_destroyGammaCorrection();
	_destroyCommon();
}

PostProcessor & PostProcessor::get()
{
	static PostProcessor processor;
	return processor;
}

void _setGLState() {
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	static const float vert[] =
	{
		-1.0, -1.0, +0.0, +0.0,
		+1.0, -1.0, +1.0, +0.0,
		-1.0, +1.0, +0.0, +1.0,
		+1.0, +1.0, +1.0, +1.0
	};

	glEnableVertexAttribArray(SC_POSITION);
	glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (float*)vert);
	glEnableVertexAttribArray(SC_TEXCOORD0);
	glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (float*)vert + 2);
	glDisableVertexAttribArray(SC_COLOR);
	glDisableVertexAttribArray(SC_TEXCOORD1);
	glDisableVertexAttribArray(SC_NUMLIGHTS);
	glViewport(0, 0, video().getWidth(), video().getHeight());
	gSP.changed |= CHANGED_VIEWPORT;
	gDP.changed |= CHANGED_RENDERMODE;
}

void PostProcessor::_preDraw(FrameBuffer * _pBuffer)
{
	_setGLState();
	OGLVideo & ogl = video();

#ifdef HAVE_OPENGLES2
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_FBO_original);
	textureCache().activateTexture(0, _pBuffer->m_pTexture);
	glUseProgram(m_copyProgram);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#else
	glBindFramebuffer(GL_READ_FRAMEBUFFER, _pBuffer->m_FBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_FBO_original);
	glBlitFramebuffer(
		0, 0, ogl.getWidth(), ogl.getHeight(),
		0, 0, ogl.getWidth(), ogl.getHeight(),
		GL_COLOR_BUFFER_BIT, GL_LINEAR
	);
#endif

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void PostProcessor::_postDraw()
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	video().getRender().dropRenderState();
	glUseProgram(0);
}

void PostProcessor::doGammaCorrection(FrameBuffer * _pBuffer)
{
	if (((*gfx_info.VI_STATUS_REG & 8)|config.gammaCorrection.force) == 0)
		return;

	if (_pBuffer == nullptr || (_pBuffer->m_postProcessed&PostProcessor::postEffectGammaCorrection) == PostProcessor::postEffectGammaCorrection)
		return;

	_pBuffer->m_postProcessed |= PostProcessor::postEffectGammaCorrection;

	_preDraw(_pBuffer);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _pBuffer->m_FBO);
	textureCache().activateTexture(0, m_pTextureOriginal);
	glUseProgram(m_gammaCorrectionProgram);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	_postDraw();
}
