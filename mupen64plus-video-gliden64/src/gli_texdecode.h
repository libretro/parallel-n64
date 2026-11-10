#ifndef GLI_TEXDECODE_H
#define GLI_TEXDECODE_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Texture internal formats and data types, mapped to graphics
 * parameters by the C++ side */
enum { GLI_TEXF_RGBA4, GLI_TEXF_RGB5A1, GLI_TEXF_RGBA8 };
enum { GLI_TEXT_4444, GLI_TEXT_5551, GLI_TEXT_U8 };

/* One row of texels: @n texels into @dst (u16 or u32 by the decoder),
 * the texel for column x taken at min(x, clamp) & mask, mirrored when
 * x & mirror */
typedef void (*gli_tex_row_fn)(void *dst, const u8 *src, u32 i, u32 palette,
		u32 n, u32 clamp, u32 mask, u32 mirror);

typedef struct gli_tex_format
{
	gli_tex_row_fn row16;
	gli_tex_row_fn row32;
	u8  type16, internal16;
	u8  type32, internal32;
	u8  autoFormat;
	u8  lineShift;
	u16 maxTexels;
} gli_tex_format;

/* The format for texture LUT mode @tlut, texel @size and @format */
const gli_tex_format *gli_tex_format_get(u32 tlut, u32 size, u32 format);

/* The texture to decode out of TMEM */
typedef struct gli_tex_decode
{
	u32 tMem;
	u32 palette;
	u16 size, format;
	u16 width, height;
	u16 clampWidth, clampHeight;
	u16 realWidth, realHeight;
	u8  maskS, maskT;
	u8  clampS, clampT;
	u8  mirrorS, mirrorT;
} gli_tex_decode;

/* Decodes @t from TMEM into @dst: u32 texels when @out32, else u16.
 * @line is the TMEM line, which YUV doubles */
void gli_tex_decode_tmem(const gli_tex_decode *t, const gli_tex_format *f,
		int out32, u32 tlut, u16 *line, void *dst);

/* Decodes a background image of @bpl bytes per line from @src */
void gli_tex_decode_bg(const gli_tex_decode *t, const gli_tex_format *f,
		int out32, const u8 *src, u32 bpl, void *dst);

#ifdef __cplusplus
}
#endif

#endif
