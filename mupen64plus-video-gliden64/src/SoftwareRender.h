#ifndef SOFTWARE_RENDER_H
#define SOFTWARE_RENDER_H

#include "gSP.h"

#ifdef __cplusplus
extern "C" {
#endif

f32 gli_renderTriangles(const SPVertex * _pVertices, const u16 * _pElements, u32 _numElements);

/* Lends the depth LUT and the buffer width when triangles go to the software
 * depth renderer; false when they don't. */
bool gli_depthbuffer_software_target(const u16 ** _zLUT, u32 * _width);

#ifdef __cplusplus
}
#endif

#endif /* SOFTWARE_RENDER_H */
