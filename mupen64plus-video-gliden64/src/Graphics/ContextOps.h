#pragma once

/* A backend's C++ class as the C table GLideN64 calls (gli_context.h):
 * each entry calls the class's own function by name, so no virtual call
 * is left, and the compiler can take the function's body in place.
 * Included by the one file of each backend that names its class:
 *   const gli_context_ops *gli_gl_context_ops(void)
 *   { return &graphics::ContextOps<opengl::ContextImpl>::table; } */

#include <string>
#include <stdio.h>
#include "gli_context.h"
#include "gli_combiner.h"
#include "ContextImpl.h"

namespace graphics {

	/* the C numbers are the C++ enums', which the entries cast straight across */
	static_assert(u32(SpecialFeatures::Multisampling) == GLI_FEATURE_MULTISAMPLING &&
		u32(SpecialFeatures::BlitFramebuffer) == GLI_FEATURE_BLIT_FRAMEBUFFER &&
		u32(SpecialFeatures::WeakBlitFramebuffer) == GLI_FEATURE_WEAK_BLIT_FRAMEBUFFER &&
		u32(SpecialFeatures::DepthFramebufferTextures) == GLI_FEATURE_DEPTH_FRAMEBUFFER_TEXTURES &&
		u32(SpecialFeatures::ShaderProgramBinary) == GLI_FEATURE_SHADER_PROGRAM_BINARY &&
		u32(SpecialFeatures::ImageTextures) == GLI_FEATURE_IMAGE_TEXTURES &&
		u32(SpecialFeatures::IntegerTextures) == GLI_FEATURE_INTEGER_TEXTURES &&
		u32(SpecialFeatures::ClipControl) == GLI_FEATURE_CLIP_CONTROL &&
		u32(SpecialFeatures::FramebufferFetch) == GLI_FEATURE_FRAMEBUFFER_FETCH &&
		u32(SpecialFeatures::TextureBarrier) == GLI_FEATURE_TEXTURE_BARRIER &&
		u32(SpecialFeatures::DualSourceBlending) == GLI_FEATURE_DUAL_SOURCE_BLENDING &&
		u32(SpecialFeatures::DualSourceBlending) + 1 == GLI_FEATURE_COUNT,
		"SpecialFeatures and GLI_FEATURE_* must count alike");
	static_assert(sizeof(ObjectHandle) == sizeof(u32) && sizeof(Parameter) == sizeof(u32),
		"handles and parameters cross as their 32-bit values");

	template <class T>
	struct ContextOps
	{
		static T * I(void * _p) { return static_cast<T *>(_p); }
		static const T * C(const void * _p) { return static_cast<const T *>(_p); }

		static void free_impl(void * _p) { delete I(_p); }
		static void init(void * _p) { I(_p)->T::init(); }
		static void destroy(void * _p) { I(_p)->T::destroy(); }
		static void setClampMode(void * _p, u32 _mode) { I(_p)->T::setClampMode(ClampMode(_mode)); }
		static u32 getClampMode(void * _p) { return u32(I(_p)->T::getClampMode()); }
		static void enable(void * _p, u32 _parameter, bool _enable) { I(_p)->T::enable(EnableParam(_parameter), _enable); }
		static u32 isEnabled(void * _p, u32 _parameter) { return I(_p)->T::isEnabled(EnableParam(_parameter)); }
		static void cullFace(void * _p, u32 _mode) { I(_p)->T::cullFace(CullModeParam(_mode)); }
		static void enableDepthWrite(void * _p, bool _enable) { I(_p)->T::enableDepthWrite(_enable); }
		static void setDepthCompare(void * _p, u32 _mode) { I(_p)->T::setDepthCompare(CompareParam(_mode)); }
		static void setViewport(void * _p, s32 _x, s32 _y, s32 _w, s32 _h) { I(_p)->T::setViewport(_x, _y, _w, _h); }
		static void setScissor(void * _p, s32 _x, s32 _y, s32 _w, s32 _h) { I(_p)->T::setScissor(_x, _y, _w, _h); }
		static void setBlending(void * _p, u32 _s, u32 _d) { I(_p)->T::setBlending(BlendParam(_s), BlendParam(_d)); }
		static void setBlendingSeparate(void * _p, u32 _sc, u32 _dc, u32 _sa, u32 _da)
		{
			I(_p)->T::setBlendingSeparate(BlendParam(_sc), BlendParam(_dc), BlendParam(_sa), BlendParam(_da));
		}
		static void setBlendColor(void * _p, f32 _r, f32 _g, f32 _b, f32 _a) { I(_p)->T::setBlendColor(_r, _g, _b, _a); }
		static void clearColorBuffer(void * _p, f32 _r, f32 _g, f32 _b, f32 _a) { I(_p)->T::clearColorBuffer(_r, _g, _b, _a); }
		static void clearDepthBuffer(void * _p) { I(_p)->T::clearDepthBuffer(); }
		static void setPolygonOffset(void * _p, f32 _factor, f32 _units) { I(_p)->T::setPolygonOffset(_factor, _units); }

