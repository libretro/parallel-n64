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
