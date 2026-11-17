/* The formats of the textures GLideN64's frame buffers are made of, for
 * each backend (gli_context.h). C89.
 *
 * The C++ had a class for each kind of GL and one for Vulkan, each with a
 * virtual init;
 * they are tables here, set over the unset formats. */
#include "gli_context.h"

#define SET(what, internal, format, type, bytes) \
	(_f->what##InternalFormat = (internal), _f->what##Format = (format), \
	 _f->what##Type = (type), _f->what##FormatBytes = (bytes))

void gli_fb_formats_gl(gli_fb_formats *_f, u32 _api, u32 _flags)
{
	if (_api == GLI_FB_GLES2) {
		/* no depth image, no LUTs: they stay unset */
		SET(monochrome, GLI_ICF_RGB, GLI_CF_RED_GREEN_BLUE, GLI_DT_UNSIGNED_SHORT_5_6_5, 2);
		if (_flags & GLI_FB_OES_DEPTH)
			SET(depth, GLI_ICF_DEPTH_COMPONENT, GLI_CF_DEPTH, GLI_DT_UNSIGNED_INT, 4);
		else
			SET(depth, GLI_ICF_DEPTH16, GLI_CF_DEPTH, GLI_DT_UNSIGNED_INT, 2);
		if (_flags & GLI_FB_OES_RGBA8)
			SET(color, GLI_ICF_RGBA, GLI_CF_RGBA, GLI_DT_UNSIGNED_BYTE, 4);
		else
			SET(color, GLI_ICF_RGB, GLI_CF_RED_GREEN_BLUE, GLI_DT_UNSIGNED_SHORT_5_6_5, 2);
		SET(noise, GLI_ICF_LUMINANCE, GLI_CF_LUMINANCE, GLI_DT_UNSIGNED_BYTE, 1);
		return;
	}
	if (_api == GLI_FB_GLES3 && (_flags & GLI_FB_ADRENO530))
		SET(color, GLI_ICF_RGBA32F, GLI_CF_RGBA, GLI_DT_FLOAT, 16);
	else
		SET(color, GLI_ICF_RGBA8, GLI_CF_RGBA, GLI_DT_UNSIGNED_BYTE, 4);
	SET(monochrome, GLI_ICF_R8, GLI_CF_RED, GLI_DT_UNSIGNED_BYTE, 1);
	/* GLES 3 reads depth as integers, desktop GL as floats */
	SET(depth, GLI_ICF_DEPTH, GLI_CF_DEPTH,
		_api == GLI_FB_GLES3 ? GLI_DT_UNSIGNED_INT : GLI_DT_FLOAT, 4);
	SET(depthImage, GLI_ICF_R32F, GLI_CF_RED, GLI_DT_FLOAT, 4);
	SET(lut, GLI_ICF_R16UI, GLI_CF_RED_INTEGER, GLI_DT_UNSIGNED_SHORT, 2);
	SET(noise, GLI_ICF_R8, GLI_CF_RED, GLI_DT_UNSIGNED_BYTE, 1);
}

/* Vulkan's are desktop GL's: the renderer maps the GL numbers to its own */
void gli_fb_formats_vk(gli_fb_formats *_f)
{
	gli_fb_formats_gl(_f, GLI_FB_GL, 0);
}
