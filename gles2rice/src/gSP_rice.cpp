#include <stdint.h>

#include <retro_miscellaneous.h>

#include "osal_preproc.h"
#include "float.h"
#include "DeviceBuilder.h"
#include "Render.h"
#include "Timing.h"

#include "../../Graphics/GBI.h"

/* Multiply (x,y,z,0) by matrix m, then normalize */
#define Vec3TransformNormal(vec, m) \
   VECTOR3 temp; \
   temp.x = (vec.x * m._11) + (vec.y * m._21) + (vec.z * m._31); \
   temp.y = (vec.x * m._12) + (vec.y * m._22) + (vec.z * m._32); \
   temp.z = (vec.x * m._13) + (vec.y * m._23) + (vec.z * m._33); \
   float norm = sqrt(temp.x*temp.x+temp.y*temp.y+temp.z*temp.z); \
   if (norm == 0.0) { vec.x = 0.0; vec.y = 0.0; vec.z = 0.0;} else \
   { vec.x = temp.x/norm; vec.y = temp.y/norm; vec.z = temp.z/norm; }

extern uint32_t dwPDCIAddr;
extern uint32_t dwConkerVtxZAddr;
extern FiddledVtx * g_pVtxBase;

extern ALIGN(16,XVECTOR4 g_normal);
extern ALIGN(16,XVECTOR4 g_vtxNonTransformed[MAX_VERTS]);

extern void TexGen(float &s, float &t);
extern void RSP_Vtx_Clipping(int i);
extern uint32_t LightVert(XVECTOR4 & norm, int vidx);
extern void ReplaceAlphaWithFogFactor(int i);

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
            LOG_UCODE("Modify vertex %d color, 0x%08x", vtx, g_dwVtxDifColor[vtx]);
        }
        break;
    case G_MWO_POINT_ST:
        {
            int16_t tu = (int16_t)(val>>16);
            int16_t tv = (int16_t)(val & 0xFFFF);
            float ftu  = tu / 32.0f;
            float ftv  = tv / 32.0f;
            LOG_UCODE("      Setting vertex %d tu/tv to %f, %f", vtx, (float)tu, (float)tv);
            CRender::g_pRender->SetVtxTextureCoord(vtx, ftu/gRSP.fTexScaleX, ftv/gRSP.fTexScaleY);
        }
        break;
    case G_MWO_POINT_XYSCREEN:
        {
            uint16_t nX = (uint16_t)(val>>16);
            int16_t x   = *((int16_t*)&nX);
            x /= 4;

            uint16_t nY = (uint16_t)(val&0xFFFF);
            int16_t y   = *((int16_t*)&nY);
            y /= 4;

            // Should do viewport transform.


            x -= windowSetting.uViWidth/2;
            y = windowSetting.uViHeight/2-y;

            if( options.bEnableHacks && ((*gfx_info.VI_X_SCALE_REG)&0xF) != 0 )
            {
                // Tarzan
                // I don't know why Tarzan is different
                SetVertexXYZ(vtx, x/windowSetting.fViWidth, y/windowSetting.fViHeight, g_vecProjected[vtx].z);
            }
            else
            {
                // Toy Story 2 and other games
                SetVertexXYZ(vtx, x*2/windowSetting.fViWidth, y*2/windowSetting.fViHeight, g_vecProjected[vtx].z);
            }

            LOG_UCODE("Modify vertex %d: x=%d, y=%d", vtx, x, y);
            VTX_DUMP(TRACE3("Modify vertex %d: (%d,%d)", vtx, x, y));
        }
        break;
    case G_MWO_POINT_ZSCREEN:
        {
            int z = val>>16;

            SetVertexXYZ(vtx, g_vecProjected[vtx].x, g_vecProjected[vtx].y, (((float)z/0x03FF)+0.5f)/2.0f );
            LOG_UCODE("Modify vertex %d: z=%d", vtx, z);
            VTX_DUMP(TRACE2("Modify vertex %d: z=%d", vtx, z));
        }
        break;
    }
    DEBUGGER_PAUSE_AND_DUMP(NEXT_VERTEX_CMD,{TRACE0("Paused at ModVertex Command");});
}

