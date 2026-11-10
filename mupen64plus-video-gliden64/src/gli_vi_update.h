#ifndef GLI_VI_UPDATE_H
#define GLI_VI_UPDATE_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the VI scans out this field */
typedef struct gli_vi_result
{
	u32 vi_vres;
	u32 vi_hres;
	u32 vi_v_start;
	u32 vi_h_start;
	u32 vi_x_start;
	u32 vi_y_start;
	u32 vi_x_add;
	u32 vi_y_add;
	u32 vi_width;
	u32 vi_origin;
	u32 vi_minhpass;
	u32 vi_maxhpass;
	u8 vi_lowerfield;
	u8 vi_fsaa;
	u8 vi_divot;
	u8 vi_ispal;
} gli_vi_result;

/* Interlace tracking carried from field to field */
typedef struct gli_vi_state
{
	s32 oldvstart;
	u32 prevvicurrent;
	s32 emucontrolsvicurrent;
	u8 prevwasblank;
	u8 prevserrate;
	u8 oldlowerfield;
} gli_vi_state;

void gli_vi_state_init(gli_vi_state *_s);

/* Reads the VI registers; false when nothing is scanned out */
int gli_vi_update(gli_vi_state *_s, gli_vi_result *_r);

/* Which part of the scanned-out buffer the VI shows */
typedef struct gli_vi_source_rect
{
	s32 srcY0, dstY0;
	s32 XoffsetLeft, XoffsetRight;
	s32 srcWidth, srcHeight;
} gli_vi_source_rect;

void gli_vi_source(const gli_vi_result *_r, u32 _bufStart, u32 _bufWidth, u32 _bufHeight,
		u32 _bufSize, f32 _bufScale, u32 _texRealHeight, gli_vi_source_rect *_o);

/* Where the shown part of the buffer goes in the window */
typedef struct gli_vi_present_in
{
	f32 bufScale;      /* the buffer's scale */
	u16 texSize[2];    /* its texture's real width, height */
	u32 windowWidth;
	u32 drawingWidth;  /* the overscan buffer's */
	s32 offset[2];     /* the overscan buffer's h, v offset */
	f32 dstScaleY;
	u32 bufferSize[2]; /* the overscan buffer's width, height */
	u8 hasNext;        /* the picture runs on into the next buffer */
	u8 immersive;      /* the picture fills the whole field of view */
} gli_vi_present_in;

typedef struct gli_vi_present_out
{
	s32 src[4], dst[4];  /* x0, y0, x1, y1 */
	s32 nextSrcY1;       /* the next buffer's rows, before its texture's clamp */
	s32 nextDstY[2];     /* where they go */
} gli_vi_present_out;

/* 0 when the shown part runs off the buffer's texture */
int gli_vi_present_rects(const gli_vi_result *_r, const gli_vi_source_rect *_s,
		const gli_vi_present_in *_in, gli_vi_present_out *_o);

#ifdef __cplusplus
}
#endif

#endif
