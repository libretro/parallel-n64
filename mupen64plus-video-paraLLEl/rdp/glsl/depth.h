#ifndef DEPTH_H_
#define DEPTH_H_

#include "buffers.h"

int dz_compress(int dz)
{
   int j = 0;
   // This looks similar to a log2 (CLZ), but it's not quite so sadly ...
   if ((dz & 0xff00) != 0)
      j |= 8;
   if ((dz & 0xf0f0) != 0)
      j |= 4;
   if ((dz & 0xcccc) != 0)
      j |= 2;
   if ((dz & 0xaaaa) != 0)
      j |= 1;
   return j;
}

uint z_encode_fp(int mantissa, int exponent)
{
   return uint((mantissa & 0x7ff) | (exponent << 11));
}

uint z_encode(int z)
{
   int high = (z >> 11) & 0x7f;
   ivec2 v = depth_lut.encode[high];
   return z_encode_fp(z >> v.x, v.y);
}

int z_decode(uint z)
{
   int exp = (int(z) >> 11) & 7;
   int mantissa = int(z) & 0x7ff;
   ivec2 v = depth_lut.decode[exp];
   return ((mantissa << v.x) + v.y) & 0x3ffff;
}

uvec2 unpack_depth(uint word)
{
   return uvec2((word >> 4u) & 0x3fffu, word & 0xfu);
}

uint pack_depth(uvec2 pixel_z)
{
   return (pixel_z.x << 4u) | (pixel_z.y & 0xfu);
}

bool z_compare(uint flags, int z, int dz, uvec2 pixel_z,
               bool overflow, out bool out_farther)
{
   if (PRIMITIVE_Z_COMPARE(flags))
   {
      int dzmem = int(1u << pixel_z.y);
      int oz = z_decode(pixel_z.x);

      int precision_factor = (z >> 13) & 0xf;
      bool force_coplanar = false;
      if (precision_factor < 3)
      {
         if (dzmem != 0x8000)
         {
            dzmem = max(dzmem << 1, 16 >> precision_factor);
         }
         else
         {
            dzmem = 0xffff;
            force_coplanar = true;
         }
      }

      // Due to dz_normalize, we wouldn't ever see 0 here.
      int newdz = 1 << findMSB(dzmem | dz | 1);
      newdz <<= 3;

      bool z_compare = false;
      bool nearer = force_coplanar || ((z - newdz) <= oz);
      bool farther = force_coplanar || ((z + newdz) >= oz);

      bool infront = z < oz;
      bool zmax = oz == 0x3ffff;

      uint zmode = PRIMITIVE_Z_MODE(flags);
      if (zmode == ZMODE_OPAQUE)
         z_compare = zmax || (overflow ? infront : nearer);
      else if (zmode == ZMODE_INTERPENETRATING)
      {
         if (!infront || !farther || !overflow)
         {
            z_compare = zmax || (overflow ? infront : nearer);
         }
         else
         {
            // Should modify coverage here.
            z_compare = true;
         }
      }
      else if (zmode == ZMODE_TRANSPARENT)
         z_compare = zmax || infront;
      else
         z_compare = farther && nearer && !zmax;

      out_farther = farther;
      return z_compare;
   }
   else
   {
      out_farther = true;
      return true;
   }
}

#endif
