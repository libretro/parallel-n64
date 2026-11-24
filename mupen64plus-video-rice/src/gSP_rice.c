/* Rice's RSP commands that Graphics/ dispatches: vertices modified and
 * loaded (Perfect Dark's coloured ones), lights, DMA triangles and the
 * viewport. */
#include <stdint.h>
#include <math.h>

#include <retro_miscellaneous.h>

#include "osal_preproc.h"
#include "Config.h"
#include "RenderBase.h"
#include "Video.h"

#include "../../Graphics/RSP/gSP_funcs_prot.h"
#include "../../Graphics/GBI.h"

/* (x, y, z, 0) of vec through m, then normalized */
static void Vec3TransformNormal(XVECTOR4 *vec, const Matrix *m)
{
   VECTOR3 temp;
   float norm;

   temp.x = (vec->x * m->_11) + (vec->y * m->_21) + (vec->z * m->_31);
   temp.y = (vec->x * m->_12) + (vec->y * m->_22) + (vec->z * m->_32);
   temp.z = (vec->x * m->_13) + (vec->y * m->_23) + (vec->z * m->_33);
   norm = sqrt(temp.x*temp.x+temp.y*temp.y+temp.z*temp.z);
   if (norm == 0.0)
   {
      vec->x = 0.0;
      vec->y = 0.0;
      vec->z = 0.0;
   }
   else
   {
      vec->x = temp.x/norm;
      vec->y = temp.y/norm;
      vec->z = temp.z/norm;
   }
}

void ricegSPModifyVertex(uint32_t vtx, uint32_t where, uint32_t val)
{
   switch (where)
   {
      case G_MWO_POINT_RGBA:
         {
            uint32_t r = (val>>24)&0xFF;
            uint32_t g = (val>>16)&0xFF;
            uint32_t b = (val>>8)&0xFF;
            uint32_t a = val&0xFF;
            g_dwVtxDifColor[vtx] = COLOR_RGBA(r, g, b, a);
         }
         break;
      case G_MWO_POINT_ST:
         {
            int16_t tu = (int16_t)(val>>16);
            int16_t tv = (int16_t)(val & 0xFFFF);
            float ftu  = tu / 32.0f;
            float ftv  = tv / 32.0f;
            g_fVtxTxtCoords[vtx].x = ftu/gRSP.fTexScaleX;
            g_fVtxTxtCoords[vtx].y = ftv/gRSP.fTexScaleY;
         }
         break;
      case G_MWO_POINT_XYSCREEN:
         {
            int16_t x = (int16_t)(uint16_t)(val>>16);
            int16_t y = (int16_t)(uint16_t)(val&0xFFFF);
            x /= 4;
            y /= 4;

            /* Should do viewport transform. */
            x -= windowSetting.uViWidth/2;
            y = windowSetting.uViHeight/2-y;

            if( options.bEnableHacks && ((*gfx_info.VI_X_SCALE_REG)&0xF) != 0 )
            {
               /* Tarzan: I don't know why Tarzan is different */
               SetVertexXYZ(vtx, x/windowSetting.fViWidth, y/windowSetting.fViHeight, g_vecProjected[vtx].z);
            }
            else
            {
               /* Toy Story 2 and other games */
               SetVertexXYZ(vtx, x*2/windowSetting.fViWidth, y*2/windowSetting.fViHeight, g_vecProjected[vtx].z);
            }
         }
         break;
      case G_MWO_POINT_ZSCREEN:
         {
            int z = val>>16;

            SetVertexXYZ(vtx, g_vecProjected[vtx].x, g_vecProjected[vtx].y, (((float)z/0x03FF)+0.5f)/2.0f );
         }
         break;
   }
}

