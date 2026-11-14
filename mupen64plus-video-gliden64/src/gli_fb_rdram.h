#ifndef GLI_FB_RDRAM_H
#define GLI_FB_RDRAM_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A frame buffer's plain state; FrameBuffer derives from it, so the C
 * side reads a buffer in place */
typedef struct gli_fb
{
	u32 m_startAddress;
	u32 m_endAddress;
	u32 m_size;
	u32 m_width;
	u32 m_height;
	u32 m_originX;
	u32 m_originY;
	u32 m_swapCount;
	f32 m_scale;
	u8 m_copiedToRdram;
	u8 m_fingerprint;
	u8 m_cleared;
	u8 m_changed;
	u8 m_cfb;
	u8 m_isDepthBuffer;
	u8 m_isPauseScreen;
	u8 m_isOBScreen;
	u8 m_isMainBuffer;
	u8 m_readable;
	u8 m_copied;
	u8 m_resolved;
	struct
	{
		u32 uls, ult;
	} m_loadTileOrigin;
	u32 m_loadType;
	/* the last clear, which a validity check expects in RDRAM */
	struct
	{
		u32 fillcolor;
		s32 ulx, uly, lrx, lry;
	} m_clearParams;
	/* the swap the validity was last checked in */
	u32 m_validityChecked;
	/* RDRAM as the buffer left it, for the validity check, as much as
	 * its tallest texture holds: lent by the frame buffer list from its
	 * slot's storage, which outlives the buffer */
	u8 *m_RdramCopy;
	u32 m_RdramCopySize;
	u32 m_RdramCopyCapacity;
} gli_fb;

/* Copies, or for an auxiliary buffer marks, the RDRAM @_fb covers, up
 * to its copy's capacity */
void gli_fb_save_rdram(gli_fb *_fb);
/* Forgets what @_fb left in RDRAM */
void gli_fb_set_dirty(gli_fb *_fb);

/* Ends @_fb at its last byte in RDRAM */
void gli_fb_update_end_address(gli_fb *_fb);
/* Whether @_fb is narrower or wider than the VI shows */
int gli_fb_is_auxiliary(const gli_fb *_fb);
/* Notes a clear of @_fb, for the validity check */
void gli_fb_set_clear_params(gli_fb *_fb, u32 _fillcolor, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
/* Whether RDRAM still holds what @_fb left there: its clear, its mark or
 * its copy. Unless @_force, once per swap @_swapCount */
int gli_fb_is_valid(gli_fb *_fb, int _force, u32 _swapCount);

/* How many of @_height lines of @_stride bytes at @_address fit in RDRAM */
u32 cutHeight(u32 _address, u32 _height, u32 _stride);

/* Marks an auxiliary buffer's RDRAM so a game restoring the old content
 * cannot make it look untouched */
void gli_fb_rdram_mark(u32 _startAddress, u32 _dataSize);

/* Whether RDRAM still holds what the buffer left there: within 1% of
 * the dwords for a clear or a copy, all of the mark */
int gli_fb_rdram_cleared_valid(const gli_fb *_fb);
int gli_fb_rdram_mark_valid(u32 _startAddress);
int gli_fb_rdram_copy_valid(u32 _startAddress, const u8 *_copy, u32 _size);

/* Fills the color image with the fill color over a rectangle clamped to
 * the scissor; @_rect gets the dword rectangle filled (ulx, uly, lrx, lry) */
void gli_fb_rdram_fill(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry, s32 *_rect);

/* A fill of @_fb, the color image: into RDRAM, noted for the validity
 * check. Not where @_fb's last asynchronous colour copy is in RDRAM and
 * nothing has been drawn since: the copy of what is drawn now replaces
 * it whole, and until then a game reading the buffer back gets the last
 * picture rather than the fill. Returns whether it filled. */
int gli_fb_rdram_fill_buffer(gli_fb *_fb, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);

/* Whether buffer 1, ending inside buffer 2, only runs a few lines into it */
int gli_fb_overlapping(const gli_fb *_fb1, const gli_fb *_fb2);

/* Whether color image @_address of @_width and @_size, drawn in swap
 * @_swapCount, lies inside buffer @_fb off its line starts or more than
 * five lines before its end */
int gli_fb_is_sub_buffer(const gli_fb *_fb, u32 _swapCount, u32 _address, u32 _width, u32 _size);

/* Whether buffer @_fb runs under five lines into the color image at
 * @_address */
int gli_fb_runs_into(const gli_fb *_fb, u32 _address, u32 _width, u32 _size);

/* What becomes of buffer @_fb when the color image moves to @_address
 * of @_width and @_size at @_scale in swap @_swapCount */
enum
{
	GLI_FB_REUSE_KEEP,     /* the same image: draw on into it */
	GLI_FB_REUSE_SUB,      /* the image lies inside it: drawn at its origin */
	GLI_FB_REUSE_TRUNCATE, /* it runs a few lines into the image: cut short */
	GLI_FB_REUSE_REMOVE    /* it no longer matches */
};
/* Sets @_fb's origin, or its end when truncated, for the outcome */
int gli_fb_reuse(gli_fb *_fb, u32 _swapCount, u32 _address, u32 _width, u32 _size, f32 _scale);

/* A texture load from @_address of @_width and @_bytes: the load tile
 * reads plain RDRAM until a frame buffer is found to texture from */
void gli_fb_texture_reset(void);
/* Whether the load tile textures from buffer @_fb, found at @_address,
 * with @_current the buffer drawn to; sets *@_remove when @_fb is to go */
int gli_fb_texture_use(gli_fb *_fb, gli_fb *_current, u32 _address, u32 _width,
		u32 _bytes, u32 _swapCount, int *_remove);
/* The load tile's mode to the tiles that share its TMEM */
void gli_fb_texture_tiles(void);

/* Where color image @_address starts inside the buffer at @_bufStart */
void gli_fb_sub_origin(u32 _bufStart, u32 _address, u32 _width, u32 _size, u32 *_originX, u32 *_originY);

#ifdef __cplusplus
}
#endif

#endif
