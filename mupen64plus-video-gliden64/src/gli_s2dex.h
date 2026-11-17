#ifndef GLI_S2DEX_H
#define GLI_S2DEX_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the S2DEX microcode does with the frame buffers, the depth buffers
 * and the drawer (uCodes/S2DEX_gfx.c) */
void gli_s2dex_mark_current_ob_screen(void);
/* the depth buffer at gSP.bgImage copied into the current color buffer's */
void gli_s2dex_copy_depth_buffer(void);
void gli_s2dex_load_fb_image(void);
int gli_s2dex_use_fb_image(u32 _imagePtr, u32 _imageSiz, u32 _imageFmt);
void gli_s2dex_background_drawing(int _on);

#ifdef __cplusplus
}
#endif

#endif
