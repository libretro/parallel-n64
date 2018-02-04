#ifndef COMBINER_H_
#define COMBINER_H_

#include "util.h"
#include "noise.h"

ivec4 combiner(ivec4 sub_a, ivec4 sub_b, ivec4 mul, ivec4 add)
{
   // Maps to special_9bit_exttable in Angrylion, no idea why this is used though :P
   sub_a = special_9bit_ext(sub_a);
   sub_b = special_9bit_ext(sub_b);
   add = special_9bit_ext(add);
   // Sign-extend 9-bit.
   mul = (mul << 23) >> 23;
   return special_9bit_clamp(((((sub_a - sub_b) * mul) + (add << 8) + 0x80) & 0x1ffff) >> ivec4(8));
}

// Should probably split this out to a mux which doesn't support Cycle 2 stuff ...
void combiner_mux(out ivec4 sub_a, out ivec4 sub_b, out ivec4 mul, out ivec4 add,
                  uint instance, uint c, ivec4 sampled0, ivec4 sampled1, ivec4 shade, int lodfrac, ivec4 combined)
{
   // Preload static combiner inputs, and only mux in dynamic inputs.
   sub_a = combiner_data.combiners[instance].cycle[c].sub_a;
   sub_b = combiner_data.combiners[instance].cycle[c].sub_b;
   mul   = combiner_data.combiners[instance].cycle[c].mul;
   add   = combiner_data.combiners[instance].cycle[c].add;

   // This is batshit insane :>
   // Finding good ways to optimize this will be a challenge ...
   // Only enabling possible paths in the whole pass to reduce the code bloat might be one approach.
   // Others would be wild multipassing schemes or perhaps indirect compute to dispatch work ...

   // Color
   ivec4 mux = combiner_data.combiners[instance].color[c];
   // SUB-A mux
        if (mux.x == 0) sub_a.rgb = combined.rgb;
   else if (mux.x == 1) sub_a.rgb = sampled0.rgb;
   else if (mux.x == 2) sub_a.rgb = sampled1.rgb;
   else if (mux.x == 4) sub_a.rgb = shade.rgb;
   else if (mux.x == 7) sub_a.rgb = ivec3(sext_9bit(((noise8(1) & 7) << 6) | 0x20));

   // SUB-B mux
        if (mux.y == 0) sub_b.rgb = combined.rgb;
   else if (mux.y == 1) sub_b.rgb = sampled0.rgb;
   else if (mux.y == 2) sub_b.rgb = sampled1.rgb;
   else if (mux.y == 4) sub_b.rgb = shade.rgb;

   // MUL mux
        if (mux.z ==  0) mul.rgb = combined.rgb;
   else if (mux.z ==  1) mul.rgb = sampled0.rgb;
   else if (mux.z ==  2) mul.rgb = sampled1.rgb;
   else if (mux.z ==  4) mul.rgb = shade.rgb;
   else if (mux.z ==  7) mul.rgb = combined.aaa;
   else if (mux.z ==  8) mul.rgb = sampled0.aaa;
   else if (mux.z ==  9) mul.rgb = sampled1.aaa;
   else if (mux.z == 11) mul.rgb = shade.aaa;
   else if (mux.z == 13) mul.rgb = ivec3(lodfrac);

   // ADD mux
        if (mux.w == 0) add.rgb = combined.rgb;
   else if (mux.w == 1) add.rgb = sampled0.rgb;
   else if (mux.w == 2) add.rgb = sampled1.rgb;
   else if (mux.w == 4) add.rgb = shade.rgb;

   // Alpha
   mux = combiner_data.combiners[instance].alpha[c];
        if (mux.x == 0) sub_a.a = combined.a;
   else if (mux.x == 1) sub_a.a = sampled0.a;
   else if (mux.x == 2) sub_a.a = sampled1.a;
   else if (mux.x == 4) sub_a.a = shade.a;

        if (mux.y == 0) sub_b.a = combined.a;
   else if (mux.y == 1) sub_b.a = sampled0.a;
   else if (mux.y == 2) sub_b.a = sampled1.a;
   else if (mux.y == 4) sub_b.a = shade.a;

        if (mux.z == 0) mul.a = lodfrac;
        if (mux.z == 1) mul.a = sampled0.a;
   else if (mux.z == 2) mul.a = sampled1.a;
   else if (mux.z == 4) mul.a = shade.a;

        if (mux.w == 0) add.a = combined.a;
   else if (mux.w == 1) add.a = sampled0.a;
   else if (mux.w == 2) add.a = sampled1.a;
   else if (mux.w == 4) add.a = shade.a;
}

int dither_alpha(uint blendword)
{
   // Alpha dither depends on the color dither, so we need to look up based on the entire dither word.
   if (ALPHA_DITHER_SEL(blendword) == 2u)
   {
      return noise8(2) & 7;
   }
   else
   {
      uint full_dither = FULL_DITHER_SEL(blendword);
      return int(textureLod(uDitherLUT,
            vec3((vec2(gl_GlobalInvocationID.xy) + 0.5) * 0.25,
               float(full_dither)), 0.0).y);
   }
}

#endif
