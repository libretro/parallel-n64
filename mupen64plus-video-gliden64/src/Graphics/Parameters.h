#pragma once
#include "Parameter.h"
#include "gli_params.h"

namespace graphics {

	namespace colorFormat {
		constexpr ColorFormatParam RED_GREEN_BLUE(GLI_CF_RED_GREEN_BLUE);
		constexpr ColorFormatParam RGBA(GLI_CF_RGBA);
		constexpr ColorFormatParam RG(GLI_CF_RG);
		constexpr ColorFormatParam RED(GLI_CF_RED);
		constexpr ColorFormatParam DEPTH(GLI_CF_DEPTH);
		constexpr ColorFormatParam LUMINANCE(GLI_CF_LUMINANCE);
	}

	namespace internalcolorFormat {
		constexpr InternalColorFormatParam NOCOLOR(GLI_ICF_NOCOLOR);
		constexpr InternalColorFormatParam RGB8(GLI_ICF_RGB8);
		constexpr InternalColorFormatParam RGBA8(GLI_ICF_RGBA8);
		constexpr InternalColorFormatParam RGBA4(GLI_ICF_RGBA4);
		constexpr InternalColorFormatParam RGB5_A1(GLI_ICF_RGB5_A1);
		constexpr InternalColorFormatParam RG(GLI_ICF_RG);
		constexpr InternalColorFormatParam R16F(GLI_ICF_R16F);
		constexpr InternalColorFormatParam DEPTH(GLI_ICF_DEPTH);
		constexpr InternalColorFormatParam RG32F(GLI_ICF_RG32F);
		constexpr InternalColorFormatParam LUMINANCE(GLI_ICF_LUMINANCE);
		constexpr InternalColorFormatParam COLOR_INDEX8(GLI_ICF_COLOR_INDEX8);
	}

	namespace datatype {
		constexpr DatatypeParam UNSIGNED_BYTE(GLI_DT_UNSIGNED_BYTE);
		constexpr DatatypeParam UNSIGNED_SHORT(GLI_DT_UNSIGNED_SHORT);
		constexpr DatatypeParam UNSIGNED_INT(GLI_DT_UNSIGNED_INT);
		constexpr DatatypeParam FLOAT(GLI_DT_FLOAT);
		constexpr DatatypeParam HALF_FLOAT(GLI_DT_HALF_FLOAT);
		constexpr DatatypeParam UNSIGNED_SHORT_5_6_5(GLI_DT_UNSIGNED_SHORT_5_6_5);
		constexpr DatatypeParam UNSIGNED_SHORT_5_5_5_1(GLI_DT_UNSIGNED_SHORT_5_5_5_1);
		constexpr DatatypeParam UNSIGNED_SHORT_4_4_4_4(GLI_DT_UNSIGNED_SHORT_4_4_4_4);
	}

	namespace textureTarget {
		constexpr TextureTargetParam TEXTURE_2D(GLI_TT_TEXTURE_2D);
		constexpr TextureTargetParam TEXTURE_2D_MULTISAMPLE(GLI_TT_TEXTURE_2D_MULTISAMPLE);
		constexpr TextureTargetParam TEXTURE_EXTERNAL(GLI_TT_TEXTURE_EXTERNAL);
		constexpr TextureTargetParam RENDERBUFFER(GLI_TT_RENDERBUFFER);
	}

	namespace bufferTarget {
		constexpr BufferTargetParam FRAMEBUFFER(GLI_BT_FRAMEBUFFER);
		/* GLES2 knows only FRAMEBUFFER: the GL backend makes these that
		 * (opengl_ContextImpl.cpp) */
		extern BufferTargetParam DRAW_FRAMEBUFFER;
		extern BufferTargetParam READ_FRAMEBUFFER;
	}

	namespace bufferAttachment {
		constexpr BufferAttachmentParam COLOR_ATTACHMENT0(GLI_BA_COLOR_ATTACHMENT0);
		constexpr BufferAttachmentParam COLOR_ATTACHMENT1(GLI_BA_COLOR_ATTACHMENT1);
		constexpr BufferAttachmentParam COLOR_ATTACHMENT2(GLI_BA_COLOR_ATTACHMENT2);
		constexpr BufferAttachmentParam DEPTH_ATTACHMENT(GLI_BA_DEPTH_ATTACHMENT);
	}

	namespace enable {
		constexpr EnableParam BLEND(GLI_EN_BLEND);
		constexpr EnableParam CULL_FACE(GLI_EN_CULL_FACE);
		constexpr EnableParam DEPTH_TEST(GLI_EN_DEPTH_TEST);
		constexpr EnableParam DEPTH_CLAMP(GLI_EN_DEPTH_CLAMP);
		constexpr EnableParam CLIP_DISTANCE0(GLI_EN_CLIP_DISTANCE0);
		constexpr EnableParam DITHER(GLI_EN_DITHER);
		constexpr EnableParam POLYGON_OFFSET_FILL(GLI_EN_POLYGON_OFFSET_FILL);
		constexpr EnableParam SCISSOR_TEST(GLI_EN_SCISSOR_TEST);
	}

