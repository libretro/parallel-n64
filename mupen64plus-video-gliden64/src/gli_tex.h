#ifndef GLI_TEX_H
#define GLI_TEX_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a texture is a frame buffer's, and how it is sampled */
enum { GLI_TEX_FB_NONE, GLI_TEX_FB_ONE_SAMPLE, GLI_TEX_FB_MULTI_SAMPLE };

/* A cached texture's plain state; CachedTexture derives from it, so the
 * C side reads a texture in place */
typedef struct gli_tex
{
	u64 crc;
	f32 offsetS, offsetT;
	u8 maskS, maskT;
	u8 clampS, clampT;
	u8 mirrorS, mirrorT;
	u16 line;
	u16 size;
	u16 format;
	u32 tMem;
	u32 palette;
	u16 width, height;            /* N64 width and height */
	u16 clampWidth, clampHeight;  /* size to clamp to */
	u16 realWidth, realHeight;    /* actual texture size */
	f32 scaleS, scaleT;           /* scale to map to 0.0-1.0 */
	f32 shiftScaleS, shiftScaleT; /* scale to shift */
	u32 textureBytes;
	u32 address;
	u8 max_level;
	u8 frameBufferTexture;        /* GLI_TEX_FB_* */
	u8 bHDTexture;
} gli_tex;

#ifdef __cplusplus
}
#endif

#endif
