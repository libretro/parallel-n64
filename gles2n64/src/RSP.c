#include <math.h>
#include "Common.h"
#include "gles2N64.h"
#include "OpenGL.h"
#include "Debug.h"
#include "RSP.h"
#include "RDP.h"
#include "N64.h"
#include "F3D.h"
#include "3DMath.h"
#include "VI.h"
#include "ShaderCombiner.h"
#include "DepthBuffer.h"
#include "GBI.h"
#include "gSP.h"
#include "Textures.h"

RSPInfo     RSP;

void RSP_LoadMatrix( f32 mtx[4][4], u32 address )
{
   int i, j;
   f32 recip = 1.5258789e-05f;

   struct _N64Matrix
   {
      s16 integer[4][4];
      u16 fraction[4][4];
   } *n64Mat = (struct _N64Matrix *)&gfx_info.RDRAM[address];

   for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
         mtx[i][j] = (GLfloat)(n64Mat->integer[i][j^1]) + (GLfloat)(n64Mat->fraction[i][j^1]) * recip;
}

void RSP_ProcessDList(void)
{
   int i, j;
   u32 uc_start, uc_dstart, uc_dsize;

   VI_UpdateSize();
   OGL_UpdateScale();
   TextureCache_ActivateNoise(2);

   RSP.PC[0] = *(u32*)&gfx_info.DMEM[0x0FF0];
   RSP.PCi = 0;
   RSP.count = 0;

   RSP.halt = FALSE;
   RSP.busy = TRUE;

   gSP.matrix.stackSize = min( 32, *(u32*)&gfx_info.DMEM[0x0FE4] >> 6 );
   gSP.matrix.modelViewi = 0;
   gSP.changed |= CHANGED_MATRIX;

   for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
         gSP.matrix.modelView[0][i][j] = 0.0f;

   gSP.matrix.modelView[0][0][0] = 1.0f;
   gSP.matrix.modelView[0][1][1] = 1.0f;
   gSP.matrix.modelView[0][2][2] = 1.0f;
   gSP.matrix.modelView[0][3][3] = 1.0f;

   uc_start = *(u32*)&gfx_info.DMEM[0x0FD0];
   uc_dstart = *(u32*)&gfx_info.DMEM[0x0FD8];
   uc_dsize = *(u32*)&gfx_info.DMEM[0x0FDC];

   if ((uc_start != RSP.uc_start) || (uc_dstart != RSP.uc_dstart))
      gSPLoadUcodeEx( uc_start, uc_dstart, uc_dsize );

   gDPSetAlphaCompare(G_AC_NONE);
   gDPSetDepthSource(G_ZS_PIXEL);
   gDPSetRenderMode(0, 0);
   gDPSetAlphaDither(G_AD_DISABLE);
   gDPSetColorDither(G_CD_DISABLE);
   gDPSetCombineKey(G_CK_NONE);
   gDPSetTextureConvert(G_TC_FILT);
   gDPSetTextureFilter(G_TF_POINT);
   gDPSetTextureLUT(G_TT_NONE);
   gDPSetTextureLOD(G_TL_TILE);
   gDPSetTextureDetail(G_TD_CLAMP);
   gDPSetTexturePersp(G_TP_PERSP);
   gDPSetCycleType(G_CYC_1CYCLE);
   gDPPipelineMode(G_PM_NPRIMITIVE);

   while (!RSP.halt)
   {
	  u32 w0, w1, pc;
      pc = RSP.PC[RSP.PCi];

      if ((pc + 8) > RDRAMSize)
      {
#ifdef DEBUG
         DebugMsg( DEBUG_LOW | DEBUG_ERROR, "ATTEMPTING TO EXECUTE RSP COMMAND AT INVALID RDRAM LOCATION\n" );
#endif
         break;
      }


      w0 = *(u32*)&gfx_info.RDRAM[pc];
      w1 = *(u32*)&gfx_info.RDRAM[pc+4];
      RSP.nextCmd = _SHIFTR( *(u32*)&gfx_info.RDRAM[pc+8], 24, 8 );
      RSP.cmd = _SHIFTR( w0, 24, 8 );
      RSP.PC[RSP.PCi] += 8;

      GBI.cmd[RSP.cmd]( w0, w1 );

   }

   RSP.busy = FALSE;
   RSP.DList++;
   gSP.changed |= CHANGED_COLORBUFFER;
}

void RSP_Init(void)
{
   RDRAMSize = 1024 * 1024 * 8;
   RSP.DList = 0;
   RSP.uc_start = RSP.uc_dstart = 0;
   gDP.loadTile = &gDP.tiles[7];
   gSP.textureTile[0] = &gDP.tiles[0];
   gSP.textureTile[1] = &gDP.tiles[1];

   DepthBuffer_Init();
   GBI_Init();
}
