#ifndef TEXTURES_H_
#define TEXTURES_H_

vec2 uv_wrap(vec2 offset, ivec2 uv, ivec2 mirror, ivec2 mask)
{
   // Mirroring
   bvec2 no_mirror = equal(uv & mirror, ivec2(0));
   ivec2 st = uv ^ mix(ivec2(-1), ivec2(0), no_mirror);

   // Masking
   return vec2(st & mask) * constants.InvSizeTilemap + offset;
}

#define desc tile_descriptors.descriptors[tile]
ivec4 sample_tile(uint tile, ivec2 st, int bilerp)
{
   // Apply shift
   st = mix(st >> TILE_SHIFT(desc), st << (16 - TILE_SHIFT(desc)), greaterThan(TILE_SHIFT(desc), ivec2(10)));

   // Shift to tile base
   st -= TILE_BASE(desc);

   // Clamp. We always end up clamping to center of texel, so we don't have to clamp per texel at least ...
   st = clamp(st, TILE_CLAMP_MIN(desc), TILE_CLAMP_MAX(desc));

   ivec2 frac = (st & 0x1f) * bilerp;
   st >>= 5;

   // Mirroring
   vec2 uv0 = uv_wrap(TILE_OFFSET(desc), st, TILE_MIRROR(desc), TILE_MASK(desc));
   ivec4 t0 = ivec4(textureLod(uTilemap, vec3(uv0, TILE_LAYER(desc)), 0.0));

   // Nearest, only need to sample one texel.
   if (all(equal(frac, ivec2(0))))
      return t0;

   // Sample rest of quad foot-print.
   vec2 uv1 = uv_wrap(TILE_OFFSET(desc), st + ivec2(1, 0), TILE_MIRROR(desc), TILE_MASK(desc));
   vec2 uv2 = uv_wrap(TILE_OFFSET(desc), st + ivec2(0, 1), TILE_MIRROR(desc), TILE_MASK(desc));
   vec2 uv3 = uv_wrap(TILE_OFFSET(desc), st + ivec2(1, 1), TILE_MIRROR(desc), TILE_MASK(desc));

   ivec4 t1 = ivec4(textureLod(uTilemap, vec3(uv1, TILE_LAYER(desc)), 0.0));
   ivec4 t2 = ivec4(textureLod(uTilemap, vec3(uv2, TILE_LAYER(desc)), 0.0));
   ivec4 t3 = ivec4(textureLod(uTilemap, vec3(uv3, TILE_LAYER(desc)), 0.0));

   // Sample center, average all texels.
   if (TILE_MID_TEXEL(desc) != 0 && all(equal(frac, ivec2(0x10))))
      return ((t0 + t1 + t2 + t3) + 2) >> 2;
   else
   {
      // Infamous three-sample bilerp :3
      bool upper = (frac.x + frac.y) >= 0x20;
      if (upper)
      {
         frac = 0x20 - frac;
         return t3 + ((frac.x * (t2 - t3) + frac.y * (t1 - t3) + 0x10) >> 5);
      }
      else
         return t0 + ((frac.x * (t1 - t0) + frac.y * (t2 - t0) + 0x10) >> 5);
   }
}
#undef desc

#endif
