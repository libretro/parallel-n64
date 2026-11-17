/* test_gliden64_fb_formats -- the formats of GLideN64's frame buffer
 * textures, for each backend.
 *
 * They were C++ classes, one for each kind of GL and one for Vulkan, each
 * setting its formats in a virtual init; they are tables in C now
 * (Graphics/FramebufferFormats.c). Each kind must set what its class set,
 * in GL's numbers as written there, and GLES 2 leave the depth image and
 * the LUTs it has not unset.
 *
 *   ./test_gliden64_fb_formats
 */
#include <stdio.h>
#include <string.h>

#include "Graphics/gli_context.h"

static int failures;

static void expect(const char *_kind, const char *_what, const u32 *_got,
	u32 _internal, u32 _format, u32 _type, u32 _bytes)
{
	if (_got[0] != _internal || _got[1] != _format || _got[2] != _type || _got[3] != _bytes) {
		printf("FAIL: %s %s: %#x %#x %#x %u, wanted %#x %#x %#x %u\n", _kind, _what,
			_got[0], _got[1], _got[2], _got[3], _internal, _format, _type, _bytes);
		failures++;
	}
}

#define EXPECT(f, kind, what, i, fo, ty, by) do { \
	const u32 got[4] = { (f).what##InternalFormat, (f).what##Format, (f).what##Type, (f).what##FormatBytes }; \
	expect(kind, #what, got, i, fo, ty, by); } while (0)


static gli_fb_formats made(u32 _api, u32 _flags, int _vk)
{
	static const gli_fb_formats unset = GLI_FB_FORMATS_DEFAULT;
	gli_fb_formats f = unset;
	if (_vk)
		gli_fb_formats_vk(&f);
	else
		gli_fb_formats_gl(&f, _api, _flags);
	return f;
}

/* desktop GL's, and Vulkan's, which are the same numbers */
static void check_desktop(const char *_kind, gli_fb_formats f)
{
	EXPECT(f, _kind, color, 0x8058, 0x1908, 0x1401, 4);
	EXPECT(f, _kind, monochrome, 0x8229, 0x1903, 0x1401, 1);
	EXPECT(f, _kind, depth, 0x81A6, 0x1902, 0x1406, 4);
	EXPECT(f, _kind, depthImage, 0x822E, 0x1903, 0x1406, 4);
	EXPECT(f, _kind, lut, 0x8234, 0x8D94, 0x1403, 2);
	EXPECT(f, _kind, noise, 0x8229, 0x1903, 0x1401, 1);
}

static void check_gles3(const char *_kind, gli_fb_formats f, int _adreno)
{
	if (_adreno)
		EXPECT(f, _kind, color, 0x8814, 0x1908, 0x1406, 16);
	else
		EXPECT(f, _kind, color, 0x8058, 0x1908, 0x1401, 4);
	EXPECT(f, _kind, monochrome, 0x8229, 0x1903, 0x1401, 1);
	EXPECT(f, _kind, depth, 0x81A6, 0x1902, 0x1405, 4);
	EXPECT(f, _kind, depthImage, 0x822E, 0x1903, 0x1406, 4);
	EXPECT(f, _kind, lut, 0x8234, 0x8D94, 0x1403, 2);
	EXPECT(f, _kind, noise, 0x8229, 0x1903, 0x1401, 1);
}

static void check_gles2(const char *_kind, gli_fb_formats f, int _depth, int _rgba8)
{
	if (_rgba8)
		EXPECT(f, _kind, color, 0x1908, 0x1908, 0x1401, 4);
	else
		EXPECT(f, _kind, color, 0x1907, 0x1907, 0x8363, 2);
	EXPECT(f, _kind, monochrome, 0x1907, 0x1907, 0x8363, 2);
	if (_depth)
		EXPECT(f, _kind, depth, 0x1902, 0x1902, 0x1405, 4);
	else
		EXPECT(f, _kind, depth, 0x81A5, 0x1902, 0x1405, 2);
	EXPECT(f, _kind, depthImage, GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0);
	EXPECT(f, _kind, lut, GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0);
	EXPECT(f, _kind, noise, 0x1909, 0x1909, 0x1401, 1);
}

int main(void)
{
	check_desktop("gl", made(GLI_FB_GL, 0, 0));
	/* the GLES flags mean nothing to desktop GL */
	check_desktop("gl+flags", made(GLI_FB_GL, GLI_FB_ADRENO530 | GLI_FB_OES_DEPTH | GLI_FB_OES_RGBA8, 0));
	check_desktop("vulkan", made(0, 0, 1));
	check_gles3("gles3", made(GLI_FB_GLES3, 0, 0), 0);
	check_gles3("gles3 adreno530", made(GLI_FB_GLES3, GLI_FB_ADRENO530, 0), 1);
	check_gles2("gles2", made(GLI_FB_GLES2, 0, 0), 0, 0);
	check_gles2("gles2 depth", made(GLI_FB_GLES2, GLI_FB_OES_DEPTH, 0), 1, 0);
	check_gles2("gles2 rgba8", made(GLI_FB_GLES2, GLI_FB_OES_RGBA8, 0), 0, 1);
	check_gles2("gles2 both", made(GLI_FB_GLES2, GLI_FB_OES_DEPTH | GLI_FB_OES_RGBA8, 0), 1, 1);

	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("frame buffer formats: ok\n");
	return 0;
}