	namespace textureIndices {
		constexpr TextureUnitParam Tex[2] = { TextureUnitParam(GLI_TU_TEX0), TextureUnitParam(GLI_TU_TEX1) };
		constexpr TextureUnitParam NoiseTex(GLI_TU_NoiseTex);
		constexpr TextureUnitParam DepthTex(GLI_TU_DepthTex);
		constexpr TextureUnitParam ZLUTTex(GLI_TU_ZLUTTex);
		constexpr TextureUnitParam PaletteTex(GLI_TU_PaletteTex);
		constexpr TextureUnitParam MSTex[2] = { TextureUnitParam(GLI_TU_MSTEX0), TextureUnitParam(GLI_TU_MSTEX1) };
	}

	namespace textureImageUnits {
		constexpr ImageUnitParam DepthZ(GLI_IU_DepthZ);
		constexpr ImageUnitParam DepthDeltaZ(GLI_IU_DepthDeltaZ);
	}

	namespace textureImageAccessMode {
		constexpr ImageAccessModeParam READ_ONLY(GLI_IA_READ_ONLY);
		constexpr ImageAccessModeParam WRITE_ONLY(GLI_IA_WRITE_ONLY);
		constexpr ImageAccessModeParam READ_WRITE(GLI_IA_READ_WRITE);
	}

	namespace textureParameters {
		constexpr TextureParam FILTER_NEAREST(GLI_TP_FILTER_NEAREST);
		constexpr TextureParam FILTER_LINEAR(GLI_TP_FILTER_LINEAR);
		constexpr TextureParam FILTER_NEAREST_MIPMAP_NEAREST(GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST);
		constexpr TextureParam FILTER_LINEAR_MIPMAP_NEAREST(GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST);
		constexpr TextureParam WRAP_CLAMP_TO_EDGE(GLI_TP_WRAP_CLAMP_TO_EDGE);
		constexpr TextureParam WRAP_REPEAT(GLI_TP_WRAP_REPEAT);
		constexpr TextureParam WRAP_MIRRORED_REPEAT(GLI_TP_WRAP_MIRRORED_REPEAT);
	}

	namespace cullMode {
		constexpr CullModeParam FRONT(GLI_CULLMODE_FRONT);
		constexpr CullModeParam BACK(GLI_CULLMODE_BACK);
		constexpr CullModeParam FRONT_AND_BACK(GLI_CULLMODE_FRONT_AND_BACK);
	}

	namespace compare {
		constexpr CompareParam LEQUAL(GLI_COMPARE_LEQUAL);
		constexpr CompareParam LESS(GLI_COMPARE_LESS);
		constexpr CompareParam ALWAYS(GLI_COMPARE_ALWAYS);
	}

	namespace blend {
		constexpr BlendParam ZERO(GLI_BLEND_ZERO);
		constexpr BlendParam ONE(GLI_BLEND_ONE);
		constexpr BlendParam SRC_ALPHA(GLI_BLEND_SRC_ALPHA);
		constexpr BlendParam DST_ALPHA(GLI_BLEND_DST_ALPHA);
		constexpr BlendParam ONE_MINUS_SRC_ALPHA(GLI_BLEND_ONE_MINUS_SRC_ALPHA);
		constexpr BlendParam CONSTANT_ALPHA(GLI_BLEND_CONSTANT_ALPHA);
		constexpr BlendParam ONE_MINUS_CONSTANT_ALPHA(GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA);
		constexpr BlendParam SRC1_COLOR(GLI_BLEND_SRC1_COLOR);
		constexpr BlendParam ONE_MINUS_SRC1_COLOR(GLI_BLEND_ONE_MINUS_SRC1_COLOR);
		constexpr BlendParam SRC1_ALPHA(GLI_BLEND_SRC1_ALPHA);
		constexpr BlendParam ONE_MINUS_SRC1_ALPHA(GLI_BLEND_ONE_MINUS_SRC1_ALPHA);
	}

	namespace drawmode {
		constexpr DrawModeParam TRIANGLES(GLI_DM_TRIANGLES);
		constexpr DrawModeParam TRIANGLE_STRIP(GLI_DM_TRIANGLE_STRIP);
		constexpr DrawModeParam LINES(GLI_DM_LINES);
	}

	namespace blitMask {
		constexpr BlitMaskParam COLOR_BUFFER(GLI_BM_COLOR_BUFFER);
		constexpr BlitMaskParam DEPTH_BUFFER(GLI_BM_DEPTH_BUFFER);
		constexpr BlitMaskParam STENCIL_BUFFER(GLI_BM_STENCIL_BUFFER);
	}
}