void ricegSPCIVertex(uint32_t v, uint32_t n, uint32_t v0)
{
   uint8_t *rdram_u8;
   N64VtxPD *pVtxBase;
   uint32_t i;

   UpdateCombinedMatrix();

   rdram_u8   = (uint8_t*)gfx_info.RDRAM;
   pVtxBase   = (N64VtxPD*)(rdram_u8 + v);
   g_pVtxBase = (FiddledVtx*)pVtxBase; /* Fix me */

   for (i = v0; i < v0 + n; i++)
   {
      const N64VtxPD *vert = &pVtxBase[i - v0];
      uint8_t *addr;
      uint32_t a, r, g, b;

      g_vtxNonTransformed[i].x = (float)vert->x;
      g_vtxNonTransformed[i].y = (float)vert->y;
      g_vtxNonTransformed[i].z = (float)vert->z;

      Vec3Transform(&g_vtxTransformed[i], (XVECTOR3*)&g_vtxNonTransformed[i], &gRSPworldProject); /* Convert to w=1 */
      g_vecProjected[i].w = 1.0f / g_vtxTransformed[i].w;
      g_vecProjected[i].x = g_vtxTransformed[i].x * g_vecProjected[i].w;
      g_vecProjected[i].y = g_vtxTransformed[i].y * g_vecProjected[i].w;
      g_vecProjected[i].z = g_vtxTransformed[i].z * g_vecProjected[i].w;

      g_fFogCoord[i] = g_vecProjected[i].z;
      if( g_vecProjected[i].w < 0 || g_vecProjected[i].z < 0 || g_fFogCoord[i] < gRSPfFogMin )
         g_fFogCoord[i] = gRSPfFogMin;

      RSP_Vtx_Clipping(i);

      addr = rdram_u8 + dwPDCIAddr + (vert->cidx&0xFF);
      a = addr[0];
      r = addr[3];
      g = addr[2];
      b = addr[1];

      if( gRSP.bLightingEnable )
      {
         g_normal.x = (char)r;
         g_normal.y = (char)g;
         g_normal.z = (char)b;
         Vec3TransformNormal(&g_normal, &gRSPmodelViewTop);
         g_dwVtxDifColor[i] = LightVert(&g_normal, i);
         *(((uint8_t*)&(g_dwVtxDifColor[i]))+3) = (uint8_t)a;    /* still use alpha from the vertex */
      }
      else
      {
         if( (gSP.geometryMode & G_SHADE) == 0 && gRSP.ucode < 5 )  /* Shade is disabled */
            g_dwVtxDifColor[i] = gRDP.primitiveColor;
         else    /* FLAT shade */
            g_dwVtxDifColor[i] = COLOR_RGBA(r, g, b, a);
      }

      if( options.bWinFrameMode )
         g_dwVtxDifColor[i] = COLOR_RGBA(r, g, b, a);

      ReplaceAlphaWithFogFactor(i);

      if (gRSP.bTextureGen && gRSP.bLightingEnable )
         TexGen(&g_fVtxTxtCoords[i].x, &g_fVtxTxtCoords[i].y);
      else
      {
         g_fVtxTxtCoords[i].x = vert->s;
         g_fVtxTxtCoords[i].y = vert->t;
      }
   }
}

void ricegSPLightColor(uint32_t lightNum, uint32_t packedColor)
{
   gRSPlights[lightNum].r = (uint8_t)_SHIFTR( packedColor, 24, 8 );
   gRSPlights[lightNum].g = (uint8_t)_SHIFTR( packedColor, 16, 8 );
   gRSPlights[lightNum].b = (uint8_t)_SHIFTR( packedColor, 8,  8 );
   gRSPlights[lightNum].a = 255;    /* Ignore light alpha */
   gRSPlights[lightNum].fr = (float)gRSPlights[lightNum].r;
   gRSPlights[lightNum].fg = (float)gRSPlights[lightNum].g;
   gRSPlights[lightNum].fb = (float)gRSPlights[lightNum].b;
   gRSPlights[lightNum].fa = 255;   /* Ignore light alpha */
}

void ricegSPNumLights(int32_t n)
{
   gSP.numLights = n;
}

