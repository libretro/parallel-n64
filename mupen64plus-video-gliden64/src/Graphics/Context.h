#pragma once

#include <memory>
#include <string>
#include <Combiner.h>
#include "ObjectHandle.h"
#include "Parameter.h"
#include "CombinerProgram.h"
#include "ShaderProgram.h"
#include "PixelBuffer.h"
#include "FramebufferTextureFormats.h"

#define GRAPHICS_CONTEXT

#include "gli_context.h"

struct CachedTexture;

namespace graphics {

	enum class SpecialFeatures {
		Multisampling,
		BlitFramebuffer,
		WeakBlitFramebuffer,
		DepthFramebufferTextures,
		ShaderProgramBinary,
		ImageTextures,
		IntegerTextures,
		ClipControl,
		FramebufferFetch,
		TextureBarrier,
		DualSourceBlending
	};

	enum class ClampMode {
		ClippingEnabled,
		NoNearPlaneClipping,
		NoClipping
	};

	class ContextImpl;
	class ColorBufferReader;

	class Context
	{
	public:

		static void init() { gli_context_init(); }

		static void destroy() { gli_context_destroy(); }

		static void setClampMode(ClampMode _mode) { gli_ctx.ops->setClampMode(gli_ctx.impl, u32(_mode)); }

		static ClampMode getClampMode() { return ClampMode(gli_ctx.ops->getClampMode(gli_ctx.impl)); }

		static void enable(EnableParam _parameter, bool _enable) { gli_ctx.ops->enable(gli_ctx.impl, u32(_parameter), _enable); }

		static u32 isEnabled(EnableParam _parameter) { return gli_ctx.ops->isEnabled(gli_ctx.impl, u32(_parameter)); }

		static void cullFace(CullModeParam _mode) { gli_ctx.ops->cullFace(gli_ctx.impl, u32(_mode)); }

		static void enableDepthWrite(bool _enable) { gli_ctx.ops->enableDepthWrite(gli_ctx.impl, _enable); }

		static void setDepthCompare(CompareParam _mode) { gli_ctx.ops->setDepthCompare(gli_ctx.impl, u32(_mode)); }

		static void setViewport(s32 _x, s32 _y, s32 _width, s32 _height) { gli_ctx.ops->setViewport(gli_ctx.impl, _x, _y, _width, _height); }

		static void setScissor(s32 _x, s32 _y, s32 _width, s32 _height) { gli_ctx.ops->setScissor(gli_ctx.impl, _x, _y, _width, _height); }

		static void setBlending(BlendParam _sfactor, BlendParam _dfactor) { gli_ctx.ops->setBlending(gli_ctx.impl, u32(_sfactor), u32(_dfactor)); }

		static void setBlendingSeparate(BlendParam _sfactorcolor, BlendParam _dfactorcolor, BlendParam _sfactoralpha, BlendParam _dfactoralpha)
		{
			gli_ctx.ops->setBlendingSeparate(gli_ctx.impl, u32(_sfactorcolor), u32(_dfactorcolor), u32(_sfactoralpha), u32(_dfactoralpha));
		}

		static void setBlendColor(f32 _red, f32 _green, f32 _blue, f32 _alpha) { gli_ctx.ops->setBlendColor(gli_ctx.impl, _red, _green, _blue, _alpha); }

		static void clearColorBuffer(f32 _red, f32 _green, f32 _blue, f32 _alpha) { gli_ctx.ops->clearColorBuffer(gli_ctx.impl, _red, _green, _blue, _alpha); }

		static void clearDepthBuffer() { gli_ctx.ops->clearDepthBuffer(gli_ctx.impl); }

		static void setPolygonOffset(f32 _factor, f32 _units) { gli_ctx.ops->setPolygonOffset(gli_ctx.impl, _factor, _units); }

		/*---------------Texture-------------*/

		static ObjectHandle createTexture(Parameter _target) { return ObjectHandle(gli_ctx.ops->createTexture(gli_ctx.impl, u32(_target))); }

		static void deleteTexture(ObjectHandle _name) { gli_ctx.ops->deleteTexture(gli_ctx.impl, u32(_name)); }

		struct InitTextureParams {
			ObjectHandle handle;
			TextureUnitParam textureUnitIndex{0};
			TextureTargetParam target = textureTarget::TEXTURE_2D;
			u32 msaaLevel = 0;
			u32 width = 0;
			u32 height = 0;
			u32 mipMapLevel = 0;
			u32 mipMapLevels = 1;
			ColorFormatParam format;
			InternalColorFormatParam internalFormat;
			DatatypeParam dataType;
			const void * data = nullptr;
			/* The texture cache reuses the objects it evicts, so its storage
			 * has to stay respecifiable: immutable storage cannot be. */
			bool mutableStorage = false;
		};

		static void init2DTexture(const InitTextureParams & _params) { gli_ctx.ops->init2DTexture(gli_ctx.impl, &_params); }