void ricegSPCIVertex(uint32_t v, uint32_t n, uint32_t v0)
{
    UpdateCombinedMatrix();

    uint8_t *rdram_u8   = (uint8_t*)gfx_info.RDRAM;
    N64VtxPD * pVtxBase = (N64VtxPD*)(rdram_u8 + v);
    g_pVtxBase          = (FiddledVtx*)pVtxBase; // Fix me

    for (uint32_t i = v0; i < v0 + n; i++)
    {
        N64VtxPD           &vert = pVtxBase[i - v0];

        g_vtxNonTransformed[i].x = (float)vert.x;
        g_vtxNonTransformed[i].y = (float)vert.y;
        g_vtxNonTransformed[i].z = (float)vert.z;

        Vec3Transform(&g_vtxTransformed[i], (XVECTOR3*)&g_vtxNonTransformed[i], &gRSPworldProject); // Convert to w=1
        g_vecProjected[i].w = 1.0f / g_vtxTransformed[i].w;
        g_vecProjected[i].x = g_vtxTransformed[i].x * g_vecProjected[i].w;
        g_vecProjected[i].y = g_vtxTransformed[i].y * g_vecProjected[i].w;
        g_vecProjected[i].z = g_vtxTransformed[i].z * g_vecProjected[i].w;

        g_fFogCoord[i] = g_vecProjected[i].z;
        if( g_vecProjected[i].w < 0 || g_vecProjected[i].z < 0 || g_fFogCoord[i] < gRSPfFogMin )
            g_fFogCoord[i] = gRSPfFogMin;

        RSP_Vtx_Clipping(i);

        uint8_t *addr = rdram_u8 + dwPDCIAddr + (vert.cidx&0xFF);
        uint32_t a = addr[0];
        uint32_t r = addr[3];
        uint32_t g = addr[2];
        uint32_t b = addr[1];

        if( gRSP.bLightingEnable )
        {
            g_normal.x = (char)r;
            g_normal.y = (char)g;
            g_normal.z = (char)b;
            {
                Vec3TransformNormal(g_normal, gRSPmodelViewTop);
                g_dwVtxDifColor[i] = LightVert(g_normal, i);
            }
            *(((uint8_t*)&(g_dwVtxDifColor[i]))+3) = (uint8_t)a;    // still use alpha from the vertex
        }
        else
        {
            if( (gRDP.geometryMode & G_SHADE) == 0 && gRSP.ucode < 5 )  //Shade is disabled
            {
                g_dwVtxDifColor[i] = gRDP.primitiveColor;
            }
            else    //FLAT shade
            {
                g_dwVtxDifColor[i] = COLOR_RGBA(r, g, b, a);
            }
        }

        if( options.bWinFrameMode )
        {
            g_dwVtxDifColor[i] = COLOR_RGBA(r, g, b, a);
        }

        ReplaceAlphaWithFogFactor(i);

        VECTOR2 & t = g_fVtxTxtCoords[i];
        if (gRSP.bTextureGen && gRSP.bLightingEnable )
        {
            // Not sure if we should transform the normal here
            //Matrix & matWV = gRSP.projectionMtxs[gRSP.projectionMtxTop];
            //Vec3TransformNormal(g_normal, matWV);

            TexGen(g_fVtxTxtCoords[i].x, g_fVtxTxtCoords[i].y);
        }
        else
        {
            t.x = vert.s;
            t.y = vert.t; 
        }


        VTX_DUMP( 
        {
            DebuggerAppendMsg("Vertex %d: %d %d %d", i, vert.x,vert.y,vert.z); 
            DebuggerAppendMsg("      : %f, %f, %f, %f", 
                g_vtxTransformed[i].x,g_vtxTransformed[i].y,g_vtxTransformed[i].z,g_vtxTransformed[i].w);
            DebuggerAppendMsg("      : %X, %X, %X, %X", r,g,b,a);
            DebuggerAppendMsg("      : u=%f, v=%f", t.x, t.y);
        });
    }

    VTX_DUMP(TRACE2("Setting Vertexes: %d - %d\n", dwV0, dwV0+dwNum-1));
    DEBUGGER_PAUSE_AND_DUMP(NEXT_VERTEX_CMD,{TRACE0("Paused at Vertex Command");});
}

