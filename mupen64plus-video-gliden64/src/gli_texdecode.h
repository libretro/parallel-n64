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

struct gli_tex;

/* Decodes texture @t from TMEM into @dst: u32 texels when @out32, else u16.
 * @line is the TMEM line, which YUV doubles */
void gli_tex_decode_tmem(const struct gli_tex *t, const gli_tex_format *f,
		int out32, u32 tlut, u16 *line, void *dst);

/* Decodes a background image of @bpl bytes per line straight out of
 * RDRAM at @address, reading only the rows it shows */
void gli_tex_decode_bg(const struct gli_tex *t, const gli_tex_format *f,
		int out32, u32 address, u32 bpl, void *dst);
/* The same for @rows rows from row @y0 only, packed from @dst on: a big
 * image goes up in strips through a buffer of a few rows */
void gli_tex_decode_bg_rows(const struct gli_tex *t, const gli_tex_format *f,
		int out32, u32 address, u32 bpl, u32 y0, u32 rows, void *dst);

/* Turns @n N64 depth words at @buf into linear floats in place; @buf
 * must hold n floats */
f32 *gli_tex_depth_to_float(void *buf, u32 n);

/* The sizes of tile @_t, as the RDP will sample it */
typedef struct gli_tile_sizes
{
	u32 maskWidth, clampWidth, width, realWidth;
	u32 maskHeight, clampHeight, height, realHeight;
	u32 bytes;
} gli_tile_sizes;

struct gDPTile;

/* Sizes tile @_t (a texture tile when < 2) after load tile @_pLoadTile;
 * resets and may shrink the tile's masks */
void gli_tex_tile_sizes(u32 _t, gli_tile_sizes *_sizes, struct gDPTile *_pLoadTile);

struct gli_tex;

/* Describes tile @_tile of @_sizes in texture @_tex, ahead of its load */
void gli_tex_from_tile(struct gli_tex *_tex, const struct gDPTile *_tile, const gli_tile_sizes *_sizes);

/* The mip levels past the first that tile @_tile of texture @_tex has */
u8 gli_tex_max_level(const struct gli_tex *_tex, u32 _tile);
/* Steps @_tex, a copy of a texture, to its mip level @_mipLevel, with its
 * TMEM @_line; returns the level's bytes at 1 << @_sizeShift per texel */
u32 gli_tex_mip_step(struct gli_tex *_tex, u32 _mipLevel, u16 *_line, u32 _sizeShift);
/* Whether texture filtering leaves this load alone as a background */
int gli_tex_filter_skips(void);

/* The cache key of texture tile @_t with @_sizes */
u64 gli_tex_crc(u32 _t, const gli_tile_sizes *_sizes);

/* The texel line width of a block load from its dxt step */
u32 gli_tex_reverse_dxt(u32 val, u32 lrs, u32 width, u32 size);

#ifdef __cplusplus
}
#endif

#endif
