#include "gSP_funcs.h"

void GSPCombineMatrices(enum gsp_plugin_type plug_type)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPCombineMatrices();
         break;
      case GSP_PLUGIN_GLN64:
         gln64gSPCombineMatrices();
         break;
   }
}

void GSPClipVertex(enum gsp_plugin_type plug_type, uint32_t v)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPClipVertex(v);
         break;
      case GSP_PLUGIN_GLN64:
         gln64gSPClipVertex(v);
         break;
   }
}

/* Loads a LookAt structure in the RSP for specular highlighting
 * and projection mapping.
 *
 * l             - The lookat structure address.
 */
void GSPLookAt(enum gsp_plugin_type plug_type, uint32_t l, uint32_t n)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPLookAt(l, n);
         break;
      case GSP_PLUGIN_GLN64:
         gln64gSPLookAt(l, n);
         break;
   }
}

/* Loads one light structure to the RSP.
 *
 * l             - The pointer to the light structure.
 * n             - The light number that is replaced (1~8)
 */
void GSPLight(enum gsp_plugin_type plug_type, uint32_t l, int32_t n)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPLight(l, n);
         break;
      case GSP_PLUGIN_GLN64:
         gln64gSPLight(l, n);
         break;
   }
}

/* Quickly changes the light color in the RSP.
 *
 * lightNum     - The light number whose color is being modified:
 *                LIGHT_1 (First light)
 *                LIGHT_2 (Second light)
 *                :
 *                LIGHT_u (Eighth light)
 *
 * packedColor - The new light color (32-bit value 0xRRGGBB??)
 *               (?? is ignored)
 * */

void GSPLightColor(enum gsp_plugin_type plug_type, uint32_t lightNum, uint32_t packedColor )
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         gSPLightColor(lightNum, packedColor);
         break;
      case GSP_PLUGIN_GLN64:
         gln64gSPLightColor(lightNum, packedColor);
         break;
   }
}

void GSPViewport(enum gsp_plugin_type plug_type, uint32_t v)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPViewport(v);
         break;
      case GSP_PLUGIN_GLN64:
         gln64gSPViewport(v);
         break;
   }
}