		struct UpdateTextureDataParams {
			ObjectHandle handle;
			TextureUnitParam textureUnitIndex{0};
			u32 x = 0;
			u32 y = 0;
			u32 width = 0;
			u32 height = 0;
			u32 mipMapLevel = 0;
			ColorFormatParam format;
			InternalColorFormatParam internalFormat;
			DatatypeParam dataType;
			const void * data = nullptr;
		};

		static void update2DTexture(const UpdateTextureDataParams & _params) { gli_ctx.ops->update2DTexture(gli_ctx.impl, &_params); }

		static void * lendTextureData(size_t _bytes) { return gli_ctx.ops->lendTextureData(gli_ctx.impl, _bytes); }

		struct TexParameters {
			ObjectHandle handle;
			TextureUnitParam textureUnitIndex{0};
			TextureTargetParam target;
			TextureParam magFilter;
			TextureParam minFilter;
			TextureParam wrapS;
			TextureParam wrapT;
			Parameter maxMipmapLevel;
			Parameter maxAnisotropy;
		};

		static void setTextureParameters(const TexParameters & _parameters) { gli_ctx.ops->setTextureParameters(gli_ctx.impl, &_parameters); }

		struct BindTextureParameters {
			ObjectHandle texture;
			TextureUnitParam textureUnitIndex;
			TextureTargetParam target;
		};

		static void bindTexture(const BindTextureParameters & _params) { gli_ctx.ops->bindTexture(gli_ctx.impl, &_params); }

		static void setTextureUnpackAlignment(s32 _param) { gli_ctx.ops->setTextureUnpackAlignment(gli_ctx.impl, _param); }

		static s32 getTextureUnpackAlignment() { return gli_ctx.ops->getTextureUnpackAlignment(gli_ctx.impl); }

		static s32 getMaxTextureSize() { return gli_ctx.ops->getMaxTextureSize(gli_ctx.impl); }

		static f32 getMaxAnisotropy() { return gli_ctx.ops->getMaxAnisotropy(gli_ctx.impl); }

		struct BindImageTextureParameters {
			ImageUnitParam imageUnit;
			ObjectHandle texture;
			ImageAccessModeParam accessMode;
			InternalColorFormatParam textureFormat;
		};

		static void bindImageTexture(const BindImageTextureParameters & _params) { gli_ctx.ops->bindImageTexture(gli_ctx.impl, &_params); }

		static u32 convertInternalTextureFormat(u32 _format) { return gli_ctx.ops->convertInternalTextureFormat(gli_ctx.impl, _format); }

		static void textureBarrier() { gli_ctx.ops->textureBarrier(gli_ctx.impl); }

		/*---------------Framebuffer-------------*/

		static const FramebufferTextureFormats & getFramebufferTextureFormats()
		{
			return *static_cast<const FramebufferTextureFormats *>(gli_ctx.formats);
		}

		static ObjectHandle createFramebuffer() { return ObjectHandle(gli_ctx.ops->createFramebuffer(gli_ctx.impl)); }

		static void deleteFramebuffer(ObjectHandle _name) { gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, u32(_name)); }

		static void bindFramebuffer(BufferTargetParam _target, ObjectHandle _name) { gli_ctx.ops->bindFramebuffer(gli_ctx.impl, u32(_target), u32(_name)); }

		static ObjectHandle createRenderbuffer() { return ObjectHandle(gli_ctx.ops->createRenderbuffer(gli_ctx.impl)); }

		struct InitRenderbufferParams {
			ObjectHandle handle;
			TextureTargetParam target;
			InternalColorFormatParam format;
			u32 width = 0;
			u32 height = 0;
		};

		static void initRenderbuffer(const InitRenderbufferParams & _params) { gli_ctx.ops->initRenderbuffer(gli_ctx.impl, &_params); }

		struct FrameBufferRenderTarget {
			ObjectHandle bufferHandle;
			BufferTargetParam bufferTarget;
			BufferAttachmentParam attachment;
			Parameter textureTarget;
			ObjectHandle textureHandle;
		};

		static void addFrameBufferRenderTarget(const FrameBufferRenderTarget & _params) { gli_ctx.ops->addFrameBufferRenderTarget(gli_ctx.impl, &_params); }

		struct BlitFramebuffersParams
		{
			ObjectHandle readBuffer;
			ObjectHandle drawBuffer;
			s32 srcX0;
			s32 srcY0;
			s32 srcX1;
			s32 srcY1;
			s32 dstX0;
			s32 dstY0;
			s32 dstX1;
			s32 dstY1;
			Parameter mask;
			Parameter filter;
		};

