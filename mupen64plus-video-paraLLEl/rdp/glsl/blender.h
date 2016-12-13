#ifndef BLENDER_H_
#define BLENDER_H_

#include "noise.h"

ivec3 rgb_dither(ivec3 color, int dith, int dither_sel)
{
   ivec3 newcol = mix((color & 0xf8) + 8, ivec3(255), greaterThan(color, ivec3(247)));
   ivec3 comp = dither_sel != 2 ? ivec3(dith) : ivec3(dith, (dith + ivec2(3, 5)) & 7);
   ivec3 replacesign = (comp - (color & 7)) >> 31;
   ivec3 ditherdiff = newcol - color;
   return color + (ditherdiff & replacesign);
}

int mux4(ivec4 muxinput, int selector)
{
        if (selector == 0) return muxinput.x;
   else if (selector == 1) return muxinput.y;
   else if (selector == 2) return muxinput.z;
   else                    return muxinput.w;
}

ivec3 mux4(ivec3 c0, ivec3 c1, ivec3 c2, ivec3 c3, int selector)
{
        if (selector == 0) return c0;
   else if (selector == 1) return c1;
   else if (selector == 2) return c2;
   else                    return c3;
}

ivec3 pre_blender(uint flags, 
                  uint blendword, uint blend_color, uint fog_color,
                  int shaded_alpha, ivec4 color)
{
   // Angrylion Pipelines the update to memory_color, so in the first
   // blend cycle, we cannot observe the memory color anyways.
   // It seems that it has memory sticking around for this, but we cannot implement that sanely
   // with compute. To fix this, just use pixel color alpha to guesstimate the right values.
   //
   int blend_m1a = int(blendword >> 14u) & 3;
   int blend_m2a = int(blendword >>  6u) & 3;
   int blend_m1b = int(blendword >> 10u) & 3;
   int blend_m2b = int(blendword >>  2u) & 3;
   int a0 = mux4(ivec4(color.a, int(fog_color & 0xffu), shaded_alpha, 0), blend_m1b);
   int a1 = mux4(ivec4(~a0 & 0xff, color.a, 0xff, 0), blend_m2b);
   int blend1a = a0 >> 3;
   int blend2a = a1 >> 3;
   int mulb = blend2a + 1;

   ivec3 pre_blended =
      mux4(color.rgb, color.rgb, ivec3(unpack_rgba8(blend_color).rgb), ivec3(unpack_rgba8(fog_color).rgb), blend_m1a) * blend1a +
      mux4(color.rgb, color.rgb, ivec3(unpack_rgba8(blend_color).rgb), ivec3(unpack_rgba8(fog_color).rgb), blend_m2a) * mulb;
   return (pre_blended >> 5) & 0xff;
}

bool blender(uint flags,
             inout uvec4 pixel, uvec2 cvg, uint blendword, uint blend_color, uint fog_color, int shaded_alpha, ivec4 color,
             bool overflow, bool farther, out bool blend_en, uint cycle_shift)
{
   if (PRIMITIVE_ALPHATEST(flags) && (color.a < (PRIMITIVE_ALPHA_NOISE(flags) ? noise8(0) : int(blend_color & 0xffu))))
      return false;

   // In AA mode, at least one sample must be non zero, in non-AA, top-left sample must be non-zero.
   // If we're in cycle one, the blender will no-op and Z writes are discarded, so we can just exit early.
   if ((PRIMITIVE_AA_ENABLE(flags) ? cvg.x : (cvg.y & 0x80u)) == 0u)
      return false;

   blend_en = PRIMITIVE_FORCE_BLEND(flags) || (!overflow && farther && PRIMITIVE_AA_ENABLE(flags));

   // If we're doing color on coverage, only blend if coverage overflows.
   bool noblend = PRIMITIVE_COLOR_ON_CVG(flags) && !overflow;

   ivec3 result;
   int blend_m1a = int(blendword >> (14u - cycle_shift)) & 3;
   int blend_m2a = int(blendword >> ( 6u - cycle_shift)) & 3;

   // If we skip blend, select first blend input.
   if (noblend || !blend_en)
   {
       result = mux4(color.rgb, ivec3(pixel.rgb), ivec3(unpack_rgba8(blend_color).rgb), ivec3(unpack_rgba8(fog_color).rgb),
                     noblend ? blend_m2a : blend_m1a);
   }
   else
   {
      int blend_m1b = int(blendword >> (10u - cycle_shift)) & 3;
      int blend_m2b = int(blendword >> ( 2u - cycle_shift)) & 3;

      int a0 = mux4(ivec4(color.a, int(fog_color & 0xffu), shaded_alpha, 0), blend_m1b);
      int a1 = mux4(ivec4(~a0 & 0xff, int(pixel.a << 5u) | 0x1f, 0xff, 0), blend_m2b);
      int blend1a = a0 >> 3;
      int blend2a = a1 >> 3;
      int mulb = blend2a + 1;

      // Special path for coverage blends.
      // OR-ing in 3 already done by a1 mux.
      // TODO: Deal with blshifta/blshiftb.
      // This mask is not needed unless we care about the blend shifts.
      if (blend_m2b == 1)
         blend1a &= 0x3c;

      // Blend
      ivec3 blended =
         mux4(color.rgb, ivec3(pixel.rgb), ivec3(unpack_rgba8(blend_color).rgb), ivec3(unpack_rgba8(fog_color).rgb), blend_m1a) * blend1a +
         mux4(color.rgb, ivec3(pixel.rgb), ivec3(unpack_rgba8(blend_color).rgb), ivec3(unpack_rgba8(fog_color).rgb), blend_m2a) * mulb;

      if (PRIMITIVE_FORCE_BLEND(flags))
         result = (blended >> 5u) & 0xff;
      else
      {
         // This uses the bldiv_hwaccurate_table which seems a bit overkill for our uses, so just blend in FP for now.
         // At least try to get the input sum and blended right :)
         int sum = (blend1a & ~3) + (blend2a & ~3) + 4;
         result = clamp(ivec3(round(4.0 * vec3(blended >> 2) / float(sum))), ivec3(0), ivec3(0xff));
      }
   }

   int dither_sel = int(RGB_DITHER_SEL(blendword));
   if (dither_sel != 3)
   {
      int cdith;
      if (dither_sel == 2)
         cdith = noise8(1) & 0x7;
      else
         cdith = int(textureLod(uDitherLUT, vec3((vec2(gl_GlobalInvocationID.xy) + 0.5) * 0.25, float(FULL_DITHER_SEL(blendword))), 0.0).x);
      result = rgb_dither(result, cdith, dither_sel);
   }

   // Writeback
   pixel.rgb = uvec3(clamp(result, ivec3(0), ivec3(0xff)));
   return true;
}

#endif
