#include <string>
#include <assert.h>
#include <Config.h>
#include <Graphics/Parameters.h>
#include "opengl_ContextImpl.h"
#include <Graphics/ContextOps.h>

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

/* the parameters' numbers (gli_params.h) are GL's */
static_assert(GLI_CF_RED_GREEN_BLUE == GL_RGB, "GLI_CF_RED_GREEN_BLUE");
static_assert(GLI_CF_RGBA == GL_RGBA, "GLI_CF_RGBA");
static_assert(GLI_CF_RG == GL_RG, "GLI_CF_RG");
static_assert(GLI_CF_RED == GL_RED, "GLI_CF_RED");
static_assert(GLI_CF_DEPTH == GL_DEPTH_COMPONENT, "GLI_CF_DEPTH");
static_assert(GLI_ICF_RGB8 == GL_RGB8, "GLI_ICF_RGB8");
static_assert(GLI_ICF_RGBA8 == GL_RGBA8, "GLI_ICF_RGBA8");
static_assert(GLI_ICF_RGBA4 == GL_RGBA4, "GLI_ICF_RGBA4");
static_assert(GLI_ICF_RGB5_A1 == GL_RGB5_A1, "GLI_ICF_RGB5_A1");
static_assert(GLI_ICF_RG == GL_RG8, "GLI_ICF_RG");
static_assert(GLI_ICF_R16F == GL_R16F, "GLI_ICF_R16F");
static_assert(GLI_ICF_DEPTH == GL_DEPTH_COMPONENT24, "GLI_ICF_DEPTH");
static_assert(GLI_ICF_RG32F == GL_RG32F, "GLI_ICF_RG32F");
static_assert(GLI_CF_RED_INTEGER == GL_RED_INTEGER, "GLI_CF_RED_INTEGER");
static_assert(GLI_ICF_RGB == GL_RGB, "GLI_ICF_RGB");
static_assert(GLI_ICF_RGBA == GL_RGBA, "GLI_ICF_RGBA");
static_assert(GLI_ICF_R8 == GL_R8, "GLI_ICF_R8");
static_assert(GLI_ICF_R16UI == GL_R16UI, "GLI_ICF_R16UI");
static_assert(GLI_ICF_R32F == GL_R32F, "GLI_ICF_R32F");
static_assert(GLI_ICF_RGBA32F == GL_RGBA32F, "GLI_ICF_RGBA32F");
static_assert(GLI_ICF_DEPTH_COMPONENT == GL_DEPTH_COMPONENT, "GLI_ICF_DEPTH_COMPONENT");
static_assert(GLI_ICF_DEPTH16 == GL_DEPTH_COMPONENT16, "GLI_ICF_DEPTH16");
static_assert(GLI_DT_UNSIGNED_BYTE == GL_UNSIGNED_BYTE, "GLI_DT_UNSIGNED_BYTE");
static_assert(GLI_DT_UNSIGNED_SHORT == GL_UNSIGNED_SHORT, "GLI_DT_UNSIGNED_SHORT");
static_assert(GLI_DT_UNSIGNED_INT == GL_UNSIGNED_INT, "GLI_DT_UNSIGNED_INT");
static_assert(GLI_DT_FLOAT == GL_FLOAT, "GLI_DT_FLOAT");
static_assert(GLI_DT_HALF_FLOAT == GL_HALF_FLOAT, "GLI_DT_HALF_FLOAT");
static_assert(GLI_DT_UNSIGNED_SHORT_5_6_5 == GL_UNSIGNED_SHORT_5_6_5, "GLI_DT_UNSIGNED_SHORT_5_6_5");
static_assert(GLI_DT_UNSIGNED_SHORT_5_5_5_1 == GL_UNSIGNED_SHORT_5_5_5_1, "GLI_DT_UNSIGNED_SHORT_5_5_5_1");
static_assert(GLI_DT_UNSIGNED_SHORT_4_4_4_4 == GL_UNSIGNED_SHORT_4_4_4_4, "GLI_DT_UNSIGNED_SHORT_4_4_4_4");
static_assert(GLI_TT_TEXTURE_2D == GL_TEXTURE_2D, "GLI_TT_TEXTURE_2D");
static_assert(GLI_TT_TEXTURE_2D_MULTISAMPLE == GL_TEXTURE_2D_MULTISAMPLE, "GLI_TT_TEXTURE_2D_MULTISAMPLE");
static_assert(GLI_TT_TEXTURE_EXTERNAL == GL_TEXTURE_EXTERNAL_OES, "GLI_TT_TEXTURE_EXTERNAL");
static_assert(GLI_TT_RENDERBUFFER == GL_RENDERBUFFER, "GLI_TT_RENDERBUFFER");
static_assert(GLI_BT_FRAMEBUFFER == GL_FRAMEBUFFER, "GLI_BT_FRAMEBUFFER");
static_assert(GLI_BT_DRAW_FRAMEBUFFER == GL_DRAW_FRAMEBUFFER, "GLI_BT_DRAW_FRAMEBUFFER");
static_assert(GLI_BT_READ_FRAMEBUFFER == GL_READ_FRAMEBUFFER, "GLI_BT_READ_FRAMEBUFFER");
static_assert(GLI_BA_COLOR_ATTACHMENT0 == GL_COLOR_ATTACHMENT0, "GLI_BA_COLOR_ATTACHMENT0");
static_assert(GLI_BA_COLOR_ATTACHMENT1 == GL_COLOR_ATTACHMENT1, "GLI_BA_COLOR_ATTACHMENT1");
static_assert(GLI_BA_COLOR_ATTACHMENT2 == GL_COLOR_ATTACHMENT2, "GLI_BA_COLOR_ATTACHMENT2");
static_assert(GLI_BA_DEPTH_ATTACHMENT == GL_DEPTH_ATTACHMENT, "GLI_BA_DEPTH_ATTACHMENT");
static_assert(GLI_EN_BLEND == GL_BLEND, "GLI_EN_BLEND");
static_assert(GLI_EN_CULL_FACE == GL_CULL_FACE, "GLI_EN_CULL_FACE");
static_assert(GLI_EN_DEPTH_TEST == GL_DEPTH_TEST, "GLI_EN_DEPTH_TEST");
static_assert(GLI_EN_DEPTH_CLAMP == GL_DEPTH_CLAMP, "GLI_EN_DEPTH_CLAMP");
static_assert(GLI_EN_CLIP_DISTANCE0 == GL_CLIP_DISTANCE0, "GLI_EN_CLIP_DISTANCE0");
static_assert(GLI_EN_DITHER == GL_DITHER, "GLI_EN_DITHER");
static_assert(GLI_EN_POLYGON_OFFSET_FILL == GL_POLYGON_OFFSET_FILL, "GLI_EN_POLYGON_OFFSET_FILL");
static_assert(GLI_EN_SCISSOR_TEST == GL_SCISSOR_TEST, "GLI_EN_SCISSOR_TEST");
static_assert(GLI_IA_READ_ONLY == GL_READ_ONLY, "GLI_IA_READ_ONLY");
static_assert(GLI_IA_WRITE_ONLY == GL_WRITE_ONLY, "GLI_IA_WRITE_ONLY");
static_assert(GLI_IA_READ_WRITE == GL_READ_WRITE, "GLI_IA_READ_WRITE");
static_assert(GLI_TP_FILTER_NEAREST == GL_NEAREST, "GLI_TP_FILTER_NEAREST");
static_assert(GLI_TP_FILTER_LINEAR == GL_LINEAR, "GLI_TP_FILTER_LINEAR");
static_assert(GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST == GL_NEAREST_MIPMAP_NEAREST, "GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST");
static_assert(GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST == GL_LINEAR_MIPMAP_NEAREST, "GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST");
static_assert(GLI_TP_WRAP_CLAMP_TO_EDGE == GL_CLAMP_TO_EDGE, "GLI_TP_WRAP_CLAMP_TO_EDGE");
static_assert(GLI_TP_WRAP_REPEAT == GL_REPEAT, "GLI_TP_WRAP_REPEAT");
static_assert(GLI_TP_WRAP_MIRRORED_REPEAT == GL_MIRRORED_REPEAT, "GLI_TP_WRAP_MIRRORED_REPEAT");
static_assert(GLI_CULLMODE_FRONT == GL_BACK, "GLI_CULLMODE_FRONT");
static_assert(GLI_CULLMODE_BACK == GL_FRONT, "GLI_CULLMODE_BACK");
static_assert(GLI_CULLMODE_FRONT_AND_BACK == GL_FRONT_AND_BACK, "GLI_CULLMODE_FRONT_AND_BACK");
static_assert(GLI_COMPARE_LEQUAL == GL_LEQUAL, "GLI_COMPARE_LEQUAL");
static_assert(GLI_COMPARE_LESS == GL_LESS, "GLI_COMPARE_LESS");
static_assert(GLI_COMPARE_ALWAYS == GL_ALWAYS, "GLI_COMPARE_ALWAYS");
static_assert(GLI_BLEND_ZERO == GL_ZERO, "GLI_BLEND_ZERO");
static_assert(GLI_BLEND_ONE == GL_ONE, "GLI_BLEND_ONE");
static_assert(GLI_BLEND_SRC_ALPHA == GL_SRC_ALPHA, "GLI_BLEND_SRC_ALPHA");
static_assert(GLI_BLEND_DST_ALPHA == GL_DST_ALPHA, "GLI_BLEND_DST_ALPHA");
static_assert(GLI_BLEND_ONE_MINUS_SRC_ALPHA == GL_ONE_MINUS_SRC_ALPHA, "GLI_BLEND_ONE_MINUS_SRC_ALPHA");
static_assert(GLI_BLEND_CONSTANT_ALPHA == GL_CONSTANT_ALPHA, "GLI_BLEND_CONSTANT_ALPHA");
static_assert(GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA == GL_ONE_MINUS_CONSTANT_ALPHA, "GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA");
static_assert(GLI_BLEND_SRC1_COLOR == GL_SRC1_COLOR, "GLI_BLEND_SRC1_COLOR");
static_assert(GLI_BLEND_ONE_MINUS_SRC1_COLOR == GL_ONE_MINUS_SRC1_COLOR, "GLI_BLEND_ONE_MINUS_SRC1_COLOR");
static_assert(GLI_BLEND_SRC1_ALPHA == GL_SRC1_ALPHA, "GLI_BLEND_SRC1_ALPHA");
static_assert(GLI_BLEND_ONE_MINUS_SRC1_ALPHA == GL_ONE_MINUS_SRC1_ALPHA, "GLI_BLEND_ONE_MINUS_SRC1_ALPHA");
static_assert(GLI_DM_TRIANGLES == GL_TRIANGLES, "GLI_DM_TRIANGLES");
static_assert(GLI_DM_TRIANGLE_STRIP == GL_TRIANGLE_STRIP, "GLI_DM_TRIANGLE_STRIP");
static_assert(GLI_DM_LINES == GL_LINES, "GLI_DM_LINES");
static_assert(GLI_BM_COLOR_BUFFER == GL_COLOR_BUFFER_BIT, "GLI_BM_COLOR_BUFFER");
static_assert(GLI_BM_DEPTH_BUFFER == GL_DEPTH_BUFFER_BIT, "GLI_BM_DEPTH_BUFFER");
static_assert(GLI_BM_STENCIL_BUFFER == GL_STENCIL_BUFFER_BIT, "GLI_BM_STENCIL_BUFFER");

