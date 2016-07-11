#ifndef UTIL_H_
#define UTIL_H_

#define RDP_SUBPIXELS 4
#define RDP_SUBPIXELS_LOG2 2
#define RDP_SUBPIXELS_MASK ((1 << RDP_SUBPIXELS_LOG2) - 1)
#define RDP_SUBPIXELS_INVMASK (~(RDP_SUBPIXELS_MASK))

bvec4 bitwiseOr(bvec4 a, bvec4 b)
{
   return bvec4(ivec4(a) | ivec4(b));
}

bvec4 bitwiseOr(bvec4 a, bvec4 b, bvec4 c)
{
   return bvec4(ivec4(a) | ivec4(b) | ivec4(c));
}

ivec4 bitwiseAnd(bvec4 a, bvec4 b)
{
   return ivec4(a) & ivec4(b);
}

int horizontalMin(ivec4 v)
{
   ivec2 w = min(v.xy, v.zw);
   return min(w.x, w.y);
}

int horizontalMax(ivec4 v)
{
   ivec2 w = max(v.xy, v.zw);
   return max(w.x, w.y);
}

int horizontalOr(ivec4 v)
{
   ivec2 tmp = v.xy | v.zw;
   return tmp.x | tmp.y;
}

int mergeMask(bvec4 a, bvec4 b)
{
   ivec4 masked = ivec4(a) & ivec4(b);
   ivec2 halved = (masked.xy * ivec2(1, 2)) + (masked.zw * ivec2(4, 8));
   return halved.x + halved.y;
}

uint pack_rgba8(uvec4 pixel)
{
   pixel = clamp(pixel, uvec4(0u), uvec4(255u));
   return (pixel.r << 24u) | (pixel.g << 16u) | (pixel.b << 8u) | (pixel.a << 0u);
}

uvec4 unpack_rgba8(uint fbval)
{
   return (uvec4(fbval) >> uvec4(24u, 16u, 8u, 0u)) & uvec4(0xffu);
}

uint pack_s16(ivec2 uv)
{
   return uint((uv.x & 0xffff) | (uv.y << 16));
}

ivec2 unpack_s16(uint x)
{
   // *sigh* ...
   return ivec2((int(x) << 16) >> 16, int(x) >> 16);
}

ivec4 special_9bit_ext(ivec4 x)
{
   // No idea what the idea of this is, but that's what Angrylion seems to be doing ...
   // Maps [0, 0x180) to [0 - 0x180) and [0x180 - 0x1ff] to [-128, -1].
   return ((x + 0x80) & 0x1ff) - 0x80;
}

int sext_9bit(int x)
{
   return (x << 23) >> 23;
}

ivec4 special_9bit_clamp(ivec4 x)
{
   return clamp(special_9bit_ext(x), ivec4(0), ivec4(255));
}

#endif
