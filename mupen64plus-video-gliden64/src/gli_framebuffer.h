#ifndef GLI_FRAMEBUFFER_H
#define GLI_FRAMEBUFFER_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The buffer lists, for C command handlers */
void gli_fb_set_color_image(u32 _address, u32 _format, u32 _size, u32 _width);
void gli_fb_fill_rdram(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
void gli_fb_set_changed(f32 _maxY);
void gli_fb_stereo_sync(void);
void gli_fb_full_sync(bool _lle);
bool gli_fb_check_texture(u32 _address, u32 _width, u32 _bytes);
void gli_depth_set_image(u32 _address);
void gli_depth_set_cleared(void);
struct FrameBuffer *gli_fb_find(u32 _address);
u32 gli_fb_width(const struct FrameBuffer *_pBuffer);
bool gli_fb_is_valid_scanout(struct FrameBuffer *_pBuffer);
bool gli_fb_rendered_uncopied(const struct FrameBuffer *_pBuffer);
void gli_fb_copy_rdram(struct FrameBuffer *_pBuffer);
bool gli_fb_depth_width_differs(u32 _address, u32 _width);
void gli_fb_vi_resized(u32 _widthPrev, u32 _width);
void gli_fb_save_cpu_buffer(u32 _address, u32 _size, u32 _width);
void gli_fb_copy_from_rdram(u32 _address, bool _useAlpha);
void gli_fb_render(void);
void gli_fb_clear_changed(void);
bool gli_FBInfo_supported(void);
bool gli_window_change(void);
bool gli_window_resize(void);
void gli_window_screenshot(void);
void gli_window_update_scale(void);
void gli_gfx_touch_error(void);
void gli_gfx_clear_color(void);

#ifdef __cplusplus
}
#endif

#endif
