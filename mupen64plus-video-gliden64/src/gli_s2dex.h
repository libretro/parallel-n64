#ifndef GLI_S2DEX_H
#define GLI_S2DEX_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the S2DEX microcode needs from the C++ frame buffer lists and config. */
u32 gli_s2dex_hacks(void);
int gli_s2dex_bg_one_piece(void);
int gli_s2dex_native_res_texrects(void);
void gli_s2dex_mark_current_ob_screen(void);
int gli_s2dex_depth_buffer_at(u32 _address);
void gli_s2dex_copy_depth_buffer(void);
void gli_s2dex_load_fb_image(void);
int gli_s2dex_use_fb_image(u32 _imagePtr, u32 _imageSiz, u32 _imageFmt);
void gli_s2dex_background_drawing(int _on);

#ifdef __cplusplus
}
#endif

#endif
