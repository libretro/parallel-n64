#ifndef S2DEX_H
#define S2DEX_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void gli_S2DEX_BG_1Cyc(u32 w0, u32 w1);
void gli_S2DEX_BG_Copy( u32 w0, u32 w1 );
void gli_S2DEX_Obj_Rectangle( u32 w0, u32 w1 );
void gli_S2DEX_Obj_Sprite( u32 w0, u32 w1 );
void gli_S2DEX_Obj_MoveMem( u32 w0, u32 w1 );
void gli_S2DEX_RDPHalf_0( u32 w0, u32 w1 );
void gli_S2DEX_Select_DL( u32 w0, u32 w1 );
void gli_S2DEX_Obj_RenderMode( u32 w0, u32 w1 );
void gli_S2DEX_Obj_Rectangle_R( u32 w0, u32 w1 );
void gli_S2DEX_Obj_LoadTxtr( u32 w0, u32 w1 );
void gli_S2DEX_Obj_LdTx_Sprite( u32 w0, u32 w1 );
void gli_S2DEX_Obj_LdTx_Rect( u32 w0, u32 w1 );
void gli_S2DEX_Obj_LdTx_Rect_R( u32 w0, u32 w1 );
void gli_S2DEX_1_03_Init(void);
void gli_S2DEX_1_05_Init(void);
void gli_S2DEX_1_07_Init(void);
void gli_resetObjMtx(void);

#ifdef __cplusplus
}
#endif

#endif
