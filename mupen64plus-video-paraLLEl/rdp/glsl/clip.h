#ifndef CLIP_H_
#define CLIP_H_

void clip_x(ivec2 clip, ivec4 xs, out ivec4 xsc, out bool curunder, out bool curover)
{
   // Quantize X bounds down to 1/8th pixel resolution.
   // Apply scissor and clipping in X.
   // Very finicky math follows, see Angrylion.
   ivec4 stickybit = ivec4(bvec4(xs & 0x3fff));
   xsc = ((xs >> 13) & ~1) | stickybit;

   // Clip against left side.
   bvec4 under = lessThan(xsc, clip.xxxx);
   xsc = mix(((xs >> 13) & 0x3ffe) | stickybit, clip.xxxx, under);

   // Clip against right side.
   xsc = xsc & 0x3fff;
   bvec4 over = greaterThanEqual(xsc, clip.yyyy);
   xsc = mix(xsc, clip.yyyy, over);

   curunder = all(under);
   curover = all(over);
}

ivec2 unpack_scissor(uint word)
{
   return ivec2((uvec2(word) >> uvec2(0u, 16u)) & uvec2(0xffff));
}

#endif