namespace graphics {
	namespace bufferTarget {
		BufferTargetParam DRAW_FRAMEBUFFER(GLI_BT_DRAW_FRAMEBUFFER);
		BufferTargetParam READ_FRAMEBUFFER(GLI_BT_READ_FRAMEBUFFER);
	}
}
#include "opengl_BufferedDrawer.h"
#include "opengl_UnbufferedDrawer.h"
#include "opengl_ColorBufferReader.h"
#ifdef OS_ANDROID
#endif
#include "opengl_Utils.h"
#include "GLSL/glsl_CombinerProgramBuilder.h"
#include "GLSL/glsl_SpecialShadersFactory.h"
#include "GLSL/glsl_ShaderStorage.h"

/* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT is part of GL_EXT_texture_filter_
 * anisotropic. The per-texture parameter constant
 * GL_TEXTURE_MAX_ANISOTROPY_EXT (0x84FE) gets a similar fallback
 * #define in opengl_TextureManipulationObjectFactory.cpp; we need
 * the maximum-supported-anisotropy query constant (0x84FF) here.
 */
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifdef OS_ANDROID
#endif

#include "glsm/glsm.h"

using namespace opengl;

ContextImpl::ContextImpl()
	: m_clampMode(graphics::ClampMode::ClippingEnabled)
{

}


