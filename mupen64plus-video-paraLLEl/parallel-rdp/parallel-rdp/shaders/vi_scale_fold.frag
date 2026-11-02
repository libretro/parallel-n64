#version 450
/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* vi_scale.frag with the passes that used to follow it folded in: the
 * 2:1 downscales (each a linear blit's halfway sample of the level above,
 * rounded as the level was stored) and the deinterlace of a bobbed field (a copy of a
 * row, or the mean of two, a quarter field line up or down). Each output
 * pixel works out the scale pass's pixels it is made of, so the passes'
 * images are never written or read back. A scale pixel the scale pass
 * would not draw is its clear colour, 0, or, inside the rects the scale
 * pass blends the previous frame into (weave), the previous frame's pixel
 * that scale pixel ends up in, as vi_blend_fields.frag gives it. */
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

#include "small_types.h"
#include "vi_status.h"
#include "noise.h"

layout(set = 0, binding = 0) uniform mediump utexture2DArray uDivotOutput;
layout(set = 0, binding = 1) uniform itextureBuffer uHorizontalInfo;
layout(set = 0, binding = 2) uniform texture2D uPrevious;
layout(set = 1, binding = 0) uniform mediump utextureBuffer uGammaTable;
layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    ivec2 frag_coord_offset;
    int v_start;
    int y_add;
    int frame_count;

    int serrate_shift;
    int serrate_mask;
    int serrate_select;

    int info_y_shift;

    /* the scale pass's scissor, in its own coordinates */
    ivec2 rect_lo;
    ivec2 rect_hi;

    /* 2:1 downscales after the scale pass */
    int downscale_steps;

    /* deinterlace: row r of the output reads the downscaled image at
     * (deinterlace_mul * r + deinterlace_add) / 4 rows, from its rows
     * [0, deinterlace_rows); deinterlace_mul 0 for no deinterlace */
    int deinterlace_mul;
    int deinterlace_add;
    int deinterlace_rows;

    /* where the previous frame shows through, in the scale pass's
     * coordinates: empty for none */
    ivec2 blend_lo[3];
    ivec2 blend_hi[3];
} registers;

uvec3 vi_lerp(uvec3 a, uvec3 b, uint l)
{
    return (a + (((b - a) * l + 16u) >> 5u)) & 0xffu;
}

uvec3 integer_gamma(uvec3 color)
{
    uvec3 res;
    if (GAMMA_DITHER)
    {
        color = (color << 6) + noise_get_full_gamma_dither() + 256u;
        res = uvec3(
            texelFetch(uGammaTable, int(color.r)).r,
            texelFetch(uGammaTable, int(color.g)).r,
            texelFetch(uGammaTable, int(color.b)).r);
    }
    else
    {
        res = uvec3(
            texelFetch(uGammaTable, int(color.r)).r,
            texelFetch(uGammaTable, int(color.g)).r,
            texelFetch(uGammaTable, int(color.b)).r);
    }
    return res;
}

layout(constant_id = 2) const bool FETCH_BUG = false;

bool inside(ivec2 p, ivec2 lo, ivec2 hi)
{
    return all(greaterThanEqual(p, lo)) && all(lessThan(p, hi));
}

/* what the scale pass leaves where it does not draw */
uvec4 undrawn(ivec2 pixel)
{
    if (inside(pixel, registers.blend_lo[0], registers.blend_hi[0]) ||
        inside(pixel, registers.blend_lo[1], registers.blend_hi[1]) ||
        inside(pixel, registers.blend_lo[2], registers.blend_hi[2]))
    {
        // A persistent pixel does not propagate more than one frame.
        vec4 prev = texelFetch(uPrevious, pixel >> registers.downscale_steps, 0);
        return uvec4(uvec3(prev.rgb * prev.a * 255.0 + 0.5), 0u);
    }
    return uvec4(0u);
}

/* The scale pass's pixel at 'pixel' of its image, alpha 255 where it draws:
 * vi_scale.frag's main(), with discard returning what is left there. */
