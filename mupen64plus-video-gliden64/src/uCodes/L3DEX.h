#ifndef L3DEX_H
#define L3DEX_H
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define L3DEX_TRI1				0xBF
#define L3DEX_TRI2				0xB1
#define L3DEX_LINE3D			0xB5

void gli_L3DEX_Line3D( u32 w0, u32 w1 );
void gli_L3DEX_Init(void);

#ifdef __cplusplus
}
#endif

#endif

