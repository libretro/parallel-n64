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

/* Immersive mode: whether texrect @_rect (ulx, uly, lrx, lry, N64 pixels)
 * is placed as a screen rect, and whether it covers the whole screen */
int gli_drawer_immersive_rect(const f32 *_rect, int *_fullScreen);
/* Places the four clip space vertices at @_v; @_scale is the buffer's x, y
 * coordinate scale */
void gli_drawer_immersive_clip(RectVertex *_v, int _fullScreen, const f32 *_scale);
/* Fills the four vertices of fill rect @_r (ulx, uly, lrx, lry); 0 when the
 * rect is to be dropped */
int gli_drawer_fill_rect(RectVertex *_v, const s32 *_r, const f32 *_scale, int _adjustScreen, f32 _adjustScale);

/* A textured rectangle, in N64 pixels and texels */
typedef struct gli_texrect_params
{
	f32 ulx, uly, lrx, lry;
	f32 dsdx, dtdy;
	s16 s, t;
	u8 flip, forceAjustScale, texrectCmd;
} gli_texrect_params;

/* What the texrect math needs of a cached texture */
enum { GLI_TEX_FB_NONE, GLI_TEX_FB_ONE_SAMPLE, GLI_TEX_FB_MULTI_SAMPLE };
typedef struct gli_texrect_tex
{
	f32 offset[2]; /* offsetS, offsetT */
	f32 scale[2];  /* scaleS, scaleT */
	u16 size[2];   /* N64 width, height */
	u8 fb;         /* GLI_TEX_FB_* */
	u8 maskS, mirrorS, mirrorT;
} gli_texrect_tex;

/* Fills the four vertices of textured rect @_p, positions and both tiles'
 * coordinates; @_tex[t] is NULL for a tile not sampled. Returns two bits per
 * tile, bit 2t to clamp S to the edge and bit 2t + 1 to clamp T */
u32 gli_drawer_texrect_vertices(RectVertex *_v, const gli_texrect_params *_p,
		const gli_texrect_tex *const *_tex, const f32 *_scale, int _adjustScreen, f32 _adjustScale);

/* Writes the four screen space vertices of a line from vertex @_v0 to @_v1,
 * @_halfWidth either side, into @_out; colors the ends by the shading mode,
 * flat shading from vertex @_flag */
void gli_drawer_thick_line(SPVertex *_out, u32 _v0, u32 _v1, f32 _halfWidth, u32 _flag);

/* A copy of texture pixels @src* of a @srcWidth x @srcHeight texture to
 * pixels @dst* of a @dstWidth x @dstHeight target */
typedef struct gli_copy_rect
{
	s32 srcX0, srcY0, srcX1, srcY1;
	u32 srcWidth, srcHeight;
	s32 dstX0, dstY0, dstX1, dstY1;
	u32 dstWidth, dstHeight;
	u8 invertX, invertY;
} gli_copy_rect;

/* Fills the four vertices of copy @_p: clip space positions and texture
 * coordinates s0, t0 */
void gli_drawer_copy_rect(RectVertex *_v, const gli_copy_rect *_p);

/* Texrects done on the CPU, straight into RDRAM */
/* Whether tile 0 was block loaded from the depth image */
int gli_texrect_is_depth_copy(void);
/* Writes the depth line loaded in TMEM into the color image */
void gli_texrect_depth_row(const gli_texrect_params *_p);
/* Copies a background rect into an 8-bit color image; 0 for larger ones */
int gli_texrect_bg_copy(const gli_texrect_params *_p);
/* Paper Mario's 2D lighting palette, a 16x1 rect; 0 for other rects */
int gli_texrect_palette_mod(const gli_texrect_params *_p);

/* all three vertices outside the same clip plane */
#define GLI_IS_CLIPPED(_v, _a, _b, _c) (((_v)[_a].clip & (_v)[_b].clip & (_v)[_c].clip) != 0)

#ifdef __cplusplus
}
#endif

#endif
