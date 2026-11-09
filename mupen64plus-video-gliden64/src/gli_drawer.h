#ifndef GLI_DRAWER_H
#define GLI_DRAWER_H

#include "gSP.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The drawer's arrays, for C microcode to fill directly: one call per object,
 * none per vertex. */
SPVertex *gli_drawer_vertices(void);
SPVertex *gli_drawer_dma_vertices(u32 _atLeast);
void gli_drawer_draw_triangles(void);
void gli_drawer_draw_dma_triangles(u32 _numVtx);
void gli_drawer_draw_screen_triangles(u32 _numVtx);
SPVertex *gli_drawer_take_dma_vertices(u32 _n);
void gli_drawer_draw_dma_pending(void);
void gli_drawer_draw_screen_strip(u32 _numVtx);
void gli_drawer_clear_depth(void);
bool gli_drawer_is_rejected(s32 _v0, s32 _v1, s32 _v2);
void gli_drawer_add_triangle(s32 _v0, s32 _v1, s32 _v2);
void gli_drawer_draw_line(u32 _v0, u32 _v1, f32 _width, u32 _flag);
void gli_drawer_draw_rect(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
void gli_drawer_textured_rect(f32 _ulx, f32 _uly, f32 _lrx, f32 _lry, f32 _dsdx, f32 _dtdy, s16 _s, s16 _t, bool _flip);

/* all three vertices outside the same clip plane */
#define GLI_IS_CLIPPED(_v, _a, _b, _c) (((_v)[_a].clip & (_v)[_b].clip & (_v)[_c].clip) != 0)

#ifdef __cplusplus
}
#endif

#endif
