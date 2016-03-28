#include <stdint.h>

#include "glide64_gDP.h"
#include "Util.h"

void glide64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry )
{
   g_gdp.__clip.xh = (uint32_t)ulx;
   g_gdp.__clip.yh = (uint32_t)ulx;
   g_gdp.__clip.xl = (uint32_t)lrx;
   g_gdp.__clip.yl = (uint32_t)lry;
   if (rdp.ci_count)
   {
      COLOR_IMAGE *cur_fb = (COLOR_IMAGE*)&rdp.frame_buffers[rdp.ci_count-1];
      if (g_gdp.__clip.xl - g_gdp.__clip.xh > (uint32_t)(cur_fb->width >> 1))
      {
         if (cur_fb->height == 0 || (cur_fb->width >= g_gdp.__clip.xl - 1 && cur_fb->width <= g_gdp.__clip.xl + 1))
            cur_fb->height = g_gdp.__clip.yl;
      }
   }
}
