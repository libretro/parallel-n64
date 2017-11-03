#include "frontend.hpp"
#include <assert.h>
#include <string.h>

using namespace std;

namespace RDP
{
static int32_t minimum(int32_t x0, int32_t x1, int32_t x2, int32_t x3, int32_t x4, int32_t x5)
{
   x0 = min(x0, x1);
   x2 = min(x2, x3);
   x4 = min(x4, x5);
   x0 = min(x0, x2);
   return min(x0, x4);
}

static int32_t maximum(int32_t x0, int32_t x1, int32_t x2, int32_t x3, int32_t x4, int32_t x5)
{
   x0 = max(x0, x1);
   x2 = max(x2, x3);
   x4 = max(x4, x5);
   x0 = max(x0, x2);
   return max(x0, x4);
}

void Frontend::invalid(const uint32_t *)
{
#ifndef ANDROID
   abort();
#endif
}

void Frontend::noop(const uint32_t *)
{
}

void Frontend::tri_fill_tex_coeffs(Attribute *attr, const uint32_t *args)
{
   attr->stwz[0]      = ((args[0] << 0) & 0xffff0000u) | ((args[4] >> 16) & 0xffffu);
   attr->stwz[1]      = ((args[0] << 16) & 0xffff0000u) | ((args[4] >> 0) & 0xffffu);
   attr->stwz[2]      = ((args[1] << 0) & 0xffff0000u) | ((args[5] >> 16) & 0xffffu);

   attr->d_stwz_dx[0] = ((args[2] << 0) & 0xffff0000u) | ((args[6] >> 16) & 0xffffu);
   attr->d_stwz_dx[1] = ((args[2] << 16) & 0xffff0000u) | ((args[6] >> 0) & 0xffffu);
   attr->d_stwz_dx[2] = ((args[3] << 0) & 0xffff0000u) | ((args[7] >> 16) & 0xffffu);

   attr->d_stwz_de[0] = ((args[8] << 0) & 0xffff0000u) | ((args[12] >> 16) & 0xffffu);
   attr->d_stwz_de[1] = ((args[8] << 16) & 0xffff0000u) | ((args[12] >> 0) & 0xffffu);
   attr->d_stwz_de[2] = ((args[9] << 0) & 0xffff0000u) | ((args[13] >> 16) & 0xffffu);

   attr->d_stwz_dy[0] = ((args[10] << 0) & 0xffff0000u) | ((args[14] >> 16) & 0xffffu);
   attr->d_stwz_dy[1] = ((args[10] << 16) & 0xffff0000u) | ((args[14] >> 0) & 0xffffu);
   attr->d_stwz_dy[2] = ((args[11] << 0) & 0xffff0000u) | ((args[15] >> 16) & 0xffffu);
}

void Frontend::tri_fill_z_coeffs(Attribute *attr, const uint32_t *args)
{
   attr->stwz[3]      = args[0];
   attr->d_stwz_dx[3] = args[1];
   attr->d_stwz_de[3] = args[2];
   attr->d_stwz_dy[3] = args[3];
}

void Frontend::tri_fill_shade_coeffs(Attribute *attr, const uint32_t *args)
{
   attr->rgba[0] = ((args[0] << 0) & 0xffff0000u) | ((args[4] >> 16) & 0xffffu);
   attr->rgba[1] = ((args[0] << 16) & 0xffff0000u) | ((args[4] >> 0) & 0xffffu);
   attr->rgba[2] = ((args[1] << 0) & 0xffff0000u) | ((args[5] >> 16) & 0xffffu);
   attr->rgba[3] = ((args[1] << 16) & 0xffff0000u) | ((args[5] >> 0) & 0xffffu);

   attr->d_rgba_dx[0] = ((args[2] << 0) & 0xffff0000u) | ((args[6] >> 16) & 0xffffu);
   attr->d_rgba_dx[1] = ((args[2] << 16) & 0xffff0000u) | ((args[6] >> 0) & 0xffffu);
   attr->d_rgba_dx[2] = ((args[3] << 0) & 0xffff0000u) | ((args[7] >> 16) & 0xffffu);
   attr->d_rgba_dx[3] = ((args[3] << 16) & 0xffff0000u) | ((args[7] >> 0) & 0xffffu);

   attr->d_rgba_de[0] = ((args[8] << 0) & 0xffff0000u) | ((args[12] >> 16) & 0xffffu);
   attr->d_rgba_de[1] = ((args[8] << 16) & 0xffff0000u) | ((args[12] >> 0) & 0xffffu);
   attr->d_rgba_de[2] = ((args[9] << 0) & 0xffff0000u) | ((args[13] >> 16) & 0xffffu);
   attr->d_rgba_de[3] = ((args[9] << 16) & 0xffff0000u) | ((args[13] >> 0) & 0xffffu);

   attr->d_rgba_dy[0] = ((args[10] << 0) & 0xffff0000u) | ((args[14] >> 16) & 0xffffu);
   attr->d_rgba_dy[1] = ((args[10] << 16) & 0xffff0000u) | ((args[14] >> 0) & 0xffffu);
   attr->d_rgba_dy[2] = ((args[11] << 0) & 0xffff0000u) | ((args[15] >> 16) & 0xffffu);
   attr->d_rgba_dy[3] = ((args[11] << 16) & 0xffff0000u) | ((args[15] >> 0) & 0xffffu);
}

void Frontend::tri_fill_coeffs(Primitive *prim, const uint32_t *args, int32_t *xmin, int32_t *xmax)
{
   uint32_t right_major = (args[0] >> (55 - 32)) & 1;

   /* 11.2 fixed point */
   int32_t yl = SEXT((args[0] >> 0) & 0xffff, 14);
   int32_t ym = SEXT((args[1] >> 16) & 0xffff, 14);
   int32_t yh = SEXT((args[1] >> 0) & 0xffff, 14);

   /* 14.16 fixed point */
   int32_t xl = SEXT(args[2], 30) & ~1;
   int32_t xh = SEXT(args[4], 30) & ~1;
   int32_t xm = SEXT(args[6], 30) & ~1;

   /* 16.16 fixed point. */
   int32_t DxLDy = (int32_t(args[3]) >> 2) & ~1;
   int32_t DxHDy = (int32_t(args[5]) >> 2) & ~1;
   int32_t DxMDy = (int32_t(args[7]) >> 2) & ~1;

   // Compute screen space bounding box for primitive.
   // Find all possible X edges.
   //
   // Should take into account scissor box as well perhaps to get tighter X box,
   // but if we have scissor, the extra scissor clip in draw_primitive might do a good job ...
   //
   // FIXME: Can we have overflow here?
   //
   // Major slope
   int32_t x0 = xh;
   int32_t x1 = xh + (yl - (yh & ~3)) * DxHDy;
   // Upper minor slope.
   int32_t x2 = xm;
   int32_t x3 = xm + (ym - (yh & ~3)) * DxMDy;
   // Lower minor slope.
   int32_t x4 = xl;
   int32_t x5 = xl + (yl - ym) * DxLDy;

   *xmin = minimum(x0, x1, x2, x3, x4, x5) >> 16;
   *xmax = maximum(x0, x1, x2, x3, x4, x5) >> 16;

   prim->xl = xl;
   prim->xm = xm;
   prim->xh = xh;

   prim->yl = yl;
   prim->ym = ym;
   prim->yh = yh;

   prim->DxLDy = DxLDy;
   prim->DxHDy = DxHDy;
   prim->DxMDy = DxMDy;

   prim->flags = right_major ? RDP_FLAG_FLIP : 0;
}

void Frontend::tri_fill_flags_common(Primitive *prim)
{
   prim->flags |= other_modes.cycle_type << RDP_FLAG_CYCLE_TYPE_SHIFT;
   prim->flags |= other_modes.cvg_dest << RDP_FLAG_CVG_MODE_SHIFT;
   prim->flags |= other_modes.force_blend ? RDP_FLAG_FORCE_BLEND : 0;
   prim->flags |= other_modes.antialias_en ? RDP_FLAG_AA_ENABLE : 0;
   prim->flags |= other_modes.alpha_compare_en ? RDP_FLAG_ALPHATEST : 0;
   prim->flags |= other_modes.dither_alpha_en ? RDP_FLAG_ALPHA_NOISE : 0;
   prim->flags |= other_modes.color_on_cvg ? RDP_FLAG_COLOR_ON_CVG : 0;
   prim->flags |= other_modes.cvg_times_alpha ? RDP_FLAG_CVG_TIMES_ALPHA : 0;
   prim->flags |= other_modes.alpha_cvg_select ? RDP_FLAG_ALPHA_CVG_SELECT : 0;

   prim->flags |= other_modes.z_update_en ? RDP_FLAG_Z_UPDATE : 0;
   prim->flags |= other_modes.z_compare_en ? RDP_FLAG_Z_COMPARE : 0;
   prim->flags |= other_modes.z_mode << RDP_FLAG_Z_MODE_SHIFT;

   if (other_modes.sample_type)
   {
      prim->flags |= other_modes.bi_lerp0 ? RDP_FLAG_BILERP0 : 0;
      prim->flags |= other_modes.bi_lerp1 ? RDP_FLAG_BILERP1 : 0;
   }

   if (other_modes.persp_tex_en && (other_modes.cycle_type == CYCLE_TYPE_1 || other_modes.cycle_type == CYCLE_TYPE_2))
      prim->flags |= RDP_FLAG_PERSPECTIVE;
}

void Frontend::tri_fill_tile(Primitive *prim, uint32_t *tile_mask, uint32_t mips, uint32_t tile)
{
	// This should depend on whether or not we'll need the second tile in the combiner.
	if (!other_modes.tex_lod_en)
	{
		if (other_modes.cycle_type == CYCLE_TYPE_2)
		{
			mips = renderer->combiner_reads_secondary_tile(0) || renderer->combiner_reads_secondary_tile(1) ? 2 : 1;
			//renderer->log_combiner();
		}
		else if (other_modes.cycle_type == CYCLE_TYPE_1)
		{
			mips = 1;
			//mips = renderer->combiner_reads_secondary_tile(1) ? 2 : 1;
			//renderer->log_combiner();
		}
	}
	else
	{
		prim->flags |= RDP_FLAG_SAMPLE_TEX_LOD;
		// Detail mip-chains have an extra LOD level.
		if (other_modes.detail_tex_en)
			mips = min(mips + 1u, 8u);
	}

	if (other_modes.cycle_type == CYCLE_TYPE_2)
	{
		bool pipelined = renderer->combiner_reads_pipelined_tile();
		//if (pipelined)
		//   fprintf(stderr, "PIPELINED!\n");
		prim->flags |= pipelined ? RDP_FLAG_SAMPLE_TEX_PIPELINED : 0;
	}

	//fprintf(stderr, "Cycle mode: %u, Mips: %u\n", other_modes.cycle_type, mips);

	*tile_mask = 0;
	for (unsigned i = 0; i < mips; i++)
		*tile_mask |= 1 << ((tile + i) & 7);

	prim->flags |= RDP_FLAG_SAMPLE_TEX;
	prim->flags |= tile << RDP_FLAG_TILE_SHIFT;
	prim->flags |= (mips - 1) << RDP_FLAG_LEVELS_SHIFT;
}

void Frontend::tri_fill_novarying_tile(Primitive *prim, Attribute *attr, uint32_t *tile_mask)
{
   /* It is possible for some bizarro reason to read a texture 
    * even though we don't have varyings for it.
    *
    * In this case, we end up passing down 0 
    * for s, t and w. To work around this, 
    * we just enable sampling. */
   tri_fill_tile(prim, tile_mask, 1, 0);

   /* Make sure attributes are cleared to zero. */
   if (attr)
   {
      unsigned i;
      for (i = 0; i < 3; i++)
      {
	      attr->stwz[i]      = 0;
	      attr->d_stwz_dx[i] = 0;
	      attr->d_stwz_dy[i] = 0;
	      attr->d_stwz_de[i] = 0;
      }
   }
}

void Frontend::tri_fill_flags(Primitive *prim, const uint32_t *args, uint32_t *tile_mask, bool zbuffer, bool shade)
{
   if (tile_mask)
   {
      uint32_t mips = (args[0] & 0x00380000) >> (51 - 32);
      uint32_t tile = (args[0] & 0x00070000) >> (48 - 32);
      mips++;

      tri_fill_tile(prim, tile_mask, mips, tile);
   }
   tri_fill_flags_common(prim);
   prim->flags |= zbuffer && !other_modes.z_source_sel ? RDP_FLAG_INTERPOLATE_Z : 0;
   prim->flags |= shade ? RDP_FLAG_SHADE : 0;
}

void Frontend::tri_noshade(const uint32_t *args)
{
   int xmin, xmax;
   Primitive prim;
   uint32_t tile_mask = 0;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_flags(&prim, args, nullptr, false, false);

   if (renderer->combiner_reads_tile(static_cast<CycleType>(other_modes.cycle_type)))
      tri_fill_novarying_tile(&prim, nullptr, &tile_mask);
   renderer->draw_primitive(prim, nullptr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_noshade_z(const uint32_t *args)
{
   int xmin, xmax;
   Primitive prim;
   Attribute attr;
   uint32_t tile_mask = 0;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_z_coeffs(&attr, args + 8);
   tri_fill_flags(&prim, args, nullptr, true, false);

   if (renderer->combiner_reads_tile(static_cast<CycleType>(other_modes.cycle_type)))
      tri_fill_novarying_tile(&prim, &attr, &tile_mask);
   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_tex(const uint32_t *args)
{
   int xmin, xmax;
   Primitive prim;
   Attribute attr;
   uint32_t tile_mask = 0;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_tex_coeffs(&attr, args + 8);
   tri_fill_flags(&prim, args, &tile_mask, false, false);

   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_tex_z(const uint32_t *args)
{
   int xmin, xmax;
   uint32_t tile_mask;
   Primitive prim;
   Attribute attr;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_tex_coeffs(&attr, args + 8);
   tri_fill_z_coeffs(&attr, args + 24);
   tri_fill_flags(&prim, args, &tile_mask, true, false);

   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_shade(const uint32_t *args)
{
   int xmin, xmax;
   Primitive prim;
   Attribute attr;
   uint32_t tile_mask = 0;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_shade_coeffs(&attr, args + 8);
   tri_fill_flags(&prim, args, nullptr, false, true);

   if (renderer->combiner_reads_tile(static_cast<CycleType>(other_modes.cycle_type)))
      tri_fill_novarying_tile(&prim, &attr, &tile_mask);
   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_shade_z(const uint32_t *args)
{
   int xmin, xmax;
   Primitive prim;
   Attribute attr;
   uint32_t tile_mask = 0;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_shade_coeffs(&attr, args + 8);
   tri_fill_z_coeffs(&attr, args + 24);
   tri_fill_flags(&prim, args, nullptr, true, true);

   if (renderer->combiner_reads_tile(static_cast<CycleType>(other_modes.cycle_type)))
      tri_fill_novarying_tile(&prim, &attr, &tile_mask);

   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_shade_tex(const uint32_t *args)
{
   int xmin, xmax;
   uint32_t tile_mask;
   Primitive prim;
   Attribute attr;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_shade_coeffs(&attr, args + 8);
   tri_fill_tex_coeffs(&attr, args + 24);
   tri_fill_flags(&prim, args, &tile_mask, false, true);

   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tri_shade_tex_z(const uint32_t *args)
{
   int xmin, xmax;
   uint32_t tile_mask;
   Primitive prim;
   Attribute attr;

   tri_fill_coeffs(&prim, args, &xmin, &xmax);
   tri_fill_shade_coeffs(&attr, args + 8);
   tri_fill_tex_coeffs(&attr, args + 24);
   tri_fill_z_coeffs(&attr, args + 40);
   tri_fill_flags(&prim, args, &tile_mask, true, true);

   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, prim.yh >> 2, prim.yl >> 2);
}

void Frontend::tex_rect_common(const uint32_t *args, bool flipped)
{
   Primitive prim;
   Attribute attr;
   int32_t s, t, dsdx, dtdy;
   uint32_t tile_mask = 0;
   int xl             = (args[0] & 0xfff000u) >> (44 - 32);
   int yl             = (args[0] & 0x000fffu) >> (32 - 32);
   unsigned tile      = (args[1] & 0x07000000) >> 24;
   int xh             = (args[1] & 0xfff000u) >> (12 - 0);
   int yh             = (args[1] & 0x000fffu) >> (0 - 0);

   int xmin           = xh >> 2;
   int xmax           = xl >> 2;

   /* Convert qpel to 16.16 fixed point format. */
   xl <<= 14;
   xh <<= 14;

   yl |= (other_modes.cycle_type & 2) ? 3 : 0; // FILL or COPY.

   prim.xl = xl;
   prim.xm = xl;
   prim.xh = xh;

   prim.yl = yl;
   prim.ym = yl;
   prim.yh = yh;

   // Rect, so keep these as 0.
   prim.DxLDy = 0;
   prim.DxHDy = 0;
   prim.DxMDy = 0;
   prim.flags = RDP_FLAG_FLIP;

   memset(&attr, 0, sizeof(attr));

   s    = SEXT((args[2] & 0xffff0000u) >> 16, 16);
   t    = SEXT((args[2] & 0x0000ffffu) >> 0, 16);
   dsdx = SEXT((args[3] & 0xffff0000u) >> 16, 16);
   dtdy = SEXT((args[3] & 0x0000ffffu) >> 0, 16);

   if (flipped)
   {
      attr.stwz[0] = s << 16;
      attr.stwz[1] = t << 16;
      attr.d_stwz_dx[1] = dtdy << 11;
      attr.d_stwz_de[0] = dsdx << 11;
      attr.d_stwz_dy[0] = dsdx << 11;
   }
   else
   {
      attr.stwz[0] = s << 16;
      attr.stwz[1] = t << 16;
      attr.d_stwz_dx[0] = dsdx << 11;
      attr.d_stwz_de[1] = dtdy << 11;
      attr.d_stwz_dy[1] = dtdy << 11;
   }

   tri_fill_flags(&prim, args, nullptr, false, false);
   tri_fill_tile(&prim, &tile_mask, 1, tile);

   if (other_modes.cycle_type == CYCLE_TYPE_COPY)
   {
      /* No texture filtering allowed in copy mode. */

      prim.flags &= ~RDP_FLAG_BILERP0;
      prim.flags &= ~RDP_FLAG_BILERP1;

      // Copy mode jumps 4 pixels at a time, so instead of implementing that, just shift dSdx by 2 instead.
      // Technically, very awkward use of texture rectangle can break this, but we'll deal with it if needed.
      attr.d_stwz_dx[0] >>= 2;
   }

   renderer->draw_primitive(prim, &attr, tile_mask, xmin, xmax, yh >> 2, yl >> 2);
}

void Frontend::tex_rect(const uint32_t *args)
{
   tex_rect_common(args, false);
}

void Frontend::tex_rect_flip(const uint32_t *args)
{
   tex_rect_common(args, true);
}

void Frontend::set_key_gb(const uint32_t *args)
{
   renderer->set_key_gb(args[0], args[1]);
}

void Frontend::set_key_r(const uint32_t *args)
{
   renderer->set_key_r(args[0], args[1]);
}

void Frontend::sync_load(const uint32_t *)
{
}

void Frontend::sync_pipe(const uint32_t *)
{
}

void Frontend::sync_tile(const uint32_t *)
{
}

void Frontend::sync_full(const uint32_t *)
{
   renderer->complete_frame();
}

void Frontend::fill_rect(const uint32_t *args)
{
   Primitive prim;
   int xl = (args[0] & 0xfff000u) >> (44 - 32);
   int yl = (args[0] & 0x000fffu) >> (32 - 32);
   int xh = (args[1] & 0xfff000u) >> (12 - 0);
   int yh = (args[1] & 0x000fffu) >> (0 - 0);

   int xmin = xh >> 2;
   int xmax = xl >> 2;

   // Convert qpel to 16.16 fixed point format.
   xl <<= 14;
   xh <<= 14;

   yl |= (other_modes.cycle_type & 2) ? 3 : 0; // FILL or COPY.

   prim.xl = xl;
   prim.xm = xl;
   prim.xh = xh;

   prim.yl = yl;
   prim.ym = yl;
   prim.yh = yh;

   // Rect, so keep these as 0.
   prim.DxLDy = 0;
   prim.DxHDy = 0;
   prim.DxMDy = 0;
   prim.flags = RDP_FLAG_FLIP;

   tri_fill_flags(&prim, args, nullptr, false, false);

   renderer->draw_primitive(prim, nullptr, 0, xmin, xmax, yh >> 2, yl >> 2);
}

void Frontend::set_other_modes(const uint32_t *args)
{
   other_modes.cycle_type       = (args[0] & 0x00300000) >> (52 - 32);
   other_modes.persp_tex_en     = !!(args[0] & 0x00080000);
   other_modes.detail_tex_en    = !!(args[0] & 0x00040000);
   other_modes.sharpen_tex_en   = !!(args[0] & 0x00020000);
   other_modes.tex_lod_en       = !!(args[0] & 0x00010000);
   other_modes.en_tlut          = !!(args[0] & 0x00008000);
   other_modes.tlut_type        = !!(args[0] & 0x00004000);
   other_modes.sample_type      = !!(args[0] & 0x00002000);
   other_modes.mid_texel        = !!(args[0] & 0x00001000);
   other_modes.bi_lerp0         = !!(args[0] & 0x00000800);
   other_modes.bi_lerp1         = !!(args[0] & 0x00000400);
   other_modes.convert_one      = !!(args[0] & 0x00000200);
   other_modes.key_en           = !!(args[0] & 0x00000100);
   other_modes.rgb_dither_sel   = (args[0] & 0x000000C0) >> (36 - 32);
   other_modes.alpha_dither_sel = (args[0] & 0x00000030) >> (36 - 32);

   other_modes.force_blend      = !!(args[1] & 0x00004000);
   other_modes.alpha_cvg_select = !!(args[1] & 0x00002000);
   other_modes.cvg_times_alpha  = !!(args[1] & 0x00001000);
   other_modes.z_mode           = (args[1] & 0x00000c00) >> (10 - 0);
   other_modes.cvg_dest         = (args[1] & 0x00000300) >> (8 - 0);
   other_modes.color_on_cvg     = !!(args[1] & 0x00000080);
   other_modes.image_read_en    = !!(args[1] & 0x00000040);
   other_modes.z_update_en      = !!(args[1] & 0x00000020);
   other_modes.z_compare_en     = !!(args[1] & 0x00000010);
   other_modes.antialias_en     = !!(args[1] & 0x00000008);
   other_modes.z_source_sel     = !!(args[1] & 0x00000004);
   other_modes.dither_alpha_en  = !!(args[1] & 0x00000002);
   other_modes.alpha_compare_en = !!(args[1] & 0x00000001);

   renderer->set_lod_modes(other_modes.detail_tex_en, other_modes.sharpen_tex_en);

   uint32_t blendword = (args[1] >> 16) & 0xffff;

   /* memory_color in cycle 0 makes no sense. */
   if (other_modes.cycle_type == CYCLE_TYPE_2)
      assert(((blendword >> 14) & 3) != 1);

   renderer->get_tmem().set_enable_tlut(other_modes.en_tlut);
   renderer->get_tmem().set_tlut_type(other_modes.tlut_type);
   renderer->set_blend_word(blendword, other_modes.alpha_dither_sel);
}

void Frontend::set_combine(const uint32_t *args)
{
   renderer->set_combine(args[0], args[1]);
}

void Frontend::set_convert(const uint32_t *args)
{
   renderer->set_convert(args[0], args[1]);
}

void Frontend::set_scissor(const uint32_t *args)
{
   int xh = (args[0] & 0x00fff000) >> (44 - 32);
   int yh = (args[0] & 0x00000fff) >> (32 - 32);
   // TODO: Interlacing
   int xl = (args[1] & 0x00fff000) >> (12 - 0);
   int yl = (args[1] & 0x00000fff) >> (0 - 0);

   renderer->set_scissor(xh, yh, xl, yl);
}

void Frontend::set_prim_depth(const uint32_t *args)
{
   renderer->set_primitive_z(args[1]);
}

void Frontend::set_fill_color(const uint32_t *args)
{
   renderer->set_fill_color(args[1]);
}

void Frontend::set_blend_color(const uint32_t *args)
{
   renderer->set_blend_color(args[1]);
}

void Frontend::set_prim_color(const uint32_t *args)
{
   renderer->set_prim_color(args[0], args[1]);
}

void Frontend::set_env_color(const uint32_t *args)
{
   renderer->set_env_color(args[1]);
}

void Frontend::set_fog_color(const uint32_t *args)
{
   renderer->set_fog_color(args[1]);
}

void Frontend::load_tile(const uint32_t *args)
{
   renderer->check_tmem_feedback();
   renderer->get_tmem().load_tile(args[0], args[1]);
}

void Frontend::set_tile_size(const uint32_t *args)
{
   renderer->get_tmem().set_tile_size(args[0], args[1]);
}

void Frontend::load_block(const uint32_t *args)
{
   renderer->check_tmem_feedback();
   renderer->get_tmem().load_block(args[0], args[1]);
}

void Frontend::load_tlut(const uint32_t *args)
{
   renderer->check_tmem_feedback();
   renderer->get_tmem().load_tlut(args[0], args[1]);
}

void Frontend::set_tile(const uint32_t *args)
{
   renderer->get_tmem().set_tile(args[0], args[1]);
}

void Frontend::set_texture_image(const uint32_t *args)
{
   unsigned format     = (args[0] & 0x00e00000) >> (53 - 32);
   unsigned pixel_size = (args[0] & 0x00180000) >> (51 - 32);
   unsigned width      = (args[0] & 0x000003ff) >> (32 - 32);
   unsigned addr       = (args[1] & 0x03ffffff) >> (0 - 0);

   width++;

   renderer->get_tmem().set_texture_image(addr, format, pixel_size, width);
}

void Frontend::set_mask_image(const uint32_t *args)
{
   unsigned addr = args[1] & 0x00ffffff;
   renderer->set_depth_image(addr);
}

void Frontend::set_color_image(const uint32_t *args)
{
   unsigned format     = (args[0] & 0x00e00000) >> (53 - 32);
   unsigned pixel_size = (args[0] & 0x00180000) >> (51 - 32);
   unsigned width      = (args[0] & 0x000003ff) >> (32 - 32);
   unsigned addr       = (args[1] & 0x03ffffff) >> (0 - 0);

   width++;

   renderer->set_color_image(addr, format, pixel_size, width);
}


const Frontend::rdp_func_t Frontend::rdp_commands[64] = {
	&Frontend::noop,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::tri_noshade,
	&Frontend::tri_noshade_z,
	&Frontend::tri_tex,
	&Frontend::tri_tex_z,
	&Frontend::tri_shade,
	&Frontend::tri_shade_z,
	&Frontend::tri_shade_tex,
	&Frontend::tri_shade_tex_z,

	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,

	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::invalid,
	&Frontend::tex_rect,
	&Frontend::tex_rect_flip,
	&Frontend::sync_load,
	&Frontend::sync_pipe,
	&Frontend::sync_tile,
	&Frontend::sync_full,
	&Frontend::set_key_gb,
	&Frontend::set_key_r,
	&Frontend::set_convert,
	&Frontend::set_scissor,
	&Frontend::set_prim_depth,
	&Frontend::set_other_modes,

	&Frontend::load_tlut,
	&Frontend::invalid,
	&Frontend::set_tile_size,
	&Frontend::load_block,
	&Frontend::load_tile,
	&Frontend::set_tile,
	&Frontend::fill_rect,
	&Frontend::set_fill_color,
	&Frontend::set_fog_color,
	&Frontend::set_blend_color,
	&Frontend::set_prim_color,
	&Frontend::set_env_color,
	&Frontend::set_combine,
	&Frontend::set_texture_image,
	&Frontend::set_mask_image,
	&Frontend::set_color_image,
};

void Frontend::command(uint32_t cmd, const uint32_t *args)
{
   assert(renderer);
   assert(cmd < 64);

   if (trace && ((cmd >= 8 && cmd < 16) || cmd == 0x24 || cmd == 0x25 || cmd == 0x36))
      draw_count++;

   if (rdp_commands[cmd])
      (this->*rdp_commands[cmd])(args);
   else
      fprintf(stderr, "Unimplemented OP: 0x%x.\n", cmd);
}
}
