#ifndef RASTERIZER_H_
#define RASTERIZER_H_

#include "coverage.h"
#include "clip.h"

uvec2 compute_coverage_counts(ivec4 lx, ivec4 rx, int xbase)
{
   uvec2 counts = uvec2(0u);
   for (int i = 0; i < 4; i++)
      counts.x |= compute_coverage(lx, rx, xbase + i) << uint(8 * i);
   for (int i = 0; i < 4; i++)
      counts.y |= compute_coverage(lx, rx, xbase + 4 + i) << uint(8 * i);
   return counts;
}

void compute_span(uint primitive, uint scanline, RDPPrimitive prim)
{
   int y = int(gl_WorkGroupID.y * TILE_SIZE_Y + scanline) * RDP_SUBPIXELS;

   // Unpack Y scissor.
   ivec2 clip = unpack_scissor(prim.scissor_y);

   // Scissor test and find Y bounds for where to rasterize.
   int ycur = prim.yh & RDP_SUBPIXELS_INVMASK;
   int yhlimit = max(prim.yh, clip.x);
   int yllimit = min(prim.yl, clip.y);
   int ylfar = yllimit | RDP_SUBPIXELS_MASK;

   // Reject right away.
   if ((y + RDP_SUBPIXELS) <= yhlimit || y >= ylfar)
   {
      span_buffer[scanline][primitive] = 0u;
      return;
   }

   // Shifts and masks already applied on CPU.
   int DxMDy = prim.DxMDy;
   int DxHDy = prim.DxHDy;
   int DxLDy = prim.DxLDy;
   int xm = prim.xm;
   int xh = prim.xh;
   int xl = prim.xl;

   // Compute X bounds, for all 4 Y subpixels.
   // Use branchless mux logic to select appropriate version of xlr0.
   ivec4 xlr0_above = xm + DxMDy * ((y - ycur) + ivec4(0, 1, 2, 3));
   ivec4 xlr0_below = xl + DxLDy * ((y - prim.ym) + ivec4(0, 1, 2, 3));
   ivec4 xlr0 = mix(xlr0_below, xlr0_above, lessThan(y + ivec4(0, 1, 2, 3), ivec4(prim.ym)));
   ivec4 xlr1 = xh + DxHDy * ((y - ycur) + ivec4(0, 1, 2, 3));
   
   // Clip X.
   bvec2 curunder, curover;
   ivec4 xlsc, xrsc;

   // Unpack X scissor.
   clip = unpack_scissor(prim.scissor_x) << 1;
   clip_x(clip, xlr0, xlsc, curunder.x, curover.x);
   clip_x(clip, xlr1, xrsc, curunder.y, curover.y);

   bool flip = PRIMITIVE_IS_FLIP(prim.flags);

   // Detect where the two lines cross.
   // Get a boolean mask for which subscanlines have crossing scanlines.
   // XXX: Shouldn't this be <=?
   // XXX: Also, why the weird bitmath here?
   bvec4 clipped = flip ?
      lessThan((xlr0 & 0xfffc000) ^ 0x08000000, (xlr1 & 0xfffc000) ^ 0x08000000) :
      lessThan((xlr1 & 0xfffc000) ^ 0x08000000, (xlr0 & 0xfffc000) ^ 0x08000000);

   // Add subpixel Y clip here.
   clipped = bitwiseOr(clipped,
      lessThan(y + ivec4(0, 1, 2, 3), ivec4(yhlimit)),
      greaterThanEqual(y + ivec4(0, 1, 2, 3), ivec4(yllimit)));

   // TODO: Missing sckeepodd, scfield here. Interlacing?
   // If we can invalidate the scanline for all subpixels in at least one of three ways, we can kill it early.
   bool invalid = all(clipped) || all(curunder) || all(curover);
   if (invalid)
   {
      span_buffer[scanline][primitive] = 0u;
      return;
   }

   int xbase = int(gl_WorkGroupID.x * TILE_SIZE_X);

   // Find the range where at least one subsample will hit a pixel in this scanline.
   // Quantize to whole pixel as well.
   int lx, rx;

   if (flip)
   {
      // Intentionally clip out invalid scanlines so we don't get coverage for them and that they don't participate in min/max.
      xrsc = mix(xrsc, ivec4(0x7fff), clipped);
      xlsc = mix(xlsc, ivec4(0), clipped);

      // Don't include invalid subsample scanlines in min/max.
      lx = horizontalMin(xrsc >> 3);
      rx = horizontalMax(xlsc >> 3);

      // Find multisampled coverage.
      uvec2 coverage = compute_coverage_counts(xrsc, xlsc, xbase);
      coverage_buffer[scanline][primitive] = coverage;
   }
   else
   {
      // Intentionally clip out invalid scanlines so we don't get coverage for them and that they don't participate in min/max.
      xlsc = mix(xlsc, ivec4(0x7fff), clipped);
      xrsc = mix(xrsc, ivec4(0), clipped);

      // Don't include invalid subsample scanlines in min/max.
      lx = horizontalMin(xlsc >> 3);
      rx = horizontalMax(xrsc >> 3);

      // Find multisampled coverage.
      uvec2 coverage = compute_coverage_counts(xlsc, xrsc, xbase);
      coverage_buffer[scanline][primitive] = coverage;
   }

   int mask0 = mergeMask(
         greaterThanEqual(xbase + ivec4(0, 1, 2, 3), ivec4(lx)),
         lessThanEqual(xbase + ivec4(0, 1, 2, 3), ivec4(rx)));
   int mask1 = mergeMask(
         greaterThanEqual(xbase + ivec4(4, 5, 6, 7), ivec4(lx)),
         lessThanEqual(xbase + ivec4(4, 5, 6, 7), ivec4(rx)));

   uint cov = uint(mask0 + mask1 * 16);
   span_buffer[scanline][primitive] = cov;

   if (cov == 0u)
      return;

   // Interpolate varyings in Y.
   if (PRIMITIVE_INTERPOLATE_VARYINGS(prim.flags))
   {
      // Varyings are interpolated along the major axis.
      // For some reason, we sample at the 1st or 4th subpixel depending on some flags computed earlier ...
      int xlr = PRIMITIVE_DO_OFFSET(prim.flags) ? xlr1.w : xlr1.x;
      int unscrx = xlr >> 16;
      int xfrac = (xlr >> 8) & 0xff;
      ivec4 d_rgba_dxh = ivec4(0);
      ivec4 d_stwz_dxh = ivec4(0);

      if (!PRIMITIVE_CYCLE_TYPE_IS_COPY(prim.flags))
      {
         d_rgba_dxh = (prim.attr.d_rgba_dx >> 8) & ~1;
         d_stwz_dxh = (prim.attr.d_stwz_dx >> 8) & ~1;
      }

      ivec4 rgba = prim.attr.rgba + ((y - ycur) >> 2) * prim.attr.d_rgba_de;
      ivec4 stwz = prim.attr.stwz + ((y - ycur) >> 2) * prim.attr.d_stwz_de;
      span_rgba[scanline][primitive] = ((rgba & ivec4(~0x1ff)) + prim.attr.d_rgba_diff - xfrac * d_rgba_dxh) & ivec4(~0x3ff);
      span_stwz[scanline][primitive] = ((stwz & ivec4(~0x1ff)) + prim.attr.d_stwz_diff - xfrac * d_stwz_dxh) & ivec4(~0x3ff);
      span_base_x[scanline][primitive] = unscrx;
   }
}

#endif
