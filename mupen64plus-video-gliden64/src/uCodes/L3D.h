#ifndef L3D_H
#define L3D_H
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define L3D_LINE3D				0xB5
#define L3D_TRI1				0xBF

void gli_L3D_Line3D( u32 w0, u32 w1 );
void gli_L3D_Init(void);

#ifdef __cplusplus
}
#endif

#endif

