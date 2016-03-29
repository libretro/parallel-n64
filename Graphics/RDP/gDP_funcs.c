#include "gDP_funcs.h"

void GDPSetScissor(enum gdp_plugin_type plug_type, uint32_t mode, float ulx, float uly, float lrx, float lry )
{
   switch (plug_type)
   {
      case GDP_PLUGIN_GLIDE64:
         glide64gDPSetScissor(mode, ulx, uly, lrx, lry);
         break;
      case GDP_PLUGIN_GLN64:
         gln64gDPSetScissor(mode, ulx, uly, lrx, lry);
         break;
   }
}
