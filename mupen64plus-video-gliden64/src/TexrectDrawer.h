#ifndef TEXRECTDRAWER_H
#define TEXRECTDRAWER_H

#include <boolean.h>
#include "Types.h"
#include "gSP.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FrameBuffer;

void gli_texrect_init(void);
void gli_texrect_destroy(void);
bool gli_texrect_add_rect(void);
void gli_texrect_add_background_rect(void);
bool gli_texrect_draw(void);
bool gli_texrect_is_empty(void);
bool gli_texrect_can_continue(void);

/* TexrectDrawerGpu.cpp: the GPU work */
void gli_texrect_gpu_init(void);
void gli_texrect_gpu_destroy(void);
RectVertex * gli_texrect_gpu_rect(void);
void gli_texrect_gpu_begin(u32 _width, u32 _height, s32 _sx, s32 _sy, s32 _sw, s32 _sh);
void gli_texrect_gpu_restart(void);
void gli_texrect_gpu_draw_rect(RectVertex * _pRect);
void gli_texrect_gpu_set_blend(void);
void gli_texrect_gpu_update_depth_compare(void);
void gli_texrect_gpu_scales(const struct FrameBuffer * _pBuffer, f32 * _x, f32 * _y);
void gli_texrect_gpu_begin_composite(const struct FrameBuffer * _pBuffer, int _alphaTest);
void gli_texrect_gpu_update_scissor(struct FrameBuffer * _pBuffer);
void gli_texrect_gpu_composite(RectVertex * _rect);
void gli_texrect_gpu_clear(RectVertex * _rect, u32 _width, u32 _height);

#ifdef __cplusplus
}
#endif

#endif /* TEXRECTDRAWER_H */
