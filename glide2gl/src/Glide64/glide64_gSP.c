#include <stdint.h>

#include "glide64_gSP.h"

void glide64gSPCombineMatrices(void)
{
   MulMatrices(rdp.model, rdp.proj, rdp.combined);
   g_gdp.flags ^= UPDATE_MULT_MAT;
}
