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

#ifdef __cplusplus
}
#endif

#endif
