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
/* Whether the window's adjust-screen setting applies to the current image */
int gli_drawer_need_adjust(int _adjustScreen);
/* The scissor and viewport rectangles (x, y, width, height) for a buffer
 * drawn at @_scale with its origin at @_offset */
void gli_drawer_scissor_rect(f32 _scaleX, f32 _scaleY, f32 _offsetX, f32 _offsetY,
		int _adjust, f32 _adjustScale, int _immersive, s32 *_rect);
void gli_drawer_viewport_rect(int _buffer, f32 _scaleX, f32 _scaleY, u32 _originX, u32 _originY,
		int _adjust, f32 _adjustScale, s32 *_rect);
/* Hands the drawer's triangle list to the C side, once */
void gli_drawer_bind_triangles(SPVertex *_vertices, u16 *_elements, u32 *_num,
		s32 *_maxElement, u32 *_modify, const bool *_clipControl);
void gli_drawer_draw_line(u32 _v0, u32 _v1, f32 _width, u32 _flag);
void gli_drawer_draw_rect(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
void gli_drawer_textured_rect(f32 _ulx, f32 _uly, f32 _lrx, f32 _lry, f32 _dsdx, f32 _dtdy, s16 _s, s16 _t, bool _flip);

/* The cull face for the current geometry mode */
enum { GLI_CULL_NONE, GLI_CULL_FRONT, GLI_CULL_BACK, GLI_CULL_FRONT_AND_BACK };
int gli_drawer_cull_mode(void);

/* The depth state to apply: only the fields named in @set */
enum { GLI_DS_TEST = 1, GLI_DS_WRITE = 2, GLI_DS_COMPARE = 4, GLI_DS_CLAMP = 8 };
enum { GLI_CMP_LESS, GLI_CMP_LEQUAL, GLI_CMP_ALWAYS };
typedef struct gli_depth_state
{
	u8 set;
	u8 test, write;
	u8 offset, compare; /* polygon offset goes with the compare */
	u8 clipping;
} gli_depth_state;
void gli_drawer_depth_state(gli_depth_state *_out);

/* Snaps or grows texrect @_rect (ulx, uly, lrx, lry) by the config, against
 * the previous one at @_prev, which then becomes @_rect */
void gli_drawer_texrect_correct(f32 *_rect, f32 *_prev);

/* all three vertices outside the same clip plane */
#define GLI_IS_CLIPPED(_v, _a, _b, _c) (((_v)[_a].clip & (_v)[_b].clip & (_v)[_c].clip) != 0)

#ifdef __cplusplus
}
#endif

#endif
