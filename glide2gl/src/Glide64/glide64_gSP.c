#include <stdint.h>

#include "glide64_gSP.h"

void glide64gSPCombineMatrices(void)
{
   MulMatrices(rdp.model, rdp.proj, rdp.combined);
   g_gdp.flags ^= UPDATE_MULT_MAT;
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