ContextImpl::~ContextImpl()
{
}

void ContextImpl::init()
{
	m_clampMode = graphics::ClampMode::ClippingEnabled;
	m_glInfo.init();

	if (m_glInfo.isGLES2) {
		// Correct buffer target parameters, since GLES2 knows only GL_FRAMEBUFFER
		graphics::bufferTarget::DRAW_FRAMEBUFFER = graphics::bufferTarget::FRAMEBUFFER;
		graphics::bufferTarget::READ_FRAMEBUFFER = graphics::bufferTarget::FRAMEBUFFER;
		gli_ctx.drawFramebuffer = gli_ctx.readFramebuffer = GLI_BT_FRAMEBUFFER;
	}

	if (!m_cachedFunctions)
		m_cachedFunctions.reset(new CachedFunctions(m_glInfo));

	{
		TextureManipulationObjectFactory textureObjectsFactory(m_glInfo, *m_cachedFunctions.get());
		m_createTexture.reset(textureObjectsFactory.getCreate2DTexture());
		m_init2DTexture.reset(textureObjectsFactory.getInit2DTexture());
		m_update2DTexture.reset(textureObjectsFactory.getUpdate2DTexture());
		m_set2DTextureParameters.reset(textureObjectsFactory.getSet2DTextureParameters());
	}

	{
		BufferManipulationObjectFactory bufferObjectFactory(m_glInfo, *m_cachedFunctions.get());
		{
			static const gli_fb_formats unset = GLI_FB_FORMATS_DEFAULT;
			m_fbTexFormats = unset;
		}
		bufferObjectFactory.getFramebufferTextureFormats(m_fbTexFormats);
		m_createFramebuffer.reset(bufferObjectFactory.getCreateFramebufferObject());
		m_createRenderbuffer.reset(bufferObjectFactory.getCreateRenderbuffer());
		m_initRenderbuffer.reset(bufferObjectFactory.getInitRenderbuffer());
		m_addFramebufferRenderTarget.reset(bufferObjectFactory.getAddFramebufferRenderTarget());
		m_createPixelReadBuffer.reset(bufferObjectFactory.createPixelReadBuffer());
		m_blitFramebuffers.reset(bufferObjectFactory.getBlitFramebuffers());
	}

	{
		if ((m_glInfo.isGLESX && (m_glInfo.bufferStorage && m_glInfo.majorVersion * 10 + m_glInfo.minorVersion >= 32)) || !m_glInfo.isGLESX)
			m_graphicsDrawer.reset(new BufferedDrawer(m_glInfo, m_cachedFunctions->getCachedVertexAttribArray(), m_cachedFunctions->getCachedBindBuffer()));
		else
			m_graphicsDrawer.reset(new UnbufferedDrawer(m_glInfo, m_cachedFunctions->getCachedVertexAttribArray()));
	}

	resetCombinerProgramBuilder();
}