		static u32 createTexture(void * _p, u32 _target) { return u32(I(_p)->T::createTexture(Parameter(_target))); }
		static void deleteTexture(void * _p, u32 _name) { I(_p)->T::deleteTexture(ObjectHandle(_name)); }
		static void init2DTexture(void * _p, const gli_init_texture_params * _params) { I(_p)->T::init2DTexture(*_params); }
		static void update2DTexture(void * _p, const gli_update_texture_params * _params) { I(_p)->T::update2DTexture(*_params); }
		static void * lendTextureData(void * _p, size_t _bytes) { return I(_p)->T::lendTextureData(_bytes); }
		static void setTextureParameters(void * _p, const gli_tex_parameters * _params) { I(_p)->T::setTextureParameters(*_params); }
		static void bindTexture(void * _p, const gli_bind_texture_params * _params) { I(_p)->T::bindTexture(*_params); }
		static void setTextureUnpackAlignment(void * _p, s32 _param) { I(_p)->T::setTextureUnpackAlignment(_param); }
		static s32 getTextureUnpackAlignment(const void * _p) { return C(_p)->T::getTextureUnpackAlignment(); }
		static s32 getMaxTextureSize(const void * _p) { return C(_p)->T::getMaxTextureSize(); }
		static f32 getMaxAnisotropy(const void * _p) { return C(_p)->T::getMaxAnisotropy(); }
		static void bindImageTexture(void * _p, const gli_bind_image_texture_params * _params)
		{
			I(_p)->T::bindImageTexture(*_params);
		}
		static u32 convertInternalTextureFormat(const void * _p, u32 _format) { return C(_p)->T::convertInternalTextureFormat(_format); }
		static void textureBarrier(void * _p) { I(_p)->T::textureBarrier(); }

		static void getFramebufferTextureFormats(void * _p, gli_fb_formats * _formats) { I(_p)->T::getFramebufferTextureFormats(*_formats); }
		static u32 createFramebuffer(void * _p) { return u32(I(_p)->T::createFramebuffer()); }
		static void deleteFramebuffer(void * _p, u32 _name) { I(_p)->T::deleteFramebuffer(ObjectHandle(_name)); }
		static void bindFramebuffer(void * _p, u32 _target, u32 _name)
		{
			I(_p)->T::bindFramebuffer(BufferTargetParam(_target), ObjectHandle(_name));
		}
		static void addFrameBufferRenderTarget(void * _p, const gli_fb_render_target * _params)
		{
			I(_p)->T::addFrameBufferRenderTarget(*_params);
		}
		static u32 createRenderbuffer(void * _p) { return u32(I(_p)->T::createRenderbuffer()); }
		static void deleteRenderbuffer(void * _p, u32 _name) { I(_p)->T::deleteRenderbuffer(ObjectHandle(_name)); }
		static void initRenderbuffer(void * _p, const gli_init_renderbuffer_params * _params)
		{
			I(_p)->T::initRenderbuffer(*_params);
		}
		static bool blitFramebuffers(void * _p, const gli_blit_params * _params)
		{
			return I(_p)->T::blitFramebuffers(*_params);
		}
		static void setDrawBuffers(void * _p, u32 _num) { I(_p)->T::setDrawBuffers(_num); }
		static void * createPixelReadBuffer(void * _p, size_t _bytes) { return I(_p)->T::createPixelReadBuffer(_bytes); }
		static void * createColorBufferReader(void * _p, void * _texture)
		{
			return I(_p)->T::createColorBufferReader(static_cast<CachedTexture *>(_texture));
		}