uvec4 scale_pixel(ivec2 pixel)
{
    if (!inside(pixel, registers.rect_lo, registers.rect_hi))
        return undrawn(pixel);

    // Handles crop where we start scanning out at an offset.
    ivec2 coord = pixel + registers.frag_coord_offset;

    int info_index = coord.y >> registers.info_y_shift;
    ivec4 horiz_info0 = texelFetch(uHorizontalInfo, 2 * info_index + 0);
    ivec4 horiz_info1 = texelFetch(uHorizontalInfo, 2 * info_index + 1);

    int h_start = horiz_info0.x;
    int h_start_clamp = horiz_info0.y;
    int h_end_clamp = horiz_info0.z;
    int x_start = horiz_info0.w;
    int x_add = horiz_info1.x;
    int y_start = horiz_info1.y;
    int y_add = horiz_info1.z;
    int y_base = horiz_info1.w;

    // Rebase Y relative to YStart.
    coord.y -= registers.v_start;

    // Scissor against HStart/End, also handles serrate where we skip every other line.
    if (coord.x < h_start_clamp || coord.x >= h_end_clamp ||
            ((coord.y & registers.serrate_mask) != registers.serrate_select))
        return undrawn(pixel);

    // Shift the X coord to be relative to sampling, this can change per scanline.
    coord.x -= h_start;

    // Rebase Y in terms of progressive scan.
    coord.y >>= registers.serrate_shift;

    if (GAMMA_DITHER)
        reseed_noise(coord.x, coord.y, registers.frame_count);

    int x = coord.x * x_add + x_start;
    int y = (coord.y - y_base) * y_add + y_start;
    ivec2 base_coord = ivec2(x, y) >> 10;
    uvec3 c00 = texelFetch(uDivotOutput, ivec3(base_coord, 0), 0).rgb;

    int bug_offset = 0;
    if (FETCH_BUG)
    {
        int prev_y = (y - registers.y_add) >> 10;
        int next_y = (y + registers.y_add) >> 10;
        if (coord.y != 0 && base_coord.y == prev_y && base_coord.y != next_y)
            bug_offset = 1;
    }

    if (SCALE_AA)
    {
        int x_frac = (x >> 5) & 31;
        int y_frac = (y >> 5) & 31;

        uvec3 c10 = texelFetchOffset(uDivotOutput, ivec3(base_coord, 0), 0, ivec2(1, 0)).rgb;
        uvec3 c01 = texelFetchOffset(uDivotOutput, ivec3(base_coord, bug_offset), 0, ivec2(0, 1)).rgb;
        uvec3 c11 = texelFetchOffset(uDivotOutput, ivec3(base_coord, bug_offset), 0, ivec2(1)).rgb;

        c00 = vi_lerp(c00, c01, y_frac);
        c10 = vi_lerp(c10, c11, y_frac);
        c00 = vi_lerp(c00, c10, x_frac);
    }

    if (GAMMA_ENABLE)
        c00 = integer_gamma(c00);
    else if (GAMMA_DITHER)
        c00 = min(c00 + noise_get_partial_gamma_dither(), uvec3(0xff));

    return uvec4(c00, 255u);
}

/* the mean of two stored 8-bit values, rounded half up, as a linear
 * sample halfway between them gives it */
uvec4 mean2(uvec4 a, uvec4 b)
{
    return (a + b + 1u) >> 1u;
}

/* a 2:1 linear blit's pixel: the means of each row's pair, then their
 * mean (columns before rows, as the filter does) */
#define BOX(level, p) mean2(mean2(level(2 * (p)), level(2 * (p) + ivec2(1, 0))), \
                            mean2(level(2 * (p) + ivec2(0, 1)), level(2 * (p) + ivec2(1, 1))))

uvec4 down1(ivec2 p) { return BOX(scale_pixel, p); }
uvec4 down2(ivec2 p) { return BOX(down1, p); }
uvec4 down3(ivec2 p) { return BOX(down2, p); }

/* the downscaled image's pixel */
uvec4 downscaled(ivec2 p)
{
    if (registers.downscale_steps == 0)
        return scale_pixel(p);
    else if (registers.downscale_steps == 1)
        return down1(p);
    else if (registers.downscale_steps == 2)
        return down2(p);
    else
        return down3(p);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    uvec4 c;

    if (registers.deinterlace_mul != 0)
    {
        /* in quarter rows: a copy of a row, or the mean of two, clamped
         * to the image as a linear clamped sample is */
        int q = registers.deinterlace_mul * p.y + registers.deinterlace_add;
        int last = registers.deinterlace_rows - 1;
        int r0 = clamp(q >> 2, 0, last);
        c = downscaled(ivec2(p.x, r0));
        if ((q & 3) != 0)
        {
            int r1 = clamp((q >> 2) + 1, 0, last);
            c = mean2(c, downscaled(ivec2(p.x, r1)));
        }
    }
    else
        c = downscaled(p);

    FragColor = vec4(c) / 255.0;
}