void ContextImpl::destroy()
{
	m_createTexture.reset();
	m_init2DTexture.reset();
	m_set2DTextureParameters.reset();

	m_createFramebuffer.reset();
	m_createRenderbuffer.reset();
	m_initRenderbuffer.reset();
	m_addFramebufferRenderTarget.reset();
	m_graphicsDrawer.reset();
	m_combinerProgramBuilder.reset();

	m_cachedFunctions.reset();
}

void ContextImpl::setClampMode(graphics::ClampMode _mode)
{
	if (!m_glInfo.isGLESX) {
		switch (_mode) {
		case graphics::ClampMode::ClippingEnabled:
			m_cachedFunctions->getCachedEnable(graphics::enable::DEPTH_CLAMP)->enable(false);
			m_cachedFunctions->getCachedEnable(graphics::enable::CLIP_DISTANCE0)->enable(false);
			break;
		case graphics::ClampMode::NoNearPlaneClipping:
			m_cachedFunctions->getCachedEnable(graphics::enable::DEPTH_CLAMP)->enable(true);
			m_cachedFunctions->getCachedEnable(graphics::enable::CLIP_DISTANCE0)->enable(true);
			break;
		case graphics::ClampMode::NoClipping:
			m_cachedFunctions->getCachedEnable(graphics::enable::DEPTH_CLAMP)->enable(true);
			m_cachedFunctions->getCachedEnable(graphics::enable::CLIP_DISTANCE0)->enable(false);
			break;
		}
	}
	m_clampMode = _mode;
}

