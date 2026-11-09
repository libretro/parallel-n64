/***************************************************************** */
/* */
/* Software rendering to N64 depth buffer */
/* Created by Gonetz, Dec 2004 */
/* */
/***************************************************************** */

#ifndef DEPTH_BUFFER_RENDER_H
#define DEPTH_BUFFER_RENDER_H

#include "Types.h"

typedef struct vertexi
{
	int x, y;      /* Screen position in 16:16 bit fixed point */
	int z;         /* z value in 16:16 bit fixed point */
} vertexi;

#ifdef __cplusplus
extern "C" {
#endif


void gli_Rasterize(vertexi * vtx, int vertices, int dzdx, const u16 * zLUT, u32 depthBufferWidth);

#ifdef __cplusplus
}
#endif

#endif /*DEPTH_BUFFER_RENDER_H */
