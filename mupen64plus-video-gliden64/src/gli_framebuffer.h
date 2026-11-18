#ifndef GLI_FRAMEBUFFER_H
#define GLI_FRAMEBUFFER_H

#include <stddef.h>
#include <boolean.h>
#include "Types.h"
#include "PluginAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FrameBuffer;
struct DepthBuffer;

/* a graphics::ShaderProgram, opaque to C */
struct gli_cprog;

/* The buffer lists, for C command handlers */
void gli_fb_set_color_image(u32 _address, u32 _format, u32 _size, u32 _width);
void gli_fb_fill_rdram(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
void gli_fb_set_changed(f32 _maxY);
void gli_fb_stereo_sync(void);
void gli_fb_full_sync(bool _lle);
bool gli_fb_check_texture(u32 _address, u32 _width, u32 _bytes);
void gli_depth_set_image(u32 _address);
void gli_fb_clear_depth_buffer(struct DepthBuffer *_pDepthBuffer);
void gli_fb_attach_depth_buffer(void);
void gli_fb_set_is_depth(struct FrameBuffer *_pBuffer);
void gli_depth_set_cleared(bool _cleared);
struct FrameBuffer *gli_fb_find(u32 _address);
struct FrameBuffer *gli_fb_current(void);
void gli_fb_set_current(struct FrameBuffer *_pBuffer);
/* a buffer's plain state, NULL for none */
struct gli_fb *gli_fb_of(struct FrameBuffer *_pBuffer);
/* the current buffer as C reads it, NULL for none; whether it has a depth
 * buffer; whether a buffer starts at @_address */
const struct gli_fb *gli_fb_current_info(void);
bool gli_fb_current_has_depth(void);
bool gli_fb_is_at(u32 _address);
bool gli_fb_is_cpu(const struct FrameBuffer *_pBuffer);
void gli_fb_remove(u32 _address);
/* the buffer copied from while it is drawn to; NULL for none */
void gli_fb_set_copy_buffer(struct FrameBuffer *_pBuffer);
/* a buffer other than the current one to borrow, for a blit */
struct FrameBuffer *gli_fb_find_tmp(u32 _address);
u32 gli_fb_start_address(const struct FrameBuffer *_pBuffer);
bool gli_fb_has_texture(const struct FrameBuffer *_pBuffer);
/* copies the color image into *_ppLeftEye, made or remade to fit */
bool gli_fb_stereo_copy(struct FrameBuffer *_pBuffer, struct FrameBuffer **_ppLeftEye);
void gli_fb_stereo_delete(struct FrameBuffer *_pBuffer);
void gli_fb_delete(struct FrameBuffer *_pBuffer);
/* a buffer the size of _pMainBuffer for the post-processors to draw into */
struct FrameBuffer *gli_fb_pp_create(const struct FrameBuffer *_pMainBuffer);
/* the scales from N64 to screen coordinates of @_pBuffer */
void gli_fb_coords_scales(const struct FrameBuffer *_pBuffer, f32 *_x, f32 *_y);
void gli_fb_pp_draw(struct FrameBuffer *_pBuffer, struct FrameBuffer *_pResult, struct gli_cprog *_pShader);
void gli_fb_make_current(u32 _address);
void gli_fb_bind_draw(const struct FrameBuffer *_pBuffer);
void gli_fb_set_current_draw_buffer(void);
bool gli_fb_copy_depth(u32 _address);
u32 gli_fb_width(const struct FrameBuffer *_pBuffer);
bool gli_fb_is_valid_scanout(struct FrameBuffer *_pBuffer);
bool gli_fb_rendered_uncopied(const struct FrameBuffer *_pBuffer);
void gli_fb_copy_rdram(struct FrameBuffer *_pBuffer);
/* resolves a multisampled buffer's texture */
void gli_fb_resolve(struct FrameBuffer *_pBuffer);
bool gli_fb_depth_width_differs(u32 _address, u32 _width);
void gli_fb_vi_resized(u32 _widthPrev, u32 _width);
void gli_fb_save_cpu_buffer(u32 _address, u32 _size, u32 _width);
void gli_fb_copy_from_rdram(u32 _address, bool _useAlpha);
void gli_fb_render(void);
void gli_fb_clear_changed(void);
bool gli_FBInfo_supported(void);
/* the frame buffer list and the copies to and from RDRAM made and dropped */
void gli_fb_emulation_init(void);
void gli_fb_emulation_destroy(void);
void gli_fb_add_address(u32 _address, u32 _size);
bool gli_fb_is_depth(const struct FrameBuffer *_pBuffer);
void gli_fb_copy_depth_chunk(u32 _address);
void gli_fb_copy_chunk(u32 _address);
void gli_fb_copy_to_rdram(u32 _address, bool _sync);
void gli_fb_fill_info(void *_pinfo, u32 _size);
bool gli_depth_current(u32 *_address, u32 *_width);
void gli_window_start(void);
void gli_window_stop(void);
void gli_window_set_size(u32 _width, u32 _height);
void gli_window_destroy_gfx_context(void);
void gli_window_reinit_gfx_context(void);
bool gli_window_resize(void);
void gli_window_update_scale(void);
bool gli_window_is_adjust_screen(void);
f32 gli_window_adjust_scale(void);
u32 gli_window_swap_count(void);
u32 gli_window_width(void);
s32 gli_gfx_max_texture_size(void);
void gli_gfx_touch_error(void);
void gli_gfx_clear_color(void);
void gli_gfx_set_no_near_clip(bool _noNearClip);
/* the clamp mode: 0 clipping, 1 no near plane clipping, 2 no clipping;
 * whether polygon offset fill is on */
u32 gli_gfx_clamp_mode(void);
bool gli_gfx_polygon_offset_fill(void);

#ifdef __cplusplus
}
#endif

#endif