void ricegSPDMATriangles( uint32_t tris, uint32_t n )
{
   bool bTrisAdded     = false;
   uint32_t *rdram_u32 = (uint32_t*)gfx_info.RDRAM;
   uint32_t dwAddr     = RSPSegmentAddr((tris));
   uint32_t *pData     = (uint32_t*)&rdram_u32[dwAddr/4];
   uint32_t i;

   if( dwAddr+ 16 * n >= g_dwRamSize )
      return;

   status.primitiveType = PRIM_DMA_TRI;

   for (i = 0; i < n; i++)
   {
      uint32_t dwInfo = pData[0];

      uint32_t dwV0 = (dwInfo >> 16) & 0x1F;
      uint32_t dwV1 = (dwInfo >>  8) & 0x1F;
      uint32_t dwV2 = (dwInfo      ) & 0x1F;

      if (!bTrisAdded )
      {
         PrepareTextures();
         InitVertexTextureConstants();
      }

      /* Generate texture coordinates */
      g_fVtxTxtCoords[dwV0].x = (short)(pData[1]>>16);
      g_fVtxTxtCoords[dwV0].y = (short)(pData[1]&0xFFFF);
      g_fVtxTxtCoords[dwV1].x = (short)(pData[2]>>16);
      g_fVtxTxtCoords[dwV1].y = (short)(pData[2]&0xFFFF);
      g_fVtxTxtCoords[dwV2].x = (short)(pData[3]>>16);
      g_fVtxTxtCoords[dwV2].y = (short)(pData[3]&0xFFFF);

      if( !bTrisAdded )
         RiceRender_SetCombinerAndBlender();

      bTrisAdded = true;
      PrepareTriangle(dwV0, dwV1, dwV2);

      pData += 4;
   }

   if (bTrisAdded)
      RiceRender_DrawTriangles();
}

void ricegSPLight(uint32_t dwAddr, uint32_t dwLight)
{
   int8_t *rdram_s8;
   int8_t *pcBase;
   uint32_t *pdwBase;
   float range = 0, x, y, z;

   if( dwLight >= 16 )
      return;

   rdram_s8 = (int8_t*)gfx_info.RDRAM;
   pcBase   = rdram_s8 + dwAddr;
   pdwBase  = (uint32_t *)pcBase;

   if( options.enableHackForGames == HACK_FOR_ZELDA_MM && (pdwBase[0]&0xFF) == 0x08 && (pdwBase[1]&0xFF) == 0xFF )
   {
      short *pdwBase16 = (short*)pcBase;
      gRSPn64lights[dwLight].dwRGBA       = pdwBase[0];
      gRSPn64lights[dwLight].dwRGBACopy   = pdwBase[1];
      x       = pdwBase16[5];
      y       = pdwBase16[4];
      z       = pdwBase16[7];
      range   = pdwBase16[6];
   }
   else
   {
      gRSPn64lights[dwLight].dwRGBA       = pdwBase[0];
      gRSPn64lights[dwLight].dwRGBACopy   = pdwBase[1];
      x       = pcBase[8 ^ 0x3];
      y       = pcBase[9 ^ 0x3];
      z       = pcBase[10 ^ 0x3];
   }

   if (dwLight == gRSP.ambientLightIndex)
   {
      uint32_t dwCol = COLOR_RGBA( (gRSPn64lights[dwLight].dwRGBA >> 24)&0xFF,
            (gRSPn64lights[dwLight].dwRGBA >> 16)&0xFF,
            (gRSPn64lights[dwLight].dwRGBA >>  8)&0xFF, 0xff);

      SetAmbientLight( dwCol );
   }
   else
   {
      ricegSPLightColor(dwLight, gRSPn64lights[dwLight].dwRGBA);
      SetLightDirection(dwLight, x, y, z, range);
   }
}

void ricegSPViewport(uint32_t v)
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
   short scale[2];
   short trans[2];
   int nCenterX, nCenterY, nWidth, nHeight;
   int nLeft, nTop, nRight, nBottom;
   int maxZ;

   if( v + 16 >= g_dwRamSize )
      return;

   /* v is offset into RD_RAM of 8 x 16bits of data: scale x, y, z and
      translation x, y, z, each padded; the z and the pads not read */
   scale[0] = *(short *)(rdram_u8 + ((v+(0*2))^0x2));
   scale[1] = *(short *)(rdram_u8 + ((v+(1*2))^0x2));

   trans[0] = *(short *)(rdram_u8 + ((v+(4*2))^0x2));
   trans[1] = *(short *)(rdram_u8 + ((v+(5*2))^0x2));

   nCenterX = trans[0]/4;
   nCenterY = trans[1]/4;
   nWidth   = scale[0]/4;
   nHeight  = scale[1]/4;

   /* Check for some strange games */
   if( nWidth < 0 )
      nWidth = -nWidth;
   if( nHeight < 0 )
      nHeight = -nHeight;

   nLeft   = nCenterX - nWidth;
   nTop    = nCenterY - nHeight;
   nRight  = nCenterX + nWidth;
   nBottom = nCenterY + nHeight;

   maxZ = 0x3FF;

   RiceRender_SetViewport(nLeft, nTop, nRight, nBottom, maxZ);
}
