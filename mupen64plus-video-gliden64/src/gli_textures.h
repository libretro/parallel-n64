#ifndef GLI_TEXTURES_H
#define GLI_TEXTURES_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gli_tex;

/* The texture cache (Textures.c): the textures of the tiles, by key, and
 * the frame buffers' */
void gli_textures_init(void);
void gli_textures_destroy(void);
/* a texture of no state for a frame buffer, of @_target (GLI_TT_*) */
struct gli_tex *gli_textures_add_fb(u32 _target);
void gli_textures_remove_fb(struct gli_tex *_tex);
/* @_tex sampled by tile @_t */
void gli_textures_activate(u32 _t, struct gli_tex *_tex);
void gli_textures_activate_dummy(u32 _t);
void gli_textures_activate_ms_dummy(u32 _t);
/* the texture of tile @_t found or made, and sampled */
void gli_textures_update(u32 _t);

/* the textures the two tiles read, NULL where there is none */
extern struct gli_tex *gli_texture_current[2];
void gli_textures_current(const struct gli_tex *_cur[2]);

/* FrameBuffer.cpp: a frame buffer's texture sampled by tile @_t */
void gli_fb_activate_texture(u32 _t, u32 _address);
void gli_fb_activate_texture_bg(u32 _t, u32 _address);

#define GLI_TEX_UNIT_ZLUT    0
#define GLI_TEX_UNIT_PALETTE 1

/* frame buffer textures owned by C code */
bool gli_tex_integer_supported(void);
struct gli_tex *gli_tex_lut_create(u32 _width, u32 _height, u32 _unit, const void *_data);
void gli_tex_lut_update(struct gli_tex *_pTexture, u32 _unit, const void *_data);
struct gli_tex *gli_tex_noise_create(u32 _width, u32 _height, const void *_data);
void gli_tex_noise_bind(struct gli_tex *_pTexture);
void gli_tex_remove(struct gli_tex *_pTexture);

/* DepthBuffer.cpp */
const u16 *gli_depth_zlut(void);

#ifdef __cplusplus
}
#endif

#endif
