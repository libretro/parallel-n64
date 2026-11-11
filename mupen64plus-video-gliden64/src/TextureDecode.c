#include <string.h>

#include <retro_inline.h>

#include "Types.h"
#include "N64.h"
#include "GBI.h"
#include "convert.h"
#include "gli_texdecode.h"
#include "gli_tex.h"

static INLINE u16 rd16(const u8 *_s, u32 _k)
{
	u16 v;
	memcpy(&v, _s + 2 * _k, 2);
	return v;
}

static INLINE u32 rd32(const u8 *_s, u32 _k)
{
	u32 v;
	memcpy(&v, _s + 4 * _k, 4);
	return v;
}

/* The first 16 bits of TLUT entry @_k in upper TMEM */
static INLINE u16 tlut16(u32 _k)
{
	u16 v;
	memcpy(&v, &TMEM[256 + _k], 2);
	return v;
}

/* The 4-bit texel at column @x */
#define NIB(s, x, i) (((x) & 1) ? ((s)[((x) >> 1) ^ ((i) << 1)] & 0x0F) : ((s)[((x) >> 1) ^ ((i) << 1)] >> 4))
#define C8(s, x, i)  ((s)[(x) ^ ((i) << 1)])
#define C16(s, x, i) rd16((s), (x) ^ (i))
#define C32(s, x, i) rd32((s), (x) ^ (i))

static INLINE u32 ci16ia_8888(u16 _tex)
{
	const u16 col = tlut16(_tex >> 8);
	const u32 c = col >> 8;
	const u32 a = col & 0xFF;
	return (a << 24) | (c << 16) | (c << 8) | c;
}

static INLINE u32 ci16ia_4444(u16 _tex)
{
	const u16 col = tlut16(_tex >> 8);
	const u32 c = col >> 12;
	const u32 a = col & 0x0F;
	return (a << 12) | (c << 8) | (c << 4) | c;
}

/* A row decoder per texel format and output width, each with its texel
 * inlined, so a texture costs no call per texel */
