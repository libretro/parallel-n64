#include "gDP_funcs.h"

/* Sets a scissoring box at the screen coordinates 
 *
 * mode              - Video mode:
 *                     G_SC_NON_INTERLACE  - Draw all scan lines
 *                     G_SC_ODD_INTERLACE  - Draw only odd-numbered scan lines
 *                     G_SC_EVEN_INTERLACE - Draw only even-numbered scan lines
 *
 * ulx               - Screen's left edge coordinates (0.0~1023.75) 
 * uly               - Screen's top edge coordinates (0.0~1023.75) 
 * lrx               - Screen's right edge coordinates (0.0~1023.75) 
 * lry               - Screen's bottom edge coordinates (0.0~1023.75) 
 * */
void GDPSetScissor(enum gdp_plugin_type plug_type, uint32_t mode,
      float ulx, float uly, float lrx, float lry )
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
