#ifndef GLI_TEXTURES_H
#define GLI_TEXTURES_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CachedTexture;

#define GLI_TEX_UNIT_ZLUT    0
#define GLI_TEX_UNIT_PALETTE 1

/* Textures.cpp: frame buffer textures owned by C code */
bool gli_tex_integer_supported(void);
struct CachedTexture *gli_tex_lut_create(u32 _width, u32 _height, u32 _unit, const void *_data);
void gli_tex_lut_update(struct CachedTexture *_pTexture, u32 _unit, const void *_data);
struct CachedTexture *gli_tex_noise_create(u32 _width, u32 _height, const void *_data);
void gli_tex_noise_bind(struct CachedTexture *_pTexture);
void gli_tex_remove(struct CachedTexture *_pTexture);

/* DepthBuffer.cpp */
const u16 *gli_depth_zlut(void);

#ifdef __cplusplus
}
#endif

#endif