		static bool blitFramebuffers(const BlitFramebuffersParams & _params) { return gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &_params); }

		static void setDrawBuffers(u32 _num) { gli_ctx.ops->setDrawBuffers(gli_ctx.impl, _num); }

		/*---------------Pixelbuffer-------------*/

		static PixelReadBuffer * createPixelReadBuffer(size_t _sizeInBytes)
		{
			return static_cast<PixelReadBuffer *>(gli_ctx.ops->createPixelReadBuffer(gli_ctx.impl, _sizeInBytes));
		}

		static ColorBufferReader * createColorBufferReader(CachedTexture * _pTexture)
		{
			return static_cast<ColorBufferReader *>(gli_ctx.ops->createColorBufferReader(gli_ctx.impl, _pTexture));
		}

		/*---------------Shaders-------------*/

		static bool isCombinerProgramBuilderObsolete() { return gli_ctx.ops->isCombinerProgramBuilderObsolete(gli_ctx.impl); }

		static bool isCombinerUberOnly() { return gli_ctx.ops->isCombinerUberOnly(gli_ctx.impl); }

		static void resetCombinerProgramBuilder() { gli_ctx.ops->resetCombinerProgramBuilder(gli_ctx.impl); }

		static CombinerProgram * createCombinerProgram(Combiner & _color, Combiner & _alpha, const CombinerKey & _key)
		{
			return static_cast<CombinerProgram *>(gli_ctx.ops->createCombinerProgram(gli_ctx.impl, &_color, &_alpha, &_key));
		}

		/* nullptr when it cannot be done without waiting */
		static CombinerProgram * createCombinerProgramAsync(Combiner & _color, Combiner & _alpha, const CombinerKey & _key)
		{
			return static_cast<CombinerProgram *>(gli_ctx.ops->createCombinerProgramAsync(gli_ctx.impl, &_color, &_alpha, &_key));
		}

		static bool saveShadersStorage(const Combiners & _combiners) { return gli_ctx.ops->saveShadersStorage(gli_ctx.impl, &_combiners); }

		static bool loadShadersStorage(Combiners & _combiners) { return gli_ctx.ops->loadShadersStorage(gli_ctx.impl, &_combiners); }

		/* where the keys met this session go, and their format's version */
		static std::string getCombinerJournalPath(u32 & _version)
		{
			std::string path;
			gli_ctx.ops->getCombinerJournalPath(gli_ctx.impl, &path, &_version);
			return path;
		}

		static ShaderProgram * createDepthFogShader() { return static_cast<ShaderProgram *>(gli_ctx.ops->createDepthFogShader(gli_ctx.impl)); }

		static TexrectDrawerShaderProgram * createTexrectDrawerDrawShader()
		{
			return static_cast<TexrectDrawerShaderProgram *>(gli_ctx.ops->createTexrectDrawerDrawShader(gli_ctx.impl));
		}

		static ShaderProgram * createTexrectDrawerClearShader() { return static_cast<ShaderProgram *>(gli_ctx.ops->createTexrectDrawerClearShader(gli_ctx.impl)); }

		static ShaderProgram * createTexrectCopyShader() { return static_cast<ShaderProgram *>(gli_ctx.ops->createTexrectCopyShader(gli_ctx.impl)); }

		static ShaderProgram * createGammaCorrectionShader() { return static_cast<ShaderProgram *>(gli_ctx.ops->createGammaCorrectionShader(gli_ctx.impl)); }

		static ShaderProgram * createFXAAShader() { return static_cast<ShaderProgram *>(gli_ctx.ops->createFXAAShader(gli_ctx.impl)); }


		static void resetShaderProgram() { gli_ctx.ops->resetShaderProgram(gli_ctx.impl); }

		/*---------------Draw-------------*/

		struct DrawTriangleParameters
		{
			DrawModeParam mode;
			Parameter elementsType;
			u32 verticesCount = 0;
			u32 elementsCount = 0;
			bool flatColors = false;
			SPVertex * vertices = nullptr;
			void * elements = nullptr;
			const CombinerProgram * combiner = nullptr;
		};

		static void drawTriangles(const DrawTriangleParameters & _params) { gli_ctx.ops->drawTriangles(gli_ctx.impl, &_params); }

		struct DrawRectParameters
		{
			DrawModeParam mode;
			bool texrect = true;
			u32 verticesCount = 0;
			RectVertex * vertices = nullptr;
			const CombinerProgram * combiner = nullptr;
		};

		static void drawRects(const DrawRectParameters & _params) { gli_ctx.ops->drawRects(gli_ctx.impl, &_params); }

		static void drawLine(f32 _width, SPVertex * _vertices) { gli_ctx.ops->drawLine(gli_ctx.impl, _width, _vertices); }

		static f32 getMaxLineWidth() { return gli_ctx.ops->getMaxLineWidth(gli_ctx.impl); }

		/*---------------Misc-------------*/
		static s32 getMaxMSAALevel() { return gli_ctx.ops->getMaxMSAALevel(gli_ctx.impl); }

		static bool isError() { return gli_ctx.ops->isError(gli_ctx.impl); }

		static bool isFramebufferError() { return gli_ctx.ops->isFramebufferError(gli_ctx.impl); }

	};

}

extern graphics::Context gfxContext;
