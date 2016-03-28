#include "gSP_funcs.h"

void GSPCombineMatrices(enum gsp_plugin_type plug_type)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPCombineMatrices();
         break;
      case GSP_PLUGIN_GLN64:
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
         break;
   }
}