graphics::ClampMode ContextImpl::getClampMode()
{
	return m_clampMode;
}

void ContextImpl::enable(graphics::EnableParam _parameter, bool _enable)
{
	m_cachedFunctions->getCachedEnable(_parameter)->enable(_enable);
}

u32 ContextImpl::isEnabled(graphics::EnableParam _parameter)
{
	return m_cachedFunctions->getCachedEnable(_parameter)->get();
}

void ContextImpl::cullFace(graphics::CullModeParam _mode)
{
	m_cachedFunctions->getCachedCullFace()->setCullFace(_mode);
}

void ContextImpl::enableDepthWrite(bool _enable)
{
	m_cachedFunctions->getCachedDepthMask()->setDepthMask(_enable);
}

void ContextImpl::setDepthCompare(graphics::CompareParam _mode)
{
	m_cachedFunctions->getCachedDepthCompare()->setDepthCompare(_mode);
}

void ContextImpl::setViewport(s32 _x, s32 _y, s32 _width, s32 _height)
{
	m_cachedFunctions->getCachedViewport()->setViewport(_x, _y, _width, _height);
}

void ContextImpl::setScissor(s32 _x, s32 _y, s32 _width, s32 _height)
{
	m_cachedFunctions->getCachedScissor()->setScissor(_x, _y, _width, _height);
}

void ContextImpl::setBlending(graphics::BlendParam _sfactor, graphics::BlendParam _dfactor)
{
	m_cachedFunctions->getCachedBlending()->setBlending(_sfactor, _dfactor);
}

void ContextImpl::setBlendingSeparate(graphics::BlendParam _sfactorcolor, graphics::BlendParam _dfactorcolor, graphics::BlendParam _sfactoralpha, graphics::BlendParam _dfactoralpha)
{
	m_cachedFunctions->getCachedBlendingSeparate()->setBlendingSeparate(_sfactorcolor, _dfactorcolor, _sfactoralpha, _dfactoralpha);
}

void ContextImpl::setBlendColor(f32 _red, f32 _green, f32 _blue, f32 _alpha)
{
	m_cachedFunctions->getCachedBlendColor()->setBlendColor(_red, _green, _blue, _alpha);
}

void ContextImpl::clearColorBuffer(f32 _red, f32 _green, f32 _blue, f32 _alpha)
{
	CachedEnable * enableScissor = m_cachedFunctions->getCachedEnable(graphics::enable::SCISSOR_TEST);
	enableScissor->enable(false);

	if (m_glInfo.isGLES2) {
		m_cachedFunctions->getCachedClearColor()->setClearColor(_red, _green, _blue, _alpha);
		glClear(GL_COLOR_BUFFER_BIT);
	} else {
		GLfloat values[4] = {_red, _green, _blue, _alpha};
		glClearBufferfv(GL_COLOR, 0, values);
	}

	enableScissor->enable(true);
}

