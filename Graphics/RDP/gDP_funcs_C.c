#include "gDP_funcs_C.h"

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
void GDPSetScissorC(enum gdp_plugin_type plug_type, uint32_t mode,
      float ulx, float uly, float lrx, float lry )
{
   switch (plug_type)
   {
      case GDP_PLUGIN_GLIDE64:
         glide64gDPSetScissor(mode, ulx, uly, lrx, lry);
         break;
      case GDP_PLUGIN_GLN64:
#ifndef GLIDEN64
         gln64gDPSetScissor(mode, ulx, uly, lrx, lry);
#endif
         break;
   }
}

/*
 * Loads a texture image in a continguous block from DRAM to
 * on-chip texture memory (TMEM).
 *
 * The texture image is loaded into memory in a single transfer.
 *
 * tile      - Tile descriptor index 93-bit precision, 0~7).
 * ul_s      - texture tile's upper-left s coordinate (10.2, 0.0~1023.75)
 * ul_t      - texture tile's upper-left t coordinate (10.2, 0.0~1023.75)
 * lr_s      - texture tile's lower-right s coordinate (10.2, 0.0~1023.75)
 * dxt        - amount of change in value of t per scan line (12-bit precision, 0~4095)
 */
void GDPLoadBlockC(enum gdp_plugin_type plug_type, uint32_t tile, uint32_t ul_s, uint32_t ul_t,
      uint32_t lr_s, uint32_t dxt )
{
   switch (plug_type)
   {
      case GDP_PLUGIN_GLIDE64:
         glide64gDPLoadBlock(tile, ul_s, ul_t, lr_s, dxt);
         break;
      case GDP_PLUGIN_GLN64:
#ifndef GLIDEN64
         gln64gDPLoadBlock(tile, ul_s, ul_t, lr_s, dxt);
#endif
         break;
   }
}