void ricegSPLightColor(uint32_t lightNum, uint32_t packedColor)
{
    gRSPlights[lightNum].r = (uint8_t)_SHIFTR( packedColor, 24, 8 );
    gRSPlights[lightNum].g = (uint8_t)_SHIFTR( packedColor, 16, 8 );
    gRSPlights[lightNum].b = (uint8_t)_SHIFTR( packedColor, 8,  8 );
    gRSPlights[lightNum].a = 255;    // Ignore light alpha
    gRSPlights[lightNum].fr = (float)gRSPlights[lightNum].r;
    gRSPlights[lightNum].fg = (float)gRSPlights[lightNum].g;
    gRSPlights[lightNum].fb = (float)gRSPlights[lightNum].b;
    gRSPlights[lightNum].fa = 255;   // Ignore light alpha

    //TRACE1("Set light %d color", lightNum);
    LIGHT_DUMP(TRACE2("Set Light %d color: %08X", lightNum, dwCol));
}

void ricegSPNumLights(int32_t n) 
{ 
    gRSPnumLights = n; 
    DEBUGGER_PAUSE_AND_DUMP(NEXT_SET_LIGHT,TRACE1("Set Number Of Lights: %d", dwNumLights));
}

void ricegDPSetEnvColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
   gRDP.colorsAreReloaded = true;
   gRDP.envColor = COLOR_RGBA(r, g, b, a); 
   gRDP.fvEnvColor[0] = r / 255.0f;
   gRDP.fvEnvColor[1] = g / 255.0f;
   gRDP.fvEnvColor[2] = b / 255.0f;
   gRDP.fvEnvColor[3] = a / 255.0f;
}

void ricegSPCIVertex(uint32_t v, uint32_t n, uint32_t v0);

void ricegSPDMATriangles( uint32_t tris, uint32_t n )
{
   bool bTrisAdded     = false;
   uint32_t *rdram_u32 = (uint32_t*)gfx_info.RDRAM;
   uint32_t dwAddr     = RSPSegmentAddr((tris));
   uint32_t * pData    = (uint32_t*)&rdram_u32[dwAddr/4];

   if( dwAddr+ 16 * n >= g_dwRamSize )
   {
      TRACE0("DMATRI invalid memory pointer");
      return;
   }

   TRI_DUMP(TRACE2("DMATRI, addr=%08X, Cmd0=%08X\n", dwAddr, (gfx->words.w0)));

   status.primitiveType = PRIM_DMA_TRI;

   for (uint32_t i = 0; i < n; i++)
   {
      LOG_UCODE("    0x%08x: %08x %08x %08x %08x", dwAddr + i*16,
            pData[0], pData[1], pData[2], pData[3]);

      uint32_t dwInfo = pData[0];

      uint32_t dwV0 = (dwInfo >> 16) & 0x1F;
      uint32_t dwV1 = (dwInfo >>  8) & 0x1F;
      uint32_t dwV2 = (dwInfo      ) & 0x1F;

      TRI_DUMP(TRACE5("DMATRI: %d, %d, %d (%08X-%08X)", dwV0,dwV1,dwV2,(gfx->words.w0),(tris)));

      //if (IsTriangleVisible(dwV0, dwV1, dwV2))
      {
         DEBUG_DUMP_VERTEXES("DmaTri", dwV0, dwV1, dwV2);
         LOG_UCODE("   Tri: %d,%d,%d", dwV0, dwV1, dwV2);
         if (!bTrisAdded )//&& CRender::g_pRender->IsTextureEnabled())
         {
            PrepareTextures();
            InitVertexTextureConstants();
         }

         // Generate texture coordinates
         short s0 = ((short)(pData[1]>>16));
         short t0 = ((short)(pData[1]&0xFFFF));
         short s1 = ((short)(pData[2]>>16));
         short t1 = ((short)(pData[2]&0xFFFF));
         short s2 = ((short)(pData[3]>>16));
         short t2 = ((short)(pData[3]&0xFFFF));

         TRI_DUMP( 
               {
               DebuggerAppendMsg(" (%d,%d), (%d,%d), (%d,%d)",s0,t0,s1,t1,s2,t2);
               DebuggerAppendMsg(" (%08X), (%08X), (%08X), (%08X)",pData[0],pData[1],pData[2],pData[3]);
               });
         CRender::g_pRender->SetVtxTextureCoord(dwV0, s0, t0);
         CRender::g_pRender->SetVtxTextureCoord(dwV1, s1, t1);
         CRender::g_pRender->SetVtxTextureCoord(dwV2, s2, t2);

         if( !bTrisAdded )
         {
            CRender::g_pRender->SetCombinerAndBlender();
         }

         bTrisAdded = true;
         PrepareTriangle(dwV0, dwV1, dwV2);
      }

      pData += 4;

   }

   if (bTrisAdded) 
      CRender::g_pRender->DrawTriangles();
}