void ContextImpl::clearDepthBuffer()
{
	CachedEnable * enableScissor = m_cachedFunctions->getCachedEnable(graphics::enable::SCISSOR_TEST);
	CachedDepthMask * depthMask = m_cachedFunctions->getCachedDepthMask();
	enableScissor->enable(false);

	if (m_glInfo.renderer == Renderer::PowerVR) {
		depthMask->setDepthMask(false);
		glClear(GL_DEPTH_BUFFER_BIT);
	}

	depthMask->setDepthMask(true);
	glClear(GL_DEPTH_BUFFER_BIT);

	enableScissor->enable(true);
}

void ContextImpl::setPolygonOffset(f32 _factor, f32 _units)
{
	glPolygonOffset(_factor, _units);
}

/*---------------Texture-------------*/

graphics::ObjectHandle ContextImpl::createTexture(graphics::Parameter _target)
{
	return m_createTexture->createTexture(_target);
}

void ContextImpl::deleteTexture(graphics::ObjectHandle _name)
{
	u32 glName(_name);
	glDeleteTextures(1, &glName);
	m_init2DTexture->reset(_name);

	m_cachedFunctions->getTexParams()->erase(u32(_name));
}

void ContextImpl::init2DTexture(const gli_init_texture_params & _params)
{
	m_init2DTexture->init2DTexture(_params);
}

void ContextImpl::update2DTexture(const gli_update_texture_params & _params)
{
	m_update2DTexture->update2DTexture(_params);
}

void ContextImpl::setTextureParameters(const gli_tex_parameters & _parameters)
{
	m_set2DTextureParameters->setTextureParameters(_parameters);
}

void ContextImpl::bindTexture(const gli_bind_texture_params & _params) {
	m_cachedFunctions->getCachedBindTexture()->bind(_params.textureUnitIndex, _params.target, graphics::ObjectHandle(_params.texture));
}

void ContextImpl::setTextureUnpackAlignment(s32 _param)
{
	m_cachedFunctions->getCachedTextureUnpackAlignment()->setTextureUnpackAlignment(_param);
}

s32 ContextImpl::getTextureUnpackAlignment() const
{
	GLint unpackAlignment;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
	return unpackAlignment;
}

s32 ContextImpl::getMaxTextureSize() const
{
	GLint maxTextureSize;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	return maxTextureSize;
}

f32 ContextImpl::getMaxAnisotropy() const
{
	GLfloat maxAniso = 0.0f;
	if (m_glInfo.anisotropic_filtering)
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	return maxAniso;
}

void ContextImpl::bindImageTexture(const gli_bind_image_texture_params & _params)
{
	glBindImageTexture(GLuint(_params.imageUnit), GLuint(_params.texture), 0, GL_FALSE, 0, GLenum(_params.accessMode), GLenum(_params.textureFormat));
}

u32 ContextImpl::convertInternalTextureFormat(u32 _format) const
{
	if (!m_glInfo.isGLES2)
		return _format;

	switch (_format) {
	case GL_RGB8:
		return GL_RGB;
	case GL_RGBA8:
	case GL_RGBA4:
	case GL_RGB5_A1:
		return GL_RGBA;
	}

	return _format;
}

void ContextImpl::textureBarrier()
{
	if (m_glInfo.texture_barrier)
		glTextureBarrier();
	else if (m_glInfo.texture_barrierNV)
		glTextureBarrierNV();
}

/*---------------Framebuffer-------------*/

void ContextImpl::getFramebufferTextureFormats(gli_fb_formats & _formats)
{
	_formats = m_fbTexFormats;
}

graphics::ObjectHandle ContextImpl::createFramebuffer()
{
	return m_createFramebuffer->createFramebuffer();
}

void ContextImpl::deleteFramebuffer(graphics::ObjectHandle _name)
{
	u32 fbo(_name);
	if (fbo != 0) {
		glDeleteFramebuffers(1, &fbo);
		m_cachedFunctions->getCachedBindFramebuffer()->reset();
	}
}