		static bool isCombinerProgramBuilderObsolete(void * _p) { return I(_p)->T::isCombinerProgramBuilderObsolete(); }
		static bool isCombinerUberOnly(void * _p) { return I(_p)->T::isCombinerUberOnly(); }
		static void resetCombinerProgramBuilder(void * _p) { I(_p)->T::resetCombinerProgramBuilder(); }
		static gli_cprog * createCombinerProgram(void * _p, Combiner * _color, Combiner * _alpha, u64 _key)
		{
			return I(_p)->T::createCombinerProgram(*_color, *_alpha, CombinerKey(_key, false));
		}
		static gli_cprog * createCombinerProgramAsync(void * _p, Combiner * _color, Combiner * _alpha, u64 _key)
		{
			return I(_p)->T::createCombinerProgramAsync(*_color, *_alpha, CombinerKey(_key, false));
		}
		static bool saveShadersStorage(void * _p, const gli_combiner_table * _combiners)
		{
			return I(_p)->T::saveShadersStorage(_combiners);
		}
		static bool loadShadersStorage(void * _p, gli_combiner_table * _combiners)
		{
			return I(_p)->T::loadShadersStorage(_combiners);
		}
		static void getCombinerJournalPath(void * _p, char * _path, size_t _size, u32 * _version)
		{
			snprintf(_path, _size, "%s", I(_p)->T::getCombinerJournalPath(*_version).c_str());
		}
		static gli_cprog * createDepthFogShader(void * _p) { return I(_p)->T::createDepthFogShader(); }
		static gli_cprog * createTexrectDrawerDrawShader(void * _p) { return I(_p)->T::createTexrectDrawerDrawShader(); }
		static gli_cprog * createTexrectDrawerClearShader(void * _p) { return I(_p)->T::createTexrectDrawerClearShader(); }
		static gli_cprog * createTexrectCopyShader(void * _p) { return I(_p)->T::createTexrectCopyShader(); }
		static gli_cprog * createGammaCorrectionShader(void * _p) { return I(_p)->T::createGammaCorrectionShader(); }
		static gli_cprog * createFXAAShader(void * _p) { return I(_p)->T::createFXAAShader(); }
		static void resetShaderProgram(void * _p) { I(_p)->T::resetShaderProgram(); }

		static void drawTriangles(void * _p, const gli_draw_triangles_params * _params)
		{
			I(_p)->T::drawTriangles(*_params);
		}
		static void drawRects(void * _p, const gli_draw_rects_params * _params)
		{
			I(_p)->T::drawRects(*_params);
		}
		static void drawLine(void * _p, f32 _width, void * _vertices) { I(_p)->T::drawLine(_width, static_cast<SPVertex *>(_vertices)); }
		static f32 getMaxLineWidth(void * _p) { return I(_p)->T::getMaxLineWidth(); }
		static s32 getMaxMSAALevel(void * _p) { return I(_p)->T::getMaxMSAALevel(); }
		static bool isSupported(const void * _p, u32 _feature) { return C(_p)->T::isSupported(SpecialFeatures(_feature)); }
		static bool isError(const void * _p) { return C(_p)->T::isError(); }
		static bool isFramebufferError(const void * _p) { return C(_p)->T::isFramebufferError(); }

		static const gli_context_ops table;
	};

	/* in gli_context_ops's order */
	template <class T>
	const gli_context_ops ContextOps<T>::table = {
		&free_impl, &init, &destroy, &setClampMode, &getClampMode, &enable, &isEnabled, &cullFace,
		&enableDepthWrite, &setDepthCompare, &setViewport, &setScissor, &setBlending, &setBlendingSeparate,
		&setBlendColor, &clearColorBuffer, &clearDepthBuffer, &setPolygonOffset,
		&createTexture, &deleteTexture, &init2DTexture, &update2DTexture, &lendTextureData,
		&setTextureParameters, &bindTexture, &setTextureUnpackAlignment, &getTextureUnpackAlignment,
		&getMaxTextureSize, &getMaxAnisotropy, &bindImageTexture, &convertInternalTextureFormat, &textureBarrier,
		&getFramebufferTextureFormats, &createFramebuffer, &deleteFramebuffer, &bindFramebuffer,
		&addFrameBufferRenderTarget, &createRenderbuffer, &deleteRenderbuffer, &initRenderbuffer, &blitFramebuffers, &setDrawBuffers,
		&createPixelReadBuffer, &createColorBufferReader,
		&isCombinerProgramBuilderObsolete, &isCombinerUberOnly, &resetCombinerProgramBuilder,
		&createCombinerProgram, &createCombinerProgramAsync, &saveShadersStorage, &loadShadersStorage,
		&getCombinerJournalPath, &createDepthFogShader, &createTexrectDrawerDrawShader,
		&createTexrectDrawerClearShader, &createTexrectCopyShader, &createGammaCorrectionShader, &createFXAAShader,
		&resetShaderProgram,
		&drawTriangles, &drawRects, &drawLine, &getMaxLineWidth, &getMaxMSAALevel, &isSupported, &isError,
		&isFramebufferError
	};

}
