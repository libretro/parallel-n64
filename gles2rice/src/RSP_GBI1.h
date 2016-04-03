/*
Copyright (C) 2002 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "Render.h"
#include "Timing.h"

void RSP_GBI1_Vtx(Gfx *gfx)
{
    uint32_t addr = RSPSegmentAddr((gfx->gbi1vtx.addr));
    uint32_t v0  = gfx->gbi1vtx.v0;
    uint32_t n   = gfx->gbi1vtx.n;

    LOG_UCODE("    Address 0x%08x, v0: %d, Num: %d, Length: 0x%04x", addr, v0, n, gfx->gbi1vtx.len);

    if (addr > g_dwRamSize)
        return;

    if ((v0 + n) > 80)
        return;

    ProcessVertexData(addr, v0, n);
    status.dwNumVertices += n;
    DisplayVertexInfo(addr, v0, n);
}

void RSP_GBI1_ModifyVtx(Gfx *gfx)
{
    SP_Timing(RSP_GBI1_ModifyVtx);

    if( gRSP.ucode == 5 && ((gfx->words.w0)&0x00FFFFFF) == 0 && ((gfx->words.w1)&0xFF000000) == 0x80000000 )
    {
        DLParser_Bomberman2TextRect(gfx);
    }
    else
    {
       uint32_t where    = ((gfx->words.w0) >> 16) & 0xFF;
       uint32_t vtx      = (((gfx->words.w0)      ) & 0xFFFF) / 2;
       uint32_t val      = (gfx->words.w1);

       if( vtx > 80 )
          return;

       // Data for other commands?
       switch (where)
       {
          case G_MWO_POINT_RGBA:
          case G_MWO_POINT_XYSCREEN:
          case G_MWO_POINT_ZSCREEN:
          case G_MWO_POINT_ST:
             ricegSPModifyVertex(vtx, where, val);
             break;
          default:
             RSP_RDP_NOIMPL("RSP_GBI1_ModifyVtx: Setting unk value: 0x%02x, 0x%08x", where, val);
             break;
       }
    }
}

void RSP_GBI1_Tri2(Gfx *gfx)
{
    status.primitiveType = PRIM_TRI2;
    bool bTrisAdded = false;
    bool bTexturesAreEnabled = CRender::g_pRender->IsTextureEnabled();

    // While the next command pair is Tri2, add vertices
    uint32_t dwPC = gDlistStack[__RSP.PCi].pc;

    do {
        // Vertex indices are multiplied by 10 for Mario64, by 2 for MarioKart
        uint32_t dwV0 = gfx->gbi1tri2.v0/gRSP.vertexMult;
        uint32_t dwV1 = gfx->gbi1tri2.v1/gRSP.vertexMult;
        uint32_t dwV2 = gfx->gbi1tri2.v2/gRSP.vertexMult;

        uint32_t dwV3 = gfx->gbi1tri2.v3/gRSP.vertexMult;
        uint32_t dwV4 = gfx->gbi1tri2.v4/gRSP.vertexMult;
        uint32_t dwV5 = gfx->gbi1tri2.v5/gRSP.vertexMult;

        // Do first tri
        if (IsTriangleVisible(dwV0, dwV1, dwV2))
        {
            DEBUG_DUMP_VERTEXES("Tri2 1/2", dwV0, dwV1, dwV2);
            if (!bTrisAdded)
            {
                if( bTexturesAreEnabled )
                {
                    PrepareTextures();
                    InitVertexTextureConstants();
                }

                CRender::g_pRender->SetCombinerAndBlender();
                bTrisAdded = true;
            }
            PrepareTriangle(dwV0, dwV1, dwV2);
        }

        // Do second tri
        if (IsTriangleVisible(dwV3, dwV4, dwV5))
        {
            DEBUG_DUMP_VERTEXES("Tri2 2/2", dwV3, dwV4, dwV5);
            if (!bTrisAdded)
            {
                if( bTexturesAreEnabled )
                {
                    PrepareTextures();
                    InitVertexTextureConstants();
                }

                CRender::g_pRender->SetCombinerAndBlender();
                bTrisAdded = true;
            }
            PrepareTriangle(dwV3, dwV4, dwV5);
        }
        
        gfx++;
        dwPC += 8;
#ifdef DEBUGGER
    } while (!(pauseAtNext && eventToPause==NEXT_TRIANGLE) && gfx->words.cmd == (uint8_t)RSP_TRI2);
#else
    } while( gfx->words.cmd == (uint8_t)RSP_TRI2);
#endif


    gDlistStack[__RSP.PCi].pc = dwPC-8;


    if (bTrisAdded) 
    {
        CRender::g_pRender->DrawTriangles();
    }

    DEBUG_TRIANGLE(TRACE0("Pause at GBI1 TRI1"));
}

extern XVECTOR4 g_vtxNonTransformed[MAX_VERTS];

void RSP_GBI1_BranchZ(Gfx *gfx)
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    SP_Timing(RSP_GBI1_BranchZ);

    uint32_t vtx = ((gfx->words.w0)&0xFFF)>>1;
    float vtxdepth = g_vecProjected[vtx].z/g_vecProjected[vtx].w;

#ifdef DEBUGGER
    if( debuggerEnableZBuffer==FALSE || vtxdepth <= (int32_t)gfx->words.w1 || g_curRomInfo.bForceDepthBuffer )
#else
    if( vtxdepth <= (int32_t)(gfx->words.w1) || g_curRomInfo.bForceDepthBuffer )
#endif
    {
        uint32_t dwPC = gDlistStack[__RSP.PCi].pc;       // This points to the next instruction
        uint32_t dwDL = *(uint32_t *)(rdram_u8 + dwPC-12);
        uint32_t dwAddr = RSPSegmentAddr(dwDL);

        LOG_UCODE("BranchZ to DisplayList 0x%08x", dwAddr);
        gDlistStack[__RSP.PCi].pc = dwAddr;
        gDlistStack[__RSP.PCi].countdown = MAX_DL_COUNT;
    }
}

#ifdef DEBUGGER
void DumpUcodeInfo(UcodeInfo &info)
{
    DebuggerAppendMsg("Loading Unknown Ucode:\n%08X-%08X-%08X-%08X, Size=0x%X, CRC=0x%08X\nCode:\n",
        info.ucDWORD1, info.ucDWORD2, info.ucDWORD3, info.ucDWORD4, 
        info.ucSize, info.ucCRC);
    DumpHex(info.ucStart,20);
    TRACE0("Data:\n");
    DumpHex(info.ucDStart,20);
}
#endif

void RSP_GBI1_LoadUCode(Gfx *gfx)
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    SP_Timing(RSP_GBI1_LoadUCode);

    //TRACE0("Load ucode");
    uint32_t dwPC = gDlistStack[__RSP.PCi].pc;
    uint32_t dwUcStart = RSPSegmentAddr((gfx->words.w1));
    uint32_t dwSize = ((gfx->words.w0)&0xFFFF)+1;
    uint32_t dwUcDStart = RSPSegmentAddr(*(uint32_t *)(rdram_u8 + dwPC-12));

    uint32_t ucode = DLParser_CheckUcode(dwUcStart, dwUcDStart, dwSize, 8);
    RSP_SetUcode(ucode, dwUcStart, dwUcDStart, dwSize);
}

void RSP_GFX_Force_Matrix(uint32_t dwAddr)
{
    if (dwAddr + 64 > g_dwRamSize)
    {
        DebuggerAppendMsg("ForceMtx: Address invalid (0x%08x)", dwAddr);
        return;
    }

    // Load matrix from dwAddr
    LoadMatrix(dwAddr);

    CRender::g_pRender->SetWorldProjectMatrix(matToLoad);
}


void DisplayVertexInfo(uint32_t dwAddr, uint32_t dwV0, uint32_t dwN)
{
#ifdef DEBUGGER
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
        int8_t *pcSrc = (int8_t *)(rdram_u8 + dwAddr);
        short *psSrc = (short *)(rdram_u8 + dwAddr);

        for (uint32_t dwV = dwV0; dwV < dwV0 + dwN; dwV++)
        {
            float x = (float)psSrc[0^0x1];
            float y = (float)psSrc[1^0x1];
            float z = (float)psSrc[2^0x1];

            //uint32_t wFlags = g_dwVtxFlags[dwV]; //(uint16_t)psSrc[3^0x1];
            uint32_t wFlags = 0;

            uint8_t a = pcSrc[12^0x3];
            uint8_t b = pcSrc[13^0x3];
            uint8_t c = pcSrc[14^0x3];
            uint8_t d = pcSrc[15^0x3];
            
            //int nTU = (int)(short)(psSrc[4^0x1]<<4);
            //int nTV = (int)(short)(psSrc[5^0x1]<<4);

            //float tu = (float)(nTU>>4);
            //float tv = (float)(nTV>>4);
            float tu = (float)(short)(psSrc[4^0x1]);
            float tv = (float)(short)(psSrc[5^0x1]);

            XVECTOR4 & t = g_vecProjected[dwV];

            psSrc += 8;         // Increase by 16 bytes
            pcSrc += 16;

            LOG_UCODE(" #%02d Flags: 0x%04x Pos: {% 6f,% 6f,% 6f} Tex: {%+7.2f,%+7.2f}, Extra: %02x %02x %02x %02x (transf: {% 6f,% 6f,% 6f})",
                dwV, wFlags, x, y, z, tu, tv, a, b, c, d, t.x, t.y, t.z );
        }
#endif
}

// S2DEX uses this - 0xc1
void RSP_S2DEX_SPObjLoadTxtr_Ucode1(Gfx *gfx)
{
    SP_Timing(RSP_S2DEX_SPObjLoadTxtr_Ucode1);

    // Add S2DEX ucode supporting to F3DEX, see game DT and others
    status.bUseModifiedUcodeMap = true;
    RSP_SetUcode(1, 0, 0, 0);
    memcpy( &LoadedUcodeMap, &ucodeMap1, sizeof(UcodeMap));
    
    LoadedUcodeMap[S2DEX_OBJ_MOVEMEM]     = &RSP_S2DEX_OBJ_MOVEMEM;
    LoadedUcodeMap[S2DEX_OBJ_LOADTXTR]    = &RSP_S2DEX_SPObjLoadTxtr;
    LoadedUcodeMap[S2DEX_OBJ_LDTX_SPRITE] = &RSP_S2DEX_SPObjLoadTxSprite;
    LoadedUcodeMap[S2DEX_OBJ_LDTX_RECT]   = &RSP_S2DEX_SPObjLoadTxRect;
    LoadedUcodeMap[S2DEX_OBJ_LDTX_RECT_R] = &RSP_S2DEX_SPObjLoadTxRectR;

    RSP_S2DEX_SPObjLoadTxtr(gfx);
}