void ContextImpl::bindFramebuffer(graphics::BufferTargetParam _target, graphics::ObjectHandle _name)
{
	if (m_glInfo.renderer == Renderer::VideoCore) {
		CachedDepthMask * depthMask = m_cachedFunctions->getCachedDepthMask();
		depthMask->setDepthMask(true);
		glClear(GL_DEPTH_BUFFER_BIT);
	}
	m_cachedFunctions->getCachedBindFramebuffer()->bind(_target, _name);
}

void ContextImpl::deleteRenderbuffer(graphics::ObjectHandle _name)
{
	u32 rbo(_name);
	if (rbo != 0) {
		glDeleteRenderbuffers(1, &rbo);
		m_cachedFunctions->getCachedBindRenderbuffer()->reset();
	}
}

graphics::ObjectHandle ContextImpl::createRenderbuffer()
{
	return m_createRenderbuffer->createRenderbuffer();
}

void ContextImpl::initRenderbuffer(const gli_init_renderbuffer_params & _params)
{
	m_initRenderbuffer->initRenderbuffer(_params);
}

void ContextImpl::addFrameBufferRenderTarget(const gli_fb_render_target & _params)
{
	m_addFramebufferRenderTarget->addFrameBufferRenderTarget(_params);
}

bool ContextImpl::blitFramebuffers(const gli_blit_params & _params)
{
	return m_blitFramebuffers->blitFramebuffers(_params);
}

void ContextImpl::setDrawBuffers(u32 _num)
{
	GLenum targets[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
	glDrawBuffers(_num, targets);
}

graphics::PixelReadBuffer * ContextImpl::createPixelReadBuffer(size_t _sizeInBytes)
{
	if (m_createPixelReadBuffer)
		return m_createPixelReadBuffer->createPixelReadBuffer(_sizeInBytes);
	return nullptr;
}

gli_cbr * ContextImpl::createColorBufferReader(CachedTexture * _pTexture)
{
	if (m_glInfo.bufferStorage && m_glInfo.renderer != Renderer::Intel)
		return gli_gl_cbr_new_buffer_storage(_pTexture, m_cachedFunctions->getCachedBindBuffer());

	if (!m_glInfo.isGLES2)
		return gli_gl_cbr_new_pixel_buffer(_pTexture, m_cachedFunctions->getCachedBindBuffer());

	return gli_gl_cbr_new_read_pixels(_pTexture);
}

/*---------------Shaders-------------*/

bool ContextImpl::isCombinerProgramBuilderObsolete()
{
	if (!m_combinerProgramBuilder)
		return true;
	return m_combinerProgramBuilder->isObsolete();
}

bool ContextImpl::isCombinerUberOnly()
{
	return m_combinerProgramBuilder && m_combinerProgramBuilder->isUberOnly();
}

void ContextImpl::resetCombinerProgramBuilder()
{
	if (!isCombinerProgramBuilderObsolete())
		return;
	m_combinerProgramBuilder.reset(new glsl::CombinerProgramBuilder(m_glInfo, m_cachedFunctions->getCachedUseProgram()));
	m_specialShadersFactory.reset(new glsl::SpecialShadersFactory(m_glInfo,
		m_cachedFunctions->getCachedUseProgram(),
		m_combinerProgramBuilder->getVertexShaderHeader(),
		m_combinerProgramBuilder->getFragmentShaderHeader(),
		m_combinerProgramBuilder->getFragmentShaderEnd()));
}

graphics::CombinerProgram * ContextImpl::createCombinerProgramAsync(Combiner & _color, Combiner & _alpha, const CombinerKey & _key)
{
	return m_combinerProgramBuilder->buildCombinerProgramAsync(_color, _alpha, _key);
}

graphics::CombinerProgram * ContextImpl::createCombinerProgram(Combiner & _color, Combiner & _alpha, const CombinerKey & _key)
{
	return m_combinerProgramBuilder->buildCombinerProgram(_color, _alpha, _key);
}

bool ContextImpl::saveShadersStorage(const gli_combiner_table * _combiners)
{
	glsl::ShaderStorage storage(m_glInfo, m_cachedFunctions->getCachedUseProgram());
	return storage.saveShadersStorage(_combiners);
}

std::string ContextImpl::getCombinerJournalPath(u32 & _version)
{
	glsl::ShaderStorage storage(m_glInfo, m_cachedFunctions->getCachedUseProgram());
	return storage.getKeysJournalPath(_version);
}

bool ContextImpl::loadShadersStorage(gli_combiner_table * _combiners)
{
	glsl::ShaderStorage storage(m_glInfo, m_cachedFunctions->getCachedUseProgram());
	return storage.loadShadersStorage(_combiners);
}

graphics::ShaderProgram * ContextImpl::createDepthFogShader()
{
	return m_specialShadersFactory->createShadowMapShader();
}

graphics::TexrectDrawerShaderProgram * ContextImpl::createTexrectDrawerDrawShader()
{
	return m_specialShadersFactory->createTexrectDrawerDrawShader();
}

graphics::ShaderProgram * ContextImpl::createTexrectDrawerClearShader()
{
	return m_specialShadersFactory->createTexrectDrawerClearShader();
}

graphics::ShaderProgram * ContextImpl::createTexrectCopyShader()
{
	return m_specialShadersFactory->createTexrectCopyShader();
}

graphics::ShaderProgram * ContextImpl::createGammaCorrectionShader()
{
	return m_specialShadersFactory->createGammaCorrectionShader();
}

graphics::ShaderProgram * ContextImpl::createFXAAShader()
{
	return m_specialShadersFactory->createFXAAShader();
}

void ContextImpl::resetShaderProgram()
{
	m_cachedFunctions->getCachedUseProgram()->useProgram(graphics::ObjectHandle::null);
}

void ContextImpl::drawTriangles(const gli_draw_triangles_params & _params)
{
	m_graphicsDrawer->drawTriangles(_params);
}

void ContextImpl::drawRects(const gli_draw_rects_params & _params)
{
	m_graphicsDrawer->drawRects(_params);
}

void ContextImpl::drawLine(f32 _width, SPVertex * _vertices)
{
	m_graphicsDrawer->drawLine(_width, _vertices);
}


f32 ContextImpl::getMaxLineWidth()
{
	GLfloat lineWidthRange[2] = { 0.0f, 0.0f };
	glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineWidthRange);
	return lineWidthRange[1];
}

