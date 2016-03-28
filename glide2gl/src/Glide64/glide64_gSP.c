#include <stdint.h>

#include "glide64_gSP.h"

void glide64gSPCombineMatrices(void)
{
   MulMatrices(rdp.model, rdp.proj, rdp.combined);
   g_gdp.flags ^= UPDATE_MULT_MAT;
}

void glide64gSPSegment(int32_t seg, int32_t base)
{
   rdp.segment[seg] = base;
}

void glide64gSPClipVertex(uint32_t v)
{
   VERTEX *vtx = (VERTEX*)&rdp.vtx[v];

   vtx->scr_off = 0;
   if (vtx->x > +vtx->w)   vtx->scr_off |= 2;
   if (vtx->x < -vtx->w)   vtx->scr_off |= 1;
   if (vtx->y > +vtx->w)   vtx->scr_off |= 8;
   if (vtx->y < -vtx->w)   vtx->scr_off |= 4;
   if (vtx->w < 0.1f)      vtx->scr_off |= 16;
}

void glide64gSPLookAt(uint32_t l, uint32_t n)
{
   int8_t  *rdram_s8  = (int8_t*) (gfx_info.RDRAM  + RSP_SegmentToPhysical(l));
   int8_t dir_x = rdram_s8[11];
   int8_t dir_y = rdram_s8[10];
   int8_t dir_z = rdram_s8[9];
   rdp.lookat[n][0] = (float)(dir_x) / 127.0f;
   rdp.lookat[n][1] = (float)(dir_y) / 127.0f;
   rdp.lookat[n][2] = (float)(dir_z) / 127.0f;
   rdp.use_lookat = (n == 0) || (n == 1 && (dir_x || dir_y));
}

void glide64gSPLight(uint32_t l, int32_t n)
{
   int16_t *rdram     = (int16_t*)(gfx_info.RDRAM  + RSP_SegmentToPhysical(l));
   uint8_t *rdram_u8  = (uint8_t*)(gfx_info.RDRAM  + RSP_SegmentToPhysical(l));
   int8_t  *rdram_s8  = (int8_t*) (gfx_info.RDRAM  + RSP_SegmentToPhysical(l));

   --n;

   if (n < 8)
   {
      /* Get the data */
      rdp.light[n].nonblack  = rdram_u8[3];
      rdp.light[n].nonblack += rdram_u8[2];
      rdp.light[n].nonblack += rdram_u8[1];

      rdp.light[n].col[0]    = rdram_u8[3] / 255.0f;
      rdp.light[n].col[1]    = rdram_u8[2] / 255.0f;
      rdp.light[n].col[2]    = rdram_u8[1] / 255.0f;
      rdp.light[n].col[3]    = 1.0f;

      rdp.light[n].dir[0]    = (float)rdram_s8[11] / 127.0f;
      rdp.light[n].dir[1]    = (float)rdram_s8[10] / 127.0f;
      rdp.light[n].dir[2]    = (float)rdram_s8[9] / 127.0f;

      rdp.light[n].x         = (float)rdram[5];
      rdp.light[n].y         = (float)rdram[4];
      rdp.light[n].z         = (float)rdram[7];
      rdp.light[n].ca        = (float)rdram[0] / 16.0f;
      rdp.light[n].la        = (float)rdram[4];
      rdp.light[n].qa        = (float)rdram[13] / 8.0f;

      //g_gdp.flags |= UPDATE_LIGHTS;
   }
}

void glide64gSPLightColor( uint32_t lightNum, uint32_t packedColor )
{
   lightNum--;

   if (lightNum < 8)
   {
      rdp.light[lightNum].col[0] = _SHIFTR( packedColor, 24, 8 ) * 0.0039215689f;
      rdp.light[lightNum].col[1] = _SHIFTR( packedColor, 16, 8 ) * 0.0039215689f;
      rdp.light[lightNum].col[2] = _SHIFTR( packedColor, 8, 8 )  * 0.0039215689f;
      rdp.light[lightNum].col[3] = 255;
   }
}

void glide64gSP1Triangle( int32_t v0, int32_t v1, int32_t v2, int32_t flag )
{
   VERTEX *v[3];

   v[0] = &rdp.vtx[v0];
   v[1] = &rdp.vtx[v1];
   v[2] = &rdp.vtx[v2];

   cull_trianglefaces(v, 1, true, true, 0);
}

void glide64gSP4Triangles( int32_t v00, int32_t v01, int32_t v02,
                    int32_t v10, int32_t v11, int32_t v12,
                    int32_t v20, int32_t v21, int32_t v22,
                    int32_t v30, int32_t v31, int32_t v32 )
{
   VERTEX *v[12];

   if (rdp.skip_drawing)
      return;

   v[0]  = &rdp.vtx[v00];
   v[1]  = &rdp.vtx[v01];
   v[2]  = &rdp.vtx[v02];
   v[3]  = &rdp.vtx[v10];
   v[4]  = &rdp.vtx[v11];
   v[5]  = &rdp.vtx[v12];
   v[6]  = &rdp.vtx[v20];
   v[7]  = &rdp.vtx[v21];
   v[8]  = &rdp.vtx[v22];
   v[9]  = &rdp.vtx[v30];
   v[10] = &rdp.vtx[v31];
   v[11] = &rdp.vtx[v32];

   cull_trianglefaces(v, 4, true, true, 0);
}
