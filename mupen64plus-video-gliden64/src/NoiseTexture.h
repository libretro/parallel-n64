#ifndef NOISETEXTURE_H
#define NOISETEXTURE_H

#include "Types.h"

#define NOISE_TEX_WIDTH 640
#define NOISE_TEX_HEIGHT 580

#ifdef __cplusplus
extern "C" {
#endif

void gli_noise_init(void);
void gli_noise_destroy(void);
void gli_noise_update(void);
u32 gli_noise_offset_x(void);
u32 gli_noise_offset_y(void);

#ifdef __cplusplus
}
#endif

#endif