s32 ContextImpl::getMaxMSAALevel()
{
	GLint maxSamples = 0;
	if (m_glInfo.msaa)
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
	return maxSamples;
}

bool ContextImpl::isSupported(graphics::SpecialFeatures _feature) const
{
	switch (_feature) {
	case graphics::SpecialFeatures::BlitFramebuffer:
		return !m_glInfo.isGLES2;
	case graphics::SpecialFeatures::WeakBlitFramebuffer:
		return m_glInfo.isGLESX;
	case graphics::SpecialFeatures::Multisampling:
		return m_glInfo.msaa;
	case graphics::SpecialFeatures::ImageTextures:
		return m_glInfo.imageTextures;
	case graphics::SpecialFeatures::ShaderProgramBinary:
		return m_glInfo.shaderStorage;
	case graphics::SpecialFeatures::DepthFramebufferTextures:
		return m_glInfo.depthTexture;
	case graphics::SpecialFeatures::IntegerTextures:
		return !m_glInfo.isGLES2;
	case graphics::SpecialFeatures::ClipControl:
		return !m_glInfo.isGLESX;
	case graphics::SpecialFeatures::FramebufferFetch:
		return m_glInfo.ext_fetch;
	case graphics::SpecialFeatures::TextureBarrier:
		return m_glInfo.texture_barrier || m_glInfo.texture_barrierNV;
	case graphics::SpecialFeatures::DualSourceBlending:
		return m_glInfo.dual_source_blending;
	}
	return false;
}

bool ContextImpl::isError() const
{
	return Utils::isGLError();
}

bool ContextImpl::isFramebufferError() const
{
	return Utils::isFramebufferError();
}

const gli_context_ops * gli_gl_context_ops(void)
{
	return &graphics::ContextOps<opengl::ContextImpl>::table;
}

void * gli_gl_context_new(void)
{
	return new opengl::ContextImpl;
}