#define DEFINE_ROW(bits, name, EXPR) \
static void row##bits##_##name(void *_dst, const u8 *s, u32 i, u32 p, \
		u32 n, u32 clamp, u32 mask, u32 mirror) \
{ \
	u##bits *dst = (u##bits*)_dst; \
	u32 x; \
	(void)s; (void)i; (void)p; \
	for (x = 0; x < n; ++x) { \
		u32 tx = (x < clamp ? x : clamp) & mask; \
		if (x & mirror) \
			tx ^= mask; \
		dst[x] = (u##bits)(EXPR); \
	} \
}

DEFINE_ROW(16, None, 0)
DEFINE_ROW(32, None, 0)
DEFINE_ROW(16, CI4_RGBA4444, CI4_RGBA4444((u8)((p << 4) | NIB(s, tx, i))))
DEFINE_ROW(32, CI4_RGBA8888, CI4_RGBA8888((u8)((p << 4) | NIB(s, tx, i))))
DEFINE_ROW(16, CI4IA_RGBA4444, IA88_RGBA4444(tlut16((p << 4) + NIB(s, tx, i))))
DEFINE_ROW(32, CI4IA_RGBA8888, IA88_RGBA8888(tlut16((p << 4) + NIB(s, tx, i))))
DEFINE_ROW(16, CI4RGBA_RGBA5551, RGBA5551_RGBA5551(tlut16((p << 4) + NIB(s, tx, i))))
DEFINE_ROW(32, CI4RGBA_RGBA8888, RGBA5551_RGBA8888(tlut16((p << 4) + NIB(s, tx, i))))
DEFINE_ROW(16, IA31_RGBA4444, IA31_RGBA4444((u8)NIB(s, tx, i)))
DEFINE_ROW(32, IA31_RGBA8888, IA31_RGBA8888((u8)NIB(s, tx, i)))
DEFINE_ROW(16, I4_RGBA4444, I4_RGBA4444((u8)NIB(s, tx, i)))
DEFINE_ROW(32, I4_RGBA8888, I4_RGBA8888((u8)NIB(s, tx, i)))
DEFINE_ROW(16, CI8IA_RGBA4444, IA88_RGBA4444(tlut16(C8(s, tx, i))))
DEFINE_ROW(32, CI8IA_RGBA8888, IA88_RGBA8888(tlut16(C8(s, tx, i))))
DEFINE_ROW(16, CI8RGBA_RGBA5551, RGBA5551_RGBA5551(tlut16(C8(s, tx, i))))
DEFINE_ROW(32, CI8RGBA_RGBA8888, RGBA5551_RGBA8888(tlut16(C8(s, tx, i))))
DEFINE_ROW(16, IA44_RGBA4444, IA44_RGBA4444(C8(s, tx, i)))
DEFINE_ROW(32, IA44_RGBA8888, IA44_RGBA8888(C8(s, tx, i)))
DEFINE_ROW(16, I8_RGBA4444, I8_RGBA4444(C8(s, tx, i)))
DEFINE_ROW(32, I8_RGBA8888, I8_RGBA8888(C8(s, tx, i)))
DEFINE_ROW(16, CI16IA_RGBA4444, ci16ia_4444(C16(s, tx, i)))
DEFINE_ROW(32, CI16IA_RGBA8888, ci16ia_8888(C16(s, tx, i)))
DEFINE_ROW(16, CI16RGBA_RGBA5551, RGBA5551_RGBA5551(tlut16(C16(s, tx, i) & 0xFF)))
DEFINE_ROW(32, CI16RGBA_RGBA8888, RGBA5551_RGBA8888(tlut16(C16(s, tx, i) & 0xFF)))
DEFINE_ROW(16, RGBA5551_RGBA5551, RGBA5551_RGBA5551(C16(s, tx, i)))
DEFINE_ROW(32, RGBA5551_RGBA8888, RGBA5551_RGBA8888(C16(s, tx, i)))
DEFINE_ROW(16, IA88_RGBA4444, IA88_RGBA4444(C16(s, tx, i)))
DEFINE_ROW(32, IA88_RGBA8888, IA88_RGBA8888(C16(s, tx, i)))
DEFINE_ROW(16, RGBA8888_RGBA4444, RGBA8888_RGBA4444(C32(s, tx, i)))
DEFINE_ROW(32, RGBA8888_RGBA8888, C32(s, tx, i))

/* [LUT mode][texel size][texel format] */
static const gli_tex_format l_formats[4][4][5] = {
	{ /* G_TT_NONE */
		{ /* 4-bit */
			{ row16_I4_RGBA4444, row32_I4_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
			{ row16_CI4_RGBA4444, row32_CI4_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
			{ row16_IA31_RGBA4444, row32_IA31_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
			{ row16_I4_RGBA4444, row32_I4_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
		},
		{ /* 8-bit */
			{ row16_I8_RGBA4444, row32_I8_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 4096 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 4096 },
			{ row16_I8_RGBA4444, row32_I8_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 4096 },
			{ row16_IA44_RGBA4444, row32_IA44_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 3, 4096 },
			{ row16_I8_RGBA4444, row32_I8_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 4096 },
		},
		{ /* 16-bit */
			{ row16_RGBA5551_RGBA5551, row32_RGBA5551_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_IA88_RGBA4444, row32_IA88_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_IA88_RGBA4444, row32_IA88_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 2048 },
		},
		{ /* 32-bit */
			{ row16_RGBA8888_RGBA4444, row32_RGBA8888_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
		},
	},
	{ /* LUT 1 */
		{ /* 4-bit */
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
		},
		{ /* 8-bit */
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 4096 },
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
		},
		{ /* 16-bit */
			{ row16_CI16RGBA_RGBA5551, row32_RGBA5551_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 2048 },
			{ row16_CI16RGBA_RGBA5551, row32_CI16RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 2048 },
		},
		{ /* 32-bit */
			{ row16_RGBA8888_RGBA4444, row32_RGBA8888_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
		},
	},
	{ /* G_TT_RGBA16 */
		{ /* 4-bit */
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 4, 8192 },
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
			{ row16_CI4RGBA_RGBA5551, row32_CI4RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 4, 4096 },
		},
		{ /* 8-bit */
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 4096 },
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
			{ row16_CI8RGBA_RGBA5551, row32_CI8RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 3, 2048 },
		},
		{ /* 16-bit */
			{ row16_CI16RGBA_RGBA5551, row32_RGBA5551_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 2048 },
			{ row16_CI16RGBA_RGBA5551, row32_CI16RGBA_RGBA8888, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 2048 },
		},
		{ /* 32-bit */
			{ row16_RGBA8888_RGBA4444, row32_RGBA8888_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA4, 0, 1024 },
		},
	},
	{ /* G_TT_IA16 */
		{ /* 4-bit */
			{ row16_CI4IA_RGBA4444, row32_CI4IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 4, 4096 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 4, 8192 },
			{ row16_CI4IA_RGBA4444, row32_CI4IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 4, 4096 },
			{ row16_CI4IA_RGBA4444, row32_CI4IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 4, 4096 },
			{ row16_CI4IA_RGBA4444, row32_CI4IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 4, 4096 },
		},
		{ /* 8-bit */
			{ row16_CI8IA_RGBA4444, row32_CI8IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 4096 },
			{ row16_CI8IA_RGBA4444, row32_CI8IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 2048 },
			{ row16_CI8IA_RGBA4444, row32_CI8IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 2048 },
			{ row16_CI8IA_RGBA4444, row32_CI8IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 3, 2048 },
		},
		{ /* 16-bit */
			{ row16_CI16IA_RGBA4444, row32_CI16IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 2048 },
			{ row16_CI16IA_RGBA4444, row32_CI16IA_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 2048 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 2048 },
		},
		{ /* 32-bit */
			{ row16_RGBA8888_RGBA4444, row32_RGBA8888_RGBA8888, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 2, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 1024 },
			{ row16_None, row32_None, GLI_TEXT_4444, GLI_TEXF_RGBA4, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGBA8, 0, 1024 },
		},
	},
};

const gli_tex_format *gli_tex_format_get(u32 tlut, u32 size, u32 format)
{
	return &l_formats[tlut & 3][size & 3][format < 5 ? format : 0];
}

void gli_tex_decode_tmem(const struct gli_tex *t, const gli_tex_format *f,
		int out32, u32 tlut, u16 *line, void *dst)
{
	u16 mirrorSBit, maskSMask, clampSClamp;
	u16 mirrorTBit, maskTMask, clampTClamp;
	u32 y;

	if (t->maskS > 0) {
		clampSClamp = (u16)(t->clampS ? t->clampWidth - 1 : (t->mirrorS ? (t->width << 1) - 1 : t->width - 1));
		maskSMask = (u16)((1 << t->maskS) - 1);
		mirrorSBit = (u16)(t->mirrorS != 0 ? 1 << t->maskS : 0);
	} else {
		clampSClamp = (u16)(t->clampS ? t->clampWidth - 1 : t->width - 1);
		maskSMask = 0xFFFF;
		mirrorSBit = 0x0000;
	}

	if (t->maskT > 0) {
		clampTClamp = (u16)(t->clampT ? t->clampHeight - 1 : (t->mirrorT ? (t->height << 1) - 1 : t->height - 1));
		maskTMask = (u16)((1 << t->maskT) - 1);
		mirrorTBit = (u16)(t->mirrorT != 0 ? 1 << t->maskT : 0);
	} else {
		clampTClamp = (u16)(t->clampT ? t->clampHeight - 1 : t->height - 1);
		maskTMask = 0xFFFF;
		mirrorTBit = 0x0000;
	}

	if (t->size == G_IM_SIZ_32b) {
		const u16 *tmem16 = (const u16*)TMEM;
		const u32 tbase = t->tMem << 2;
		u32 *d = (u32*)dst;
		int wid_64 = t->clampWidth << 2;
		int line32;
		int width;
		if (wid_64 & 15)
			wid_64 += 16;
		wid_64 &= 0xFFFFFFF0;
		wid_64 >>= 3;
		line32 = *line << 1;
		line32 = (line32 - wid_64) << 3;
		if (wid_64 < 1)
			wid_64 = 1;
		width = wid_64 << 1;
		line32 = width + (line32 >> 2);

		for (y = 0; y < t->realHeight; ++y) {
			u16 ty = (u16)((y < clampTClamp ? y : clampTClamp) & maskTMask);
			u32 tline, xorval, x;
			if (y & mirrorTBit)
				ty ^= maskTMask;
			tline = tbase + line32 * ty;
			xorval = (ty & 1) ? 3 : 1;
			for (x = 0; x < t->realWidth; ++x) {
				u16 tx = (u16)((x < clampSClamp ? x : clampSClamp) & maskSMask);
				u32 taddr;
				if (x & mirrorSBit)
					tx ^= maskSMask;
				taddr = ((tline + tx) ^ xorval) & 0x3ff;
				*d++ = ((u32)swapword(tmem16[taddr | 0x400]) << 16) | swapword(tmem16[taddr]);
			}
		}
	} else if (t->format == G_IM_FMT_YUV) {
		u32 *d = (u32*)dst;
		*line <<= 1;
		for (y = 0; y < t->realHeight; ++y) {
			const u8 *src = (const u8*)(&TMEM[t->tMem] + *line * y);
			u32 x;
			for (x = 0; x < (u32)(t->realWidth / 2); ++x) {
				const u32 c = rd32(src, x);
				const u32 v = (c >> 8) & 0xFF;
				const u32 u = (c >> 24) & 0xFF;
				*d++ = 0xFF000000U | (((c >> 16) & 0xFF) << 16) | (v << 8) | u;
				*d++ = 0xFF000000U | ((c & 0xFF) << 16) | (v << 8) | u;
			}
		}
	} else {
		const gli_tex_row_fn row = out32 ? f->row32 : f->row16;
		const u32 tMemMask = tlut == G_TT_NONE ? 0x1FF : 0xFF;
		const u32 pitch = (u32)t->realWidth << (out32 ? 2 : 1);
		u8 *d = (u8*)dst;
		for (y = 0; y < t->realHeight; ++y, d += pitch) {
			u16 ty = (u16)((y < clampTClamp ? y : clampTClamp) & maskTMask);
			if (y & mirrorTBit)
				ty ^= maskTMask;
			row(d, (const u8*)&TMEM[(t->tMem + *line * ty) & tMemMask], (u32)(ty & 1) << 1,
				(u8)t->palette, t->realWidth, clampSClamp, maskSMask, mirrorSBit);
		}
	}
}

void gli_tex_decode_bg(const struct gli_tex *t, const gli_tex_format *f,
		int out32, const u8 *src, u32 bpl, void *dst)
{
	const gli_tex_row_fn row = out32 ? f->row32 : f->row16;
	const u32 clampS = (u16)(t->width - 1);
	const u32 clampT = (u16)(t->height - 1);
	const u32 pitch = (u32)t->realWidth << (out32 ? 2 : 1);
	u8 *d = (u8*)dst;
	u32 y;
	for (y = 0; y < t->realHeight; ++y, d += pitch)
		row(d, src + bpl * (y < clampT ? y : clampT), 0, (u8)t->palette,
			t->realWidth, clampS, 0xFFFF, 0);
}

/* N64 depth words, exponent in bits 15..13 and mantissa in 12..2, to
 * linear floats, in place: float i lands at or after word i, so working
 * back from the end never overwrites a word still to be read */
f32 *gli_tex_depth_to_float(void *buf, u32 n)
{
	static const struct { u32 shift; u32 add; } zFormat[8] = {
		{ 6, 0x00000 }, { 5, 0x20000 }, { 4, 0x30000 }, { 3, 0x38000 },
		{ 2, 0x3c000 }, { 1, 0x3e000 }, { 0, 0x3f000 }, { 0, 0x3f800 }
	};
	const u16 *w = (const u16*)buf;
	f32 *d = (f32*)buf;
	while (n--) {
		const u16 v = w[n];
		const u32 e = (v >> 13) & 7;
		const u32 m = (v >> 2) & 0x7ff;
		d[n] = (f32)((m << zFormat[e].shift) + zFormat[e].add) / 262143.0f;
	}
	return d;
}
