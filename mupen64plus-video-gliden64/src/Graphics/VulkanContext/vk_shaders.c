#include <stddef.h>

#include "../CombinerValues.h"
#include "vk_shaders.h"

/* ---- the uniform block: CombinerValues as it lies in memory ---- */

#define VKR_VALUES(X) \
   X(fogColor, T_F4) X(centerColor, T_F4) X(scaleColor, T_F4) X(blendColor, T_F4) \
   X(envColor, T_F4) X(primColor, T_F4) X(rectColor, T_F4) \
   X(blendMux1, T_I4) X(blendMux2, T_I4) X(convertParams, T_I4) \
   X(fogScale, T_F2) X(noiseOffset, T_F2) X(screenScale, T_F2) X(depthScale, T_F2) \
   X(screenCoordsScale, T_F2) X(texScale, T_F2) X(texOffset0, T_F2) X(texOffset1, T_F2) \
   X(cacheShiftScale0, T_F2) X(cacheShiftScale1, T_F2) X(cacheScale0, T_F2) X(cacheScale1, T_F2) \
   X(cacheOffset0, T_F2) X(cacheOffset1, T_F2) \
   X(fbMonochrome, T_I2) X(fbFixedAlpha, T_I2) X(msTexEnabled, T_I2) X(textureFormat, T_I2) \
   X(fogUsage, T_I1) X(forceBlendCycle1, T_I1) X(forceBlendCycle2, T_I1) \
   X(alphaCompareMode, T_I1) X(alphaDitherMode, T_I1) X(colorDitherMode, T_I1) \
   X(maxTile, T_I1) X(enableLod, T_I1) X(textureDetail, T_I1) X(texturePersp, T_I1) \
   X(textureFilterMode, T_I1) X(textureConvert, T_I1) X(enableAlphaTest, T_I1) \
   X(alphaCvgSel, T_I1) X(cvgXAlpha, T_I1) X(enableDepth, T_I1) X(enableDepthCompare, T_I1) \
   X(enableDepthUpdate, T_I1) X(depthMode, T_I1) X(depthSource, T_I1) X(renderTarget, T_I1) \
   X(alphaTestValue, T_F1) X(minLod, T_F1) X(primDepth, T_F1) X(deltaZ, T_F1) \
   X(primLod, T_F1) X(k4, T_F1) X(k5, T_F1)

#define X(N, T) U_##N,
enum { VKR_VALUES(X) U_lightDirection, U_lightColor, U_COUNT };
#undef X

typedef struct value_member { int t; unsigned offset; } value_member;

#define X(N, T) { T, (unsigned)offsetof(CombinerValues, N) },
static const value_member value_members[] = {
   VKR_VALUES(X)
   { T_F4, (unsigned)offsetof(CombinerValues, lightDirection) },
   { T_F4, (unsigned)offsetof(CombinerValues, lightColor) }
};
#undef X

enum { PC_OPS = 0, PC_FLAGS, PC_CLIP, PC_SPECIAL, PC_JITTER };

enum { CYC_1 = 0, CYC_2 = 1, CYC_COPY = 2, CYC_FILL = 3 };

enum { OP_LOAD = 0, OP_SUB = 1, OP_MUL = 2, OP_ADD = 3, OP_INTER = 4 };

enum
{
   IN_COMBINED = 0, IN_TEXEL0, IN_TEXEL1, IN_PRIM, IN_SHADE, IN_ENV, IN_CENTER, IN_SCALE,
   IN_COMBINED_A, IN_TEXEL0_A, IN_TEXEL1_A, IN_PRIM_A, IN_SHADE_A, IN_ENV_A, IN_LOD,
   IN_PRIM_LOD, IN_NOISE, IN_K4, IN_K5, IN_ONE, IN_ZERO, IN_COUNT
};

typedef struct emit_t
{
   spv_module *m;
   const vkr_shader_config *cfg;
   const vkr_combiner_desc *desc;
   int generic;

   uint32_t ubo, pc;
   uint32_t tex[2], ms[2], noise, depth;
   uint32_t zimg, dzimg;
   uint32_t zimg_v, dzimg_v;

   spv_v frag_coord;
   spv_v shade, tc[2], lod_tc, num_lights;
   spv_var frag_color;
   spv_var frag_depth_out;

   /* what the combiner reads */
   spv_var rt0, rt1;
   spv_v   vec_color;
   spv_v   lod_frac;
   spv_v   snoise;

   /* generic: the inputs, by index */
   spv_var ci, ai;
   /* specific: the combined colour once it exists */
   spv_var combined;
} E;

static void declare_values(E *e)
{
   spv_module *m = e->m;
   uint32_t    members[U_COUNT];
   uint32_t    block;
   int         i;

   for (i = 0; i < U_COUNT; i++)
   {
      if (i == U_lightDirection || i == U_lightColor)
         members[i] = spv_array_type(m, spv_type(m, T_F4), 8, 16);
      else
         members[i] = spv_type(m, value_members[i].t);
   }
   block = spv_struct(m, members, U_COUNT);
   spv_decorate(m, block, SPV_DEC_BLOCK);
   for (i = 0; i < U_COUNT; i++)
      spv_member_decorate(m, block, (uint32_t)i, SPV_DEC_OFFSET, value_members[i].offset);
   e->ubo = spv_global(m, block, SPV_UNIFORM);
   spv_decorate1(m, e->ubo, SPV_DEC_SET, 0);
   spv_decorate1(m, e->ubo, SPV_DEC_BINDING, VKR_BIND_VALUES);
}

static void declare_push(E *e)
{
   spv_module *m = e->m;
   uint32_t    members[5];
   uint32_t    block;

   members[PC_OPS]     = spv_array_type(m, spv_type(m, T_I1), 16, 4);
   members[PC_FLAGS]   = spv_type(m, T_I1);
   members[PC_CLIP]    = spv_type(m, T_I1);
   members[PC_SPECIAL] = spv_type(m, T_F4);
   members[PC_JITTER]  = spv_type(m, T_F2);
   block = spv_struct(m, members, 5);
   spv_decorate(m, block, SPV_DEC_BLOCK);
   spv_member_decorate(m, block, PC_OPS, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, ops));
   spv_member_decorate(m, block, PC_FLAGS, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, flags));
   spv_member_decorate(m, block, PC_CLIP, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, clip_enable));
   spv_member_decorate(m, block, PC_SPECIAL, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, special));
   spv_member_decorate(m, block, PC_JITTER, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, jitter));
   e->pc = spv_global(m, block, SPV_PUSH_CONSTANT);
}

static uint32_t declare_sampler(E *e, int is_uint, int ms, uint32_t binding)
{
   spv_module *m  = e->m;
   uint32_t    ty = spv_sampled_image(m, is_uint, ms);
   uint32_t    id = spv_global(m, ty, SPV_UNIFORM_CONSTANT);
   spv_decorate1(m, id, SPV_DEC_SET, 0);
   spv_decorate1(m, id, SPV_DEC_BINDING, binding);
   return id;
}

static uint32_t load_tex(E *e, uint32_t var, int is_uint, int ms)
{
   return spv_load_sampler(e->m, var, spv_sampled_image(e->m, is_uint, ms));
}

static spv_v u(E *e, int member)
{
   return spv_load_member(e->m, e->ubo, SPV_UNIFORM, (uint32_t)member, value_members[member].t);
}

static spv_v u_x(E *e, int member, uint32_t comp)
{
   return spv_x(e->m, u(e, member), comp);
}

static spv_c ne0(E *e, spv_v v)
{
   return spv_cond(spv_ne(e->m, v, spv_i(e->m, 0)));
}

static spv_c eq_i(E *e, spv_v v, int n)
{
   return spv_cond(spv_eq(e->m, v, spv_i(e->m, n)));
}

static spv_v pc_flags(E *e)
{
   return spv_load_member(e->m, e->pc, SPV_PUSH_CONSTANT, PC_FLAGS, T_I1);
}

static spv_c fl(E *e, int bit)
{
   if (!e->generic)
      return spv_known((e->desc->flags & bit) != 0);
   return ne0(e, spv_band(e->m, pc_flags(e), spv_i(e->m, bit)));
}

static spv_v cycle_v(E *e)
{
   return spv_band(e->m, spv_shr(e->m, pc_flags(e), spv_i(e->m, VKR_CF_CYCLE_SHIFT)), spv_i(e->m, 3));
}

static int cycle_known(E *e)
{
   return (e->desc->flags >> VKR_CF_CYCLE_SHIFT) & 3;
}

static spv_c cycle_is(E *e, int c)
{
   if (!e->generic)
      return spv_known(cycle_known(e) == c);
   return spv_cond(spv_eq(e->m, cycle_v(e), spv_i(e->m, c)));
}

static spv_c cycle_lt(E *e, int c)
{
   if (!e->generic)
      return spv_known(cycle_known(e) < c);
   return spv_cond(spv_lt(e->m, cycle_v(e), spv_i(e->m, c)));
}

/* ---- textures ---- */

static spv_v tex_at(E *e, uint32_t s, spv_v tc, spv_v size, spv_v off)
{
   spv_module *m = e->m;
   return spv_sample(m, s, spv_sub(m, tc, spv_div(m, off, size)));
}

static spv_v fetch_frag(E *e, uint32_t s)
{
   spv_module *m = e->m;
   return spv_fetch(m, s, spv_to_int(m, spv_xy(m, e->frag_coord)));
}

/* one of the bilinear modes, into @out */
static void tex_filter(E *e, uint32_t s, spv_v tc, spv_var out)
{
   spv_module *m = e->m;
   spv_v size   = spv_to_float(m, spv_texture_size(m, s, spv_i(m, 0)));
   spv_v off;
   spv_v ox, oy, sx, sy, abs_x, abs_y;
   spv_v c0, c1, c2;
   spv_if_t b;
   int mode = e->cfg->filter_mode;

   off = spv_fract(m, spv_sub(m, spv_mul(m, tc, size), spv_vec(m, T_F2, 0.5f, 0.5f, 0, 0)));
   off = spv_sub(m, off, spv_step(m, spv_f(m, 1.0f), spv_add(m, spv_x(m, off, 0), spv_x(m, off, 1))));
   ox = spv_x(m, off, 0);
   oy = spv_x(m, off, 1);
   sx = spv_sub(m, ox, spv_sign(m, ox));
   sy = spv_sub(m, oy, spv_sign(m, oy));
   abs_x = spv_abs(m, ox);
   abs_y = spv_abs(m, oy);

   c0 = tex_at(e, s, tc, size, off);
   c1 = tex_at(e, s, tc, size, spv_vec2(m, sx, oy));
   c2 = tex_at(e, s, tc, size, spv_vec2(m, ox, sy));

   if (mode == 0 || mode == 2)
   {
      if (mode == 2 && spv_if(m, &b, eq_i(e, u(e, U_enableAlphaTest), 1)))
      {
         spv_v pc0 = spv_vec3_1(m, spv_mul(m, spv_xyz(m, c0), spv_x(m, c0, 3)), spv_x(m, c0, 3));
         spv_v pc1 = spv_vec3_1(m, spv_mul(m, spv_xyz(m, c1), spv_x(m, c1, 3)), spv_x(m, c1, 3));
         spv_v pc2 = spv_vec3_1(m, spv_mul(m, spv_xyz(m, c2), spv_x(m, c2, 3)), spv_x(m, c2, 3));
         spv_v n   = spv_add(m, spv_add(m, pc0, spv_mul(m, spv_sub(m, pc1, pc0), abs_x)),
               spv_mul(m, spv_sub(m, pc2, pc0), abs_y));
         spv_store(m, out, spv_vec3_1(m, spv_div(m, spv_xyz(m, n), spv_x(m, n, 3)), spv_x(m, n, 3)));
      }
      if (mode != 2 || spv_else(m, &b))
         spv_store(m, out, spv_add(m, spv_add(m, c0, spv_mul(m, spv_sub(m, c1, c0), abs_x)),
                  spv_mul(m, spv_sub(m, c2, c0), abs_y)));
      if (mode == 2)
         spv_endif(m, &b);
      return;
   }

   {
      spv_v p1q1 = tex_at(e, s, tc, size, spv_vec2(m, sx, sy));
      spv_var a  = spv_local(m, T_F4);
      spv_var b2 = spv_local(m, T_F4);
      spv_var c  = spv_local(m, T_F4);
      spv_var d  = spv_local(m, T_F4);
      spv_v fx   = abs_x;
      spv_v fy   = abs_y;
      spv_store(m, a, c0);
      spv_store(m, b2, c1);
      spv_store(m, c, c2);
      spv_store(m, d, p1q1);

      if (mode == 3)
      {
         spv_if_t t1, t2;
         if (spv_if(m, &t1, eq_i(e, u(e, U_enableAlphaTest), 1)))
         {
            static const unsigned idx[4] = { 0, 1, 2, 3 };
            spv_var vars[4];
            unsigned i;
            vars[0] = a; vars[1] = b2; vars[2] = c; vars[3] = d;
            for (i = 0; i < 4; i++)
            {
               spv_v p = spv_load(m, vars[idx[i]]);
               spv_store(m, vars[idx[i]], spv_vec3_1(m, spv_mul(m, spv_xyz(m, p), spv_x(m, p, 3)), spv_x(m, p, 3)));
            }
            {
               spv_v q0 = spv_mix(m, spv_load(m, a), spv_load(m, b2), fx);
               spv_v q1 = spv_mix(m, spv_load(m, c), spv_load(m, d), fx);
               spv_v n  = spv_mix(m, q0, q1, fy);
               spv_store(m, out, spv_vec3_1(m, spv_div(m, spv_xyz(m, n), spv_x(m, n, 3)), spv_x(m, n, 3)));
            }
         }
         if (spv_else(m, &t1))
         {
            if (spv_if(m, &t2, eq_i(e, u(e, U_cvgXAlpha), 1)))
            {
               /* bleed the colour of the more opaque neighbour */
               static const unsigned pairs[8][2] = {
                  { 0, 1 }, { 1, 0 }, { 2, 3 }, { 3, 2 }, { 0, 2 }, { 2, 0 }, { 1, 3 }, { 3, 1 }
               };
               spv_var vars[4];
               unsigned i;
               vars[0] = a; vars[1] = b2; vars[2] = c; vars[3] = d;
               for (i = 0; i < 8; i++)
               {
                  spv_if_t t3;
                  spv_v pa = spv_load(m, vars[pairs[i][0]]);
                  spv_v pb = spv_load(m, vars[pairs[i][1]]);
                  if (spv_if(m, &t3, spv_cond(spv_gt(m, spv_x(m, pa, 3), spv_x(m, pb, 3)))))
                     spv_store(m, vars[pairs[i][1]], spv_vec3_1(m, spv_xyz(m, pa), spv_x(m, pb, 3)));
                  spv_endif(m, &t3);
               }
            }
            spv_endif(m, &t2);
            {
               spv_v q0 = spv_mix(m, spv_load(m, a), spv_load(m, b2), fx);
               spv_v q1 = spv_mix(m, spv_load(m, c), spv_load(m, d), fx);
               spv_store(m, out, spv_mix(m, q0, q1, fy));
            }
         }
         spv_endif(m, &t1);
         return;
      }

      {
         spv_v q0 = spv_mix(m, c0, c1, fx);
         spv_v q1 = spv_mix(m, c2, p1q1, fx);
         spv_store(m, out, spv_mix(m, q0, q1, fy));
      }
   }
}

static spv_v luma(E *e, spv_v c)
{
   spv_module *m = e->m;
   return spv_dot(m, spv_vec(m, T_F3, 0.2126f, 0.7152f, 0.0722f, 0), spv_xyz(m, c));
}

/* the monochrome and fixed-alpha treatment of framebuffer textures */
static void fb_modes(E *e, spv_var out, spv_v mono, spv_v fixed_alpha)
{
   spv_module *m = e->m;
   spv_if_t b;

   if (spv_if(m, &b, eq_i(e, mono, 1)))
   {
      spv_v c = spv_load(m, out);
      spv_store(m, out, spv_spread(m, spv_x(m, c, 0), 4));
   }
   spv_endif(m, &b);
   if (spv_if(m, &b, eq_i(e, mono, 2)))
   {
      spv_v c = spv_load(m, out);
      spv_v l = luma(e, c);
      spv_store(m, out, spv_vec4(m, l, l, l, spv_x(m, c, 3)));
   }
   spv_endif(m, &b);
   if (spv_if(m, &b, eq_i(e, mono, 3)))
   {
      spv_v l = luma(e, spv_load(m, out));
      spv_store(m, out, spv_vec4(m, l, l, l, spv_f(m, 0.0f)));
   }
   spv_endif(m, &b);
   if (spv_if(m, &b, eq_i(e, fixed_alpha, 1)))
   {
      spv_v c = spv_load(m, out);
      spv_store(m, out, spv_vec3_1(m, spv_xyz(m, c), spv_f(m, 0.825f)));
   }
   spv_endif(m, &b);
}

static void read_tex(E *e, int tile, spv_var out, int filtering, int copy_mode)
{
   spv_module *m = e->m;
   uint32_t s = load_tex(e, e->tex[tile], 0, 0);
   spv_v tc   = e->tc[tile];
   spv_v mono = u_x(e, U_fbMonochrome, (uint32_t)tile);
   spv_v fix  = u_x(e, U_fbFixedAlpha, (uint32_t)tile);
   spv_if_t b;

   if (spv_if(m, &b, eq_i(e, mono, 3)))
      spv_store(m, out, fetch_frag(e, s));
   if (spv_else(m, &b))
   {
      if (copy_mode)
         spv_store(m, out, spv_sample(m, s, tc));
      else
      {
         spv_if_t f;
         if (spv_if(m, &f, eq_i(e, u(e, U_textureFilterMode), 0)))
            spv_store(m, out, spv_sample(m, s, tc));
         if (spv_else(m, &f))
         {
            if (filtering)
               tex_filter(e, s, tc, out);
         }
         spv_endif(m, &f);
      }
   }
   spv_endif(m, &b);
   fb_modes(e, out, mono, fix);
}

static void read_tex_ms(E *e, int tile, spv_var out)
{
   spv_module *m = e->m;
   uint32_t s = load_tex(e, e->ms[tile], 0, 1);
   spv_v mono = u_x(e, U_fbMonochrome, (uint32_t)tile);
   spv_v fix  = u_x(e, U_fbFixedAlpha, (uint32_t)tile);
   spv_var coord = spv_local(m, T_I2);
   spv_v sum;
   spv_if_t b;
   int i;

   if (spv_if(m, &b, eq_i(e, mono, 3)))
      spv_store(m, coord, spv_to_int(m, spv_xy(m, e->frag_coord)));
   if (spv_else(m, &b))
      spv_store(m, coord, spv_to_int(m, spv_mul(m, spv_to_float(m, spv_texture_size_ms(m, s)), e->tc[tile])));
   spv_endif(m, &b);

   sum = spv_zero(m, T_F4);
   for (i = 0; i < e->cfg->msaa; i++)
      sum = spv_add(m, sum, spv_fetch_ms(m, s, spv_load(m, coord), i));
   spv_store(m, out, spv_div(m, sum, spv_f(m, (float)e->cfg->msaa)));
   fb_modes(e, out, mono, fix);
}

static void read_filtered(E *e, int tile, spv_var out, int copy_mode)
{
   spv_module *m = e->m;
   if (e->cfg->msaa > 0)
   {
      spv_if_t b;
      if (spv_if(m, &b, eq_i(e, u_x(e, U_msTexEnabled, (uint32_t)tile), 0)))
         read_tex(e, tile, out, 1, copy_mode);
      if (spv_else(m, &b))
         read_tex_ms(e, tile, out);
      spv_endif(m, &b);
   }
   else
      read_tex(e, tile, out, 1, copy_mode);
}

static void yuv_convert(E *e, spv_var out, spv_v format)
{
   spv_module *m = e->m;
   spv_var icolor = spv_local(m, T_I4);
   spv_v params   = u(e, U_convertParams);
   spv_v c, r, g, bl;
   spv_v p0, p1, p2, p3, half, shift;
   spv_if_t b;

   spv_store(m, icolor, spv_to_int(m, spv_mul(m, spv_load(m, out), spv_f(m, 255.0f))));
   if (spv_if(m, &b, eq_i(e, format, 1)))
   {
      spv_v i = spv_load(m, icolor);
      spv_store(m, icolor, spv_vec4(m, spv_sub(m, spv_x(m, i, 0), spv_i(m, 128)),
               spv_sub(m, spv_x(m, i, 1), spv_i(m, 128)), spv_x(m, i, 2), spv_x(m, i, 3)));
   }
   spv_endif(m, &b);

   c = spv_load(m, icolor);
   r = spv_x(m, c, 0);
   g = spv_x(m, c, 1);
   bl = spv_x(m, c, 2);
   p0 = spv_x(m, params, 0);
   p1 = spv_x(m, params, 1);
   p2 = spv_x(m, params, 2);
   p3 = spv_x(m, params, 3);
   half = spv_i(m, 128);
   shift = spv_i(m, 256);
   {
      spv_v ir = spv_add(m, bl, spv_div(m, spv_add(m, spv_mul(m, p0, g), half), shift));
      spv_v ig = spv_add(m, bl, spv_div(m, spv_add(m, spv_add(m, spv_mul(m, p1, r), spv_mul(m, p2, g)), half), shift));
      spv_v ib = spv_add(m, bl, spv_div(m, spv_add(m, spv_mul(m, p3, r), half), shift));
      spv_v cv = spv_vec4(m, ir, ig, ib, bl);
      spv_store(m, out, spv_div(m, spv_to_float(m, cv), spv_f(m, 255.0f)));
   }
}

static void read_tile0(E *e)
{
   spv_module *m = e->m;
   spv_if_t b;
   if (spv_if(m, &b, spv_not(m, fl(e, VKR_CF_BILERP0))))
   {
      spv_store(m, e->rt0, spv_sample(m, load_tex(e, e->tex[0], 0, 0), e->tc[0]));
      yuv_convert(e, e->rt0, u_x(e, U_textureFormat, 0));
   }
   if (spv_else(m, &b))
      read_filtered(e, 0, e->rt0, 0);
   spv_endif(m, &b);
}

static void read_tile1(E *e)
{
   spv_module *m = e->m;
   spv_if_t b;
   if (spv_if(m, &b, spv_not(m, fl(e, VKR_CF_BILERP1))))
   {
      spv_if_t c;
      if (spv_if(m, &c, ne0(e, u(e, U_textureConvert))))
         spv_store(m, e->rt1, spv_load(m, e->rt0));
      if (spv_else(m, &c))
         spv_store(m, e->rt1, spv_sample(m, load_tex(e, e->tex[1], 0, 0), e->tc[1]));
      spv_endif(m, &c);
      yuv_convert(e, e->rt1, u_x(e, U_textureFormat, 1));
   }
   if (spv_else(m, &b))
      read_filtered(e, 1, e->rt1, 0);
   spv_endif(m, &b);
}

/* 3-point at a chosen level, or the plain lookup */
static spv_v lod_read(E *e, uint32_t s, spv_v tc, spv_v lod, int explicit_lod)
{
   spv_module *m = e->m;
   if (!e->cfg->three_point)
      return explicit_lod ? spv_sample_lod(m, s, tc, lod) : spv_sample(m, s, tc);
   {
      spv_v size = spv_to_float(m, spv_texture_size(m, s, spv_to_int(m, lod)));
      spv_v off  = spv_fract(m, spv_sub(m, spv_mul(m, tc, size), spv_vec(m, T_F2, 0.5f, 0.5f, 0, 0)));
      spv_v ox, oy;
      spv_v at[3];
      int i;
      off = spv_sub(m, off, spv_step(m, spv_f(m, 1.0f), spv_add(m, spv_x(m, off, 0), spv_x(m, off, 1))));
      ox = spv_x(m, off, 0);
      oy = spv_x(m, off, 1);
      at[0] = off;
      at[1] = spv_vec2(m, spv_sub(m, ox, spv_sign(m, ox)), oy);
      at[2] = spv_vec2(m, ox, spv_sub(m, oy, spv_sign(m, oy)));
      for (i = 0; i < 3; i++)
      {
         spv_v c = spv_sub(m, tc, spv_div(m, at[i], size));
         at[i] = explicit_lod ? spv_sample_lod(m, s, c, lod) : spv_sample(m, s, c);
      }
      return spv_add(m, spv_add(m, at[0], spv_mul(m, spv_sub(m, at[1], at[0]), spv_abs(m, ox))),
            spv_mul(m, spv_sub(m, at[2], at[0]), spv_abs(m, oy)));
   }
}

/* Writes readtex0, readtex1 and returns the LOD fraction. */
static spv_v mipmap(E *e)
{
   spv_module *m = e->m;
   uint32_t s0 = load_tex(e, e->tex[0], 0, 0);
   uint32_t s1 = load_tex(e, e->tex[1], 0, 0);
   spv_var ret = spv_local(m, T_F1);
   spv_if_t b, b2;

   if (!e->cfg->enable_lod)
   {
      spv_store(m, e->rt0, spv_sample(m, s0, e->tc[0]));
      spv_store(m, e->rt1, spv_sample(m, s1, e->tc[1]));
      spv_store(m, ret, u(e, U_minLod));
      if (spv_if(m, &b, eq_i(e, u(e, U_maxTile), 0)))
         spv_store(m, ret, spv_f(m, 1.0f));
      spv_endif(m, &b);
      return spv_load(m, ret);
   }

   {
      spv_v zero   = spv_f(m, 0.0f);
      spv_v one    = spv_f(m, 1.0f);
      spv_v rt_n   = lod_read(e, s0, e->tc[0], zero, 0);
      spv_v rt_m   = lod_read(e, s1, e->tc[1], zero, 1);
      spv_v fmax   = spv_to_float(m, u(e, U_maxTile));
      spv_v dx     = spv_mul(m, spv_abs(m, spv_dfdx(m, e->lod_tc)), u(e, U_screenScale));
      spv_v lod    = spv_max(m, spv_x(m, dx, 0), spv_x(m, dx, 1));
      spv_v magv   = spv_lt(m, lod, one);
      spv_c magnify = spv_cond(magv);
      spv_v tile0  = spv_select(m, magv, zero, spv_floor(m, spv_log2(m, spv_floor(m, lod))));
      spv_v detail = u(e, U_textureDetail);
      spv_c distant = spv_or(m, spv_cond(spv_gt(m, lod, spv_f(m, 128.0f))), spv_cond(spv_ge(m, tile0, fmax)));
      spv_var frac = spv_local(m, T_F1);

      spv_store(m, e->rt0, rt_n);
      spv_store(m, e->rt1, rt_m);
      spv_store(m, frac, spv_fract(m, spv_div(m, lod, spv_pow(m, spv_f(m, 2.0f), tile0))));
      if (spv_if(m, &b, magnify))
         spv_store(m, frac, spv_max(m, spv_load(m, frac), u(e, U_minLod)));
      spv_endif(m, &b);
      if (spv_if(m, &b, eq_i(e, detail, 0)))
      {
         if (spv_if(m, &b2, distant))
            spv_store(m, frac, one);
         if (spv_else(m, &b2))
         {
            spv_if_t b3;
            if (spv_if(m, &b3, magnify))
               spv_store(m, frac, zero);
            spv_endif(m, &b3);
         }
         spv_endif(m, &b2);
      }
      spv_endif(m, &b);
      if (spv_if(m, &b, spv_and(m, magnify, ne0(e, spv_band(m, detail, spv_i(m, 1))))))
         spv_store(m, frac, spv_sub(m, one, spv_load(m, frac)));
      spv_endif(m, &b);

      if (spv_if(m, &b, eq_i(e, u(e, U_maxTile), 0)))
      {
         if (spv_if(m, &b2, ne0(e, u(e, U_enableLod))))
         {
            spv_if_t b3;
            if (spv_if(m, &b3, spv_not(m, ne0(e, spv_band(m, detail, spv_i(m, 2))))))
               spv_store(m, e->rt1, spv_load(m, e->rt0));
            if (spv_else(m, &b3))
            {
               spv_if_t b4;
               if (spv_if(m, &b4, spv_not(m, magnify)))
                  spv_store(m, e->rt0, spv_load(m, e->rt1));
               spv_endif(m, &b4);
            }
            spv_endif(m, &b3);
         }
         spv_endif(m, &b2);
      }
      if (spv_else(m, &b))
      {
         if (spv_if(m, &b2, ne0(e, u(e, U_enableLod))))
         {
            spv_v tile  = spv_min(m, tile0, spv_sub(m, fmax, one));
            spv_v tm1   = spv_max(m, zero, spv_sub(m, tile, one));
            spv_v tp1   = spv_min(m, spv_sub(m, fmax, one), spv_add(m, tile, one));
            spv_v lodt  = lod_read(e, s1, e->tc[1], tile, 1);
            spv_v lodm1 = lod_read(e, s1, e->tc[1], tm1, 1);
            spv_v lodp1 = lod_read(e, s1, e->tc[1], tp1, 1);
            spv_c det2  = ne0(e, spv_band(m, detail, spv_i(m, 2)));
            spv_if_t b3, b4;

            if (spv_if(m, &b3, spv_cond(spv_lt(m, tile, one))))
            {
               if (spv_if(m, &b4, magnify))
               {
                  spv_if_t b5;
                  if (spv_if(m, &b5, eq_i(e, detail, 0)))
                     spv_store(m, e->rt1, spv_load(m, e->rt0));
                  spv_endif(m, &b5);
               }
               if (spv_else(m, &b4))
               {
                  spv_if_t b5;
                  if (spv_if(m, &b5, det2))
                  {
                     spv_store(m, e->rt0, lodt);
                     spv_store(m, e->rt1, lodp1);
                  }
                  spv_endif(m, &b5);
               }
               spv_endif(m, &b4);
            }
            if (spv_else(m, &b3))
            {
               if (spv_if(m, &b4, det2))
               {
                  spv_store(m, e->rt0, lodt);
                  spv_store(m, e->rt1, lodp1);
               }
               if (spv_else(m, &b4))
               {
                  spv_store(m, e->rt0, lodm1);
                  spv_store(m, e->rt1, lodt);
               }
               spv_endif(m, &b4);
            }
            spv_endif(m, &b3);
         }
         spv_endif(m, &b2);
      }
      spv_endif(m, &b);
      return spv_load(m, frac);
   }
}

static spv_v calc_light(E *e, spv_v shade_rgb)
{
   spv_module *m = e->m;
   spv_var out   = spv_local(m, T_F3);
   spv_v n       = spv_to_int(m, spv_floor(m, spv_add(m, e->num_lights, spv_f(m, 0.5f))));
   spv_if_t b;
   int i;

   spv_store(m, out, shade_rgb);
   if (spv_if(m, &b, ne0(e, n)))
   {
      spv_v c = spv_xyz(m, spv_load_member_at(m, e->ubo, SPV_UNIFORM, U_lightColor, n, T_F4));
      spv_store(m, out, c);
      for (i = 0; i < 8; i++)
      {
         spv_if_t l;
         if (spv_if(m, &l, spv_cond(spv_lt(m, spv_i(m, i), n))))
         {
            spv_v idx = spv_i(m, i);
            spv_v dir = spv_xyz(m, spv_load_member_at(m, e->ubo, SPV_UNIFORM, U_lightDirection, idx, T_F4));
            spv_v col = spv_xyz(m, spv_load_member_at(m, e->ubo, SPV_UNIFORM, U_lightColor, idx, T_F4));
            spv_v k   = spv_max(m, spv_dot(m, shade_rgb, dir), spv_f(m, 0.0f));
            spv_store(m, out, spv_add(m, spv_load(m, out), spv_mul(m, k, col)));
         }
         spv_endif(m, &l);
      }
      spv_store(m, out, spv_clamp(m, spv_load(m, out), 0.f, 1.f));
   }
   spv_endif(m, &b);
   return spv_load(m, out);
}

/* ---- the combiner proper ---- */

static spv_v input_v(E *e, int p, int alpha)
{
   spv_module *m = e->m;
   switch (p)
   {
      case IN_COMBINED:
      case IN_COMBINED_A:
      {
         spv_v c = spv_load(m, e->combined);
         if (alpha)
            return spv_x(m, c, 3);
         return p == IN_COMBINED ? spv_xyz(m, c) : spv_spread(m, spv_x(m, c, 3), 3);
      }
      case IN_TEXEL0:
      case IN_TEXEL0_A:
      case IN_TEXEL1:
      case IN_TEXEL1_A:
      {
         spv_v c = spv_load(m, (p == IN_TEXEL0 || p == IN_TEXEL0_A) ? e->rt0 : e->rt1);
         if (alpha)
            return spv_x(m, c, 3);
         return (p == IN_TEXEL0 || p == IN_TEXEL1) ? spv_xyz(m, c) : spv_spread(m, spv_x(m, c, 3), 3);
      }
      case IN_PRIM:
      case IN_PRIM_A:
         if (alpha)
            return u_x(e, U_primColor, 3);
         return p == IN_PRIM ? spv_xyz(m, u(e, U_primColor)) : spv_spread(m, u_x(e, U_primColor, 3), 3);
      case IN_SHADE:
      case IN_SHADE_A:
         if (alpha)
            return spv_x(m, e->vec_color, 3);
         return p == IN_SHADE ? spv_xyz(m, e->vec_color) : spv_spread(m, spv_x(m, e->vec_color, 3), 3);
      case IN_ENV:
      case IN_ENV_A:
         if (alpha)
            return u_x(e, U_envColor, 3);
         return p == IN_ENV ? spv_xyz(m, u(e, U_envColor)) : spv_spread(m, u_x(e, U_envColor, 3), 3);
      case IN_CENTER:
         return alpha ? u_x(e, U_centerColor, 3) : spv_xyz(m, u(e, U_centerColor));
      case IN_SCALE:
         return alpha ? u_x(e, U_scaleColor, 3) : spv_xyz(m, u(e, U_scaleColor));
      case IN_LOD:
         return alpha ? e->lod_frac : spv_spread(m, e->lod_frac, 3);
      case IN_PRIM_LOD:
         return alpha ? u(e, U_primLod) : spv_spread(m, u(e, U_primLod), 3);
      case IN_NOISE:
      {
         spv_v n = spv_add(m, spv_f(m, 0.5f), spv_mul(m, spv_f(m, 0.5f), e->snoise));
         return alpha ? n : spv_spread(m, n, 3);
      }
      case IN_K4:
         return alpha ? u(e, U_k4) : spv_spread(m, u(e, U_k4), 3);
      case IN_K5:
         return alpha ? u(e, U_k5) : spv_spread(m, u(e, U_k5), 3);
      case IN_ONE:
         return alpha ? spv_f(m, 1.f) : spv_vec(m, T_F3, 1.f, 1.f, 1.f, 0.f);
      default:
         break;
   }
   return alpha ? spv_f(m, 0.f) : spv_vec(m, T_F3, 0.f, 0.f, 0.f, 0.f);
}

static spv_v stage_known(E *e, int stage, int alpha)
{
   spv_module *m = e->m;
   const int *ops = &e->desc->ops[stage * 4];
   spv_v v = alpha ? spv_f(m, 0.f) : spv_vec(m, T_F3, 0.f, 0.f, 0.f, 0.f);
   int i;

   for (i = 0; i < 4; i++)
   {
      int o = ops[i];
      int p, k;
      spv_v a;
      if (o < 0)
         break;
      p = o & 255;
      k = o >> 8;
      if (k == OP_INTER)
         return spv_mix(m, input_v(e, ops[1], alpha), input_v(e, p, alpha), input_v(e, ops[2], alpha));
      a = input_v(e, p, alpha);
      switch (k)
      {
         case OP_LOAD: v = a; break;
         case OP_SUB:  v = spv_sub(m, v, a); break;
         case OP_MUL:  v = spv_mul(m, v, a); break;
         case OP_ADD:  v = spv_add(m, v, a); break;
         default: break;
      }
   }
   return v;
}

static spv_v stage_generic(E *e, int stage, int alpha)
{
   spv_module *m = e->m;
   spv_var in = alpha ? e->ai : e->ci;
   int     t  = alpha ? T_F1 : T_F3;
   spv_var v  = spv_local(m, t);
   spv_var go = spv_local(m, T_I1);
   int base   = stage * 4;
   int i;

   spv_store(m, v, spv_zero(m, t));
   spv_store(m, go, spv_i(m, 1));
   for (i = 0; i < 4; i++)
   {
      spv_if_t r;
      if (spv_if(m, &r, ne0(e, spv_load(m, go))))
      {
         spv_v o = spv_load_member_at(m, e->pc, SPV_PUSH_CONSTANT, PC_OPS, spv_i(m, base + i), T_I1);
         spv_if_t neg;
         if (spv_if(m, &neg, spv_cond(spv_lt(m, o, spv_i(m, 0)))))
            spv_store(m, go, spv_i(m, 0));
         if (spv_else(m, &neg))
         {
            spv_v p = spv_band(m, o, spv_i(m, 255));
            spv_v k = spv_shr(m, o, spv_i(m, 8));
            spv_v a = spv_load_at(m, in, p);
            spv_if_t c;
            if (spv_if(m, &c, eq_i(e, k, OP_LOAD)))
               spv_store(m, v, a);
            spv_endif(m, &c);
            if (spv_if(m, &c, eq_i(e, k, OP_SUB)))
               spv_store(m, v, spv_sub(m, spv_load(m, v), a));
            spv_endif(m, &c);
            if (spv_if(m, &c, eq_i(e, k, OP_MUL)))
               spv_store(m, v, spv_mul(m, spv_load(m, v), a));
            spv_endif(m, &c);
            if (spv_if(m, &c, eq_i(e, k, OP_ADD)))
               spv_store(m, v, spv_add(m, spv_load(m, v), a));
            spv_endif(m, &c);
            if (spv_if(m, &c, eq_i(e, k, OP_INTER)))
            {
               spv_v p2 = spv_load_member_at(m, e->pc, SPV_PUSH_CONSTANT, PC_OPS, spv_i(m, base + 1), T_I1);
               spv_v p3 = spv_load_member_at(m, e->pc, SPV_PUSH_CONSTANT, PC_OPS, spv_i(m, base + 2), T_I1);
               spv_store(m, v, spv_mix(m, spv_load_at(m, in, p2), a, spv_load_at(m, in, p3)));
               spv_store(m, go, spv_i(m, 0));
            }
            spv_endif(m, &c);
         }
         spv_endif(m, &neg);
      }
      spv_endif(m, &r);
   }
   return spv_load(m, v);
}

static spv_v run_stage(E *e, int stage, int alpha)
{
   return e->generic ? stage_generic(e, stage, alpha) : stage_known(e, stage, alpha);
}

static void store_inputs(E *e)
{
   spv_module *m = e->m;
   int i;
   for (i = 0; i < IN_COUNT; i++)
   {
      if (i == IN_COMBINED || i == IN_COMBINED_A || i == IN_ZERO)
         continue;
      spv_store_at(m, e->ci, (uint32_t)i, input_v(e, i, 0));
      spv_store_at(m, e->ai, (uint32_t)i, input_v(e, i, 1));
   }
   spv_store_at(m, e->ci, IN_COMBINED, spv_zero(m, T_F3));
   spv_store_at(m, e->ai, IN_COMBINED, spv_zero(m, T_F1));
   spv_store_at(m, e->ci, IN_COMBINED_A, spv_zero(m, T_F3));
   spv_store_at(m, e->ai, IN_COMBINED_A, spv_zero(m, T_F1));
   spv_store_at(m, e->ci, IN_ZERO, spv_zero(m, T_F3));
   spv_store_at(m, e->ai, IN_ZERO, spv_zero(m, T_F1));
}

static void sign_extend_c(E *e, spv_var v)
{
   spv_module *m = e->m;
   spv_v c = spv_load(m, v);
   int n   = v.t == T_F1 ? 1 : 3;
   spv_v one = spv_spread(m, spv_f(m, 1.f), n);
   /* c - 2.0*(1.0 - step(c, 1.0)) */
   spv_store(m, v, spv_sub(m, c, spv_mul(m, spv_sub(m, one, spv_step(m, c, one)), spv_f(m, 2.f))));
}

static void sign_extend_abd(E *e, spv_var v)
{
   spv_module *m = e->m;
   spv_v c = spv_load(m, v);
   int n   = v.t == T_F1 ? 1 : 3;
   spv_v lo = spv_spread(m, spv_f(m, -0.51f), n);
   spv_v hi = spv_spread(m, spv_f(m, 1.51f), n);
   spv_store(m, v, spv_sub(m, spv_add(m, c, spv_mul(m, spv_step(m, c, lo), spv_f(m, 2.f))),
            spv_mul(m, spv_step(m, hi, c), spv_f(m, 2.f))));
}

static void sign_extend(E *e, spv_var v, int bit_c, int bit_abd)
{
   spv_module *m = e->m;
   spv_if_t a, b;
   if (spv_if(m, &a, fl(e, bit_c)))
      sign_extend_c(e, v);
   if (spv_else(m, &a))
   {
      if (spv_if(m, &b, fl(e, bit_abd)))
         sign_extend_abd(e, v);
      spv_endif(m, &b);
   }
   spv_endif(m, &a);
}

static void alpha_test(E *e, spv_v alpha1)
{
   spv_module *m = e->m;
   spv_if_t b;
   if (spv_if(m, &b, ne0(e, u(e, U_enableAlphaTest))))
   {
      spv_v test    = spv_select(m, spv_eq(m, u(e, U_alphaCompareMode), spv_i(m, 3)), e->snoise, u(e, U_alphaTestValue));
      spv_var value = spv_local(m, T_F1);
      spv_if_t k;
      if (spv_if(m, &k, spv_and(m, ne0(e, u(e, U_alphaCvgSel)), eq_i(e, u(e, U_cvgXAlpha), 0))))
         spv_store(m, value, spv_f(m, 0.125f));
      if (spv_else(m, &k))
         spv_store(m, value, spv_clamp(m, alpha1, 0.f, 1.f));
      spv_endif(m, &k);
      if (spv_if(m, &k, spv_cond(spv_lt(m, spv_load(m, value), test))))
         spv_kill(m);
      spv_endif(m, &k);
   }
   spv_endif(m, &b);
}

static void noise_dither(E *e, spv_var clamped)
{
   spv_module *m = e->m;
   spv_if_t b;

   if (!e->cfg->noise)
      return;
   if (spv_if(m, &b, eq_i(e, u(e, U_colorDitherMode), 2)))
   {
      spv_v c   = spv_load(m, clamped);
      spv_v rgb = spv_mul(m, spv_xyz(m, c), spv_f(m, 255.f));
      spv_v i   = spv_to_int(m, rgb);
      spv_v d   = spv_band(m, spv_to_int(m, spv_mul(m, rgb, e->snoise)), spv_spread(m, spv_i(m, 7), 3));
      spv_v o   = spv_div(m, spv_to_float(m, spv_bor(m, i, d)), spv_f(m, 255.f));
      spv_store(m, clamped, spv_vec3_1(m, o, spv_x(m, c, 3)));
   }
   spv_endif(m, &b);
   if (spv_if(m, &b, eq_i(e, u(e, U_alphaDitherMode), 2)))
   {
      spv_v c = spv_load(m, clamped);
      spv_v a = spv_mul(m, spv_x(m, c, 3), spv_f(m, 255.f));
      spv_v i = spv_to_int(m, a);
      spv_v d = spv_band(m, spv_to_int(m, spv_mul(m, a, e->snoise)), spv_i(m, 7));
      spv_v o = spv_div(m, spv_to_float(m, spv_bor(m, i, d)), spv_f(m, 255.f));
      spv_store(m, clamped, spv_vec3_1(m, spv_xyz(m, c), o));
   }
   spv_endif(m, &b);
}

static void blender(E *e, spv_var pm, spv_var a, spv_var bv, spv_var clamped, int mux_member, int force_member, int first)
{
   spv_module *m = e->m;
   spv_v mux = u(e, mux_member);
   spv_v m0  = spv_x(m, mux, 0);
   spv_if_t f;

   spv_store_at(m, pm, 0, spv_load(m, clamped));
   if (!first)
      spv_store_at(m, pm, 1, spv_zero(m, T_F4));
   if (spv_if(m, &f, ne0(e, u(e, force_member))))
   {
      spv_v muxa, muxb, blend, c;
      spv_store_at(m, a, 0, spv_x(m, spv_load(m, clamped), 3));
      muxa = spv_load_at(m, a, spv_x(m, mux, 1));
      spv_store_at(m, bv, 0, spv_sub(m, spv_f(m, 1.f), muxa));
      muxb = spv_load_at(m, bv, spv_x(m, mux, 3));
      blend = spv_add(m, spv_mul(m, spv_load_at(m, pm, m0), muxa),
            spv_mul(m, spv_load_at(m, pm, spv_x(m, mux, 2)), muxb));
      c = spv_load(m, clamped);
      spv_store(m, clamped, spv_vec3_1(m, spv_clamp(m, spv_xyz(m, blend), 0.f, 1.f), spv_x(m, c, 3)));
   }
   if (spv_else(m, &f))
   {
      spv_v c = spv_load(m, clamped);
      spv_store(m, clamped, spv_vec3_1(m, spv_xyz(m, spv_load_at(m, pm, m0)), spv_x(m, c, 3)));
   }
   spv_endif(m, &f);
}

/* ---- N64 depth compare, on storage images ---- */

static spv_v depth_compare(E *e, spv_v cur)
{
   spv_module *m = e->m;
   spv_var res   = spv_local(m, T_I1);
   spv_if_t b;

   spv_store(m, res, spv_i(m, 1));
   if (spv_if(m, &b, ne0(e, u(e, U_enableDepth))))
   {
      spv_v coord = spv_to_int(m, spv_xy(m, e->frag_coord));
      uint32_t zi = spv_load_sampler(m, e->zimg_v, e->zimg);
      uint32_t di = spv_load_sampler(m, e->dzimg_v, e->zimg);
      spv_v buf   = spv_x(m, spv_image_load(m, zi, coord), 0);
      spv_var dz  = spv_local(m, T_F1);
      spv_var dzm = spv_local(m, T_F1);
      spv_if_t s, up;
      spv_var rb  = spv_local(m, T_I1);
      spv_v inf, far_, near_, max_, mode;

      if (spv_if(m, &s, eq_i(e, u(e, U_depthSource), 1)))
      {
         spv_store(m, dz, u(e, U_deltaZ));
         spv_store(m, dzm, u(e, U_deltaZ));
      }
      if (spv_else(m, &s))
      {
         spv_v d = spv_mul(m, spv_f(m, 4.f), spv_fwidth(m, cur));
         spv_store(m, dz, d);
         spv_store(m, dzm, spv_min(m, d, spv_x(m, spv_image_load(m, di, coord), 0)));
      }
      spv_endif(m, &s);

      inf  = spv_lt(m, cur, buf);
      far_ = spv_ge(m, spv_add(m, cur, spv_load(m, dzm)), buf);
      near_ = spv_le(m, spv_sub(m, cur, spv_load(m, dzm)), buf);
      max_ = spv_eq(m, buf, spv_f(m, 1.f));
      mode = u(e, U_depthMode);

      spv_store(m, rb, spv_i(m, 0));
      if (spv_if(m, &s, eq_i(e, mode, 1)))
         spv_store(m, rb, spv_select(m, max_, spv_i(m, 1),
                  spv_select(m, near_, spv_i(m, 1), spv_i(m, 0))));
      spv_endif(m, &s);
      if (spv_if(m, &s, spv_or(m, eq_i(e, mode, 0), eq_i(e, mode, 2))))
         spv_store(m, rb, spv_select(m, max_, spv_i(m, 1), spv_select(m, inf, spv_i(m, 1), spv_i(m, 0))));
      spv_endif(m, &s);
      if (spv_if(m, &s, eq_i(e, mode, 3)))
      {
         spv_c both = spv_and(m, spv_and(m, spv_cond(far_), spv_cond(near_)), spv_not(m, spv_cond(max_)));
         spv_if_t t;
         if (spv_if(m, &t, both))
            spv_store(m, rb, spv_i(m, 1));
         spv_endif(m, &t);
      }
      spv_endif(m, &s);
      if (spv_if(m, &s, eq_i(e, u(e, U_enableDepthCompare), 0)))
         spv_store(m, rb, spv_i(m, 1));
      spv_endif(m, &s);

      if (spv_if(m, &up, spv_and(m, ne0(e, u(e, U_enableDepthUpdate)), ne0(e, spv_load(m, rb)))))
      {
         spv_image_store(m, zi, coord, spv_vec4(m, cur, spv_f(m, 1.f), spv_f(m, 1.f), spv_f(m, 1.f)));
         spv_image_store(m, di, coord, spv_vec4(m, spv_load(m, dz), spv_f(m, 1.f), spv_f(m, 1.f), spv_f(m, 1.f)));
      }
      spv_endif(m, &up);
      spv_store(m, res, spv_load(m, rb));
   }
   spv_endif(m, &b);
   return spv_load(m, res);
}

static spv_v depth_render(E *e, spv_v z, spv_v cur)
{
   spv_module *m = e->m;
   spv_v coord   = spv_to_int(m, spv_xy(m, e->frag_coord));
   uint32_t zi   = spv_load_sampler(m, e->zimg_v, e->zimg);
   uint32_t di   = spv_load_sampler(m, e->dzimg_v, e->zimg);
   spv_var ok    = spv_local(m, T_I1);
   spv_if_t b, s;

   spv_store(m, ok, spv_i(m, 1));
   if (spv_if(m, &b, ne0(e, u(e, U_enableDepthCompare))))
   {
      if (spv_if(m, &s, spv_cond(spv_ge(m, cur, spv_x(m, spv_image_load(m, zi, coord), 0)))))
         spv_store(m, ok, spv_i(m, 0));
      spv_endif(m, &s);
   }
   spv_endif(m, &b);
   if (spv_if(m, &b, ne0(e, spv_load(m, ok))))
   {
      spv_image_store(m, zi, coord, spv_vec4(m, z, spv_f(m, 1.f), spv_f(m, 1.f), spv_f(m, 1.f)));
      spv_image_store(m, di, coord, spv_vec4(m, spv_f(m, 0.f), spv_f(m, 1.f), spv_f(m, 1.f), spv_f(m, 1.f)));
   }
   spv_endif(m, &b);
   return spv_load(m, ok);
}

static void declare_common(E *e, spv_module *m, const vkr_shader_config *cfg, int stage)
{
   e->m   = m;
   e->cfg = cfg;
   spv_begin(m, stage);
   declare_values(e);
   declare_push(e);
}

/* A storage image variable: the same OpTypeImage for both. */
static uint32_t declare_storage(E *e, uint32_t binding)
{
   spv_module *m = e->m;
   uint32_t id = spv_global(m, spv_storage_image(m), SPV_UNIFORM_CONSTANT);
   spv_decorate1(m, id, SPV_DEC_SET, 0);
   spv_decorate1(m, id, SPV_DEC_BINDING, binding);
   return id;
}

const uint32_t *vkr_write_combiner(spv_module *m, const vkr_shader_config *cfg,
      const vkr_combiner_desc *desc, int generic, unsigned *count)
{
   E e_;
   E *e = &e_;
   spv_var clamped, alpha1, color1, depth_v;
   spv_v   frag_depth_val;
   spv_if_t b;

   declare_common(e, m, cfg, SPV_STAGE_FRAGMENT);
   e->desc    = desc;
   e->generic = generic;

   e->tex[0] = declare_sampler(e, 0, 0, VKR_BIND_TEX0);
   e->tex[1] = declare_sampler(e, 0, 0, VKR_BIND_TEX1);
   e->noise  = declare_sampler(e, 0, 0, VKR_BIND_NOISE);
   e->depth  = declare_sampler(e, 0, 0, VKR_BIND_DEPTH);
   if (cfg->msaa > 0)
   {
      e->ms[0] = declare_sampler(e, 0, 1, VKR_BIND_MS0);
      e->ms[1] = declare_sampler(e, 0, 1, VKR_BIND_MS1);
   }
   if (cfg->n64_depth_compare)
   {
      e->zimg    = spv_storage_image(m);
      e->zimg_v  = declare_storage(e, VKR_BIND_DEPTH_Z);
      e->dzimg_v = declare_storage(e, VKR_BIND_DEPTH_DZ);
      if (cfg->interlock)
      {
         spv_capability(m, SPV_CAP_INTERLOCK);
         spv_extension(m, "SPV_EXT_fragment_shader_interlock");
         spv_mode(m, SPV_MODE_PIXEL_INTERLOCK);
      }
   }

   e->frag_coord = spv_builtin_in(m, T_F4, SPV_BUILTIN_FRAGCOORD);
   e->shade      = spv_input(m, T_F4, 0);
   /* the RDP interpolates shade in screen space */
   spv_decorate(m, e->shade.id, SPV_DEC_NOPERSPECTIVE);
   e->tc[0]      = spv_input(m, T_F2, 1);
   e->tc[1]      = spv_input(m, T_F2, 2);
   e->lod_tc     = spv_input(m, T_F2, 3);
   e->num_lights = spv_input(m, T_F1, 4);
   e->frag_color = spv_output(m, T_F4, 0);
   if (cfg->fragment_depth_write)
   {
      e->frag_depth_out = spv_builtin_out(m, T_F1, SPV_BUILTIN_FRAGDEPTH);
      spv_mode(m, SPV_MODE_DEPTH_REPLACING);
   }

   spv_begin_main(m);

   /* loads of inputs: every OpVariable above is a pointer */
   {
      spv_var v;
      v.id = e->frag_coord.id; v.t = T_F4;
      e->frag_coord = spv_load(m, v);
      v.id = e->shade.id; v.t = T_F4;
      e->shade = spv_load(m, v);
      v.id = e->tc[0].id; v.t = T_F2;
      e->tc[0] = spv_load(m, v);
      v.id = e->tc[1].id; v.t = T_F2;
      e->tc[1] = spv_load(m, v);
      v.id = e->lod_tc.id; v.t = T_F2;
      e->lod_tc = spv_load(m, v);
      v.id = e->num_lights.id; v.t = T_F1;
      e->num_lights = spv_load(m, v);
   }

   depth_v = spv_local(m, T_F1);
   if (!cfg->fragment_depth_write && !cfg->n64_depth_compare)
      spv_store(m, depth_v, spv_f(m, 0.f));
   else
   {
      if (spv_if(m, &b, ne0(e, u(e, U_depthSource))))
         spv_store(m, depth_v, u(e, U_primDepth));
      if (spv_else(m, &b))
      {
         spv_v scale = u(e, U_depthScale);
         spv_v z     = spv_sub(m, spv_mul(m, spv_x(m, e->frag_coord, 2), spv_f(m, 2.f)), spv_f(m, 1.f));
         spv_store(m, depth_v, spv_clamp(m, spv_add(m, spv_mul(m, z, spv_x(m, scale, 0)), spv_x(m, scale, 1)), 0.f, 1.f));
      }
      spv_endif(m, &b);
   }
   frag_depth_val = spv_load(m, depth_v);

   /* snoise(): always read, the alpha test may want it */
   if (cfg->noise)
   {
      uint32_t ns = load_tex(e, e->noise, 0, 0);
      spv_v    p  = spv_add(m, spv_to_int(m, spv_div(m, spv_xy(m, e->frag_coord), u(e, U_screenScale))),
            spv_to_int(m, u(e, U_noiseOffset)));
      spv_v    w  = spv_ivec(m, T_I2, cfg->noise_width, cfg->noise_height, 0, 0);
      e->snoise   = spv_x(m, spv_fetch(m, ns, spv_rem(m, p, w)), 0);
   }
   else
      e->snoise = spv_f(m, 0.5f);

   e->rt0 = spv_local(m, T_F4);
   e->rt1 = spv_local(m, T_F4);
   spv_store(m, e->rt0, spv_zero(m, T_F4));
   spv_store(m, e->rt1, spv_zero(m, T_F4));

   {
      spv_var frac = spv_local(m, T_F1);
      spv_store(m, frac, spv_f(m, 0.f));
      if (spv_if(m, &b, fl(e, VKR_CF_TEXTURED)))
      {
         spv_if_t lod;
         if (spv_if(m, &lod, fl(e, VKR_CF_LOD)))
            spv_store(m, frac, mipmap(e));
         if (spv_else(m, &lod))
         {
            spv_if_t copy;
            if (spv_if(m, &copy, cycle_lt(e, CYC_COPY)))
            {
               spv_if_t t;
               if (spv_if(m, &t, fl(e, VKR_CF_TILE0)))
                  read_tile0(e);
               spv_endif(m, &t);
               if (spv_if(m, &t, fl(e, VKR_CF_TILE1)))
                  read_tile1(e);
               spv_endif(m, &t);
            }
            if (spv_else(m, &copy))
               read_filtered(e, 0, e->rt0, 1);
            spv_endif(m, &copy);
         }
         spv_endif(m, &lod);
      }
      spv_endif(m, &b);
      e->lod_frac = spv_load(m, frac);
   }

   e->combined = spv_local(m, T_F4);
   spv_store(m, e->combined, spv_zero(m, T_F4));

   {
      spv_var input = spv_local(m, T_F3);
      spv_if_t l;
      if (spv_if(m, &l, fl(e, VKR_CF_HWLIGHT)))
         spv_store(m, input, calc_light(e, spv_xyz(m, e->shade)));
      if (spv_else(m, &l))
         spv_store(m, input, spv_xyz(m, e->shade));
      spv_endif(m, &l);
      e->vec_color = spv_vec3_1(m, spv_load(m, input), spv_x(m, e->shade, 3));
   }

   if (generic)
   {
      e->ci = spv_local_array(m, T_F3, IN_COUNT);
      e->ai = spv_local_array(m, T_F1, IN_COUNT);
      store_inputs(e);
   }

   alpha1 = spv_local(m, T_F1);
   color1 = spv_local(m, T_F3);
   clamped = spv_local(m, T_F4);

   spv_store(m, alpha1, run_stage(e, 0, 1));
   {
      spv_if_t two;
      if (spv_if(m, &two, cycle_is(e, CYC_2)))
         sign_extend(e, alpha1, VKR_CF_ALPHA_C, VKR_CF_ALPHA_ABD);
      spv_endif(m, &two);
   }
   if (spv_if(m, &b, cycle_lt(e, CYC_FILL)))
      alpha_test(e, spv_load(m, alpha1));
   spv_endif(m, &b);

   spv_store(m, color1, run_stage(e, 1, 0));
   {
      spv_if_t two;
      if (spv_if(m, &two, cycle_is(e, CYC_2)))
         sign_extend(e, color1, VKR_CF_COLOR_C, VKR_CF_COLOR_ABD);
      spv_endif(m, &two);
   }

   {
      spv_var res = spv_local(m, T_F4);
      spv_if_t two;
      if (spv_if(m, &two, cycle_is(e, CYC_2)))
      {
         spv_var alpha2 = spv_local(m, T_F1);
         spv_var color2 = spv_local(m, T_F3);
         spv_if_t st, cv;
         spv_v a1 = spv_load(m, alpha1);
         spv_v c1 = spv_load(m, color1);
         spv_v combined = spv_vec3_1(m, c1, a1);
         spv_store(m, e->combined, combined);
         if (generic)
         {
            spv_store_at(m, e->ci, IN_COMBINED, c1);
            spv_store_at(m, e->ci, IN_COMBINED_A, spv_spread(m, a1, 3));
            spv_store_at(m, e->ai, IN_COMBINED, a1);
            spv_store_at(m, e->ai, IN_COMBINED_A, a1);
         }
         if (spv_if(m, &st, fl(e, VKR_CF_TWO_STAGES)))
            spv_store(m, alpha2, run_stage(e, 2, 1));
         if (spv_else(m, &st))
            spv_store(m, alpha2, a1);
         spv_endif(m, &st);
         if (spv_if(m, &cv, spv_and(m, ne0(e, u(e, U_cvgXAlpha)),
                     spv_cond(spv_lt(m, spv_load(m, alpha2), spv_f(m, 0.125f))))))
            spv_kill(m);
         spv_endif(m, &cv);
         if (spv_if(m, &st, fl(e, VKR_CF_TWO_STAGES)))
            spv_store(m, color2, run_stage(e, 3, 0));
         if (spv_else(m, &st))
            spv_store(m, color2, c1);
         spv_endif(m, &st);
         spv_store(m, res, spv_vec3_1(m, spv_load(m, color2), spv_load(m, alpha2)));
      }
      if (spv_else(m, &two))
      {
         spv_if_t cv;
         if (spv_if(m, &cv, spv_and(m, cycle_lt(e, CYC_FILL), ne0(e, u(e, U_cvgXAlpha)))))
         {
            spv_if_t a;
            if (spv_if(m, &a, spv_cond(spv_lt(m, spv_load(m, alpha1), spv_f(m, 0.125f)))))
               spv_kill(m);
            spv_endif(m, &a);
         }
         spv_endif(m, &cv);
         spv_store(m, res, spv_vec3_1(m, spv_load(m, color1), spv_load(m, alpha1)));
      }
      spv_endif(m, &two);

      {
         spv_if_t wrap;
         spv_v r = spv_load(m, res);
         if (spv_if(m, &wrap, cycle_lt(e, CYC_COPY)))
         {
            spv_v w = spv_sub(m, spv_add(m, r, spv_mul(m, spv_step(m, r, spv_spread(m, spv_f(m, -0.51f), 4)), spv_f(m, 2.f))),
                  spv_mul(m, spv_step(m, spv_spread(m, spv_f(m, 1.51f), 4), r), spv_f(m, 2.f)));
            spv_store(m, clamped, spv_clamp(m, w, 0.f, 1.f));
         }
         if (spv_else(m, &wrap))
            spv_store(m, clamped, spv_clamp(m, r, 0.f, 1.f));
         spv_endif(m, &wrap);
      }
   }

   if (spv_if(m, &b, cycle_lt(e, CYC_COPY)))
   {
      noise_dither(e, clamped);
      if (!cfg->legacy_blending)
      {
         spv_var pm = spv_local_array(m, T_F4, 4);
         spv_var a  = spv_local_array(m, T_F1, 4);
         spv_var bv = spv_local_array(m, T_F1, 4);
         spv_v fog  = u(e, U_fogColor);
         spv_if_t two;
         spv_store_at(m, pm, 0, spv_zero(m, T_F4));
         spv_store_at(m, pm, 1, spv_zero(m, T_F4));
         spv_store_at(m, pm, 2, u(e, U_blendColor));
         spv_store_at(m, pm, 3, fog);
         spv_store_at(m, a, 0, spv_f(m, 0.f));
         spv_store_at(m, a, 1, spv_x(m, fog, 3));
         spv_store_at(m, a, 2, spv_x(m, e->shade, 3));
         spv_store_at(m, a, 3, spv_f(m, 0.f));
         spv_store_at(m, bv, 0, spv_f(m, 0.f));
         spv_store_at(m, bv, 1, spv_f(m, 1.f));
         spv_store_at(m, bv, 2, spv_f(m, 1.f));
         spv_store_at(m, bv, 3, spv_f(m, 0.f));
         blender(e, pm, a, bv, clamped, U_blendMux1, U_forceBlendCycle1, 1);
         if (spv_if(m, &two, cycle_is(e, CYC_2)))
            blender(e, pm, a, bv, clamped, U_blendMux2, U_forceBlendCycle2, 0);
         spv_endif(m, &two);
      }
   }
   spv_endif(m, &b);

   {
      spv_v c = spv_load(m, clamped);
      spv_store(m, e->frag_color, c);
      if (cfg->legacy_blending && spv_if(m, &b, spv_cond(spv_eq(m, u(e, U_fogUsage), spv_i(m, 1)))))
      {
         spv_v fc = spv_xyz(m, u(e, U_fogColor));
         spv_store(m, e->frag_color, spv_vec3_1(m, spv_mix(m, spv_xyz(m, c), fc, spv_x(m, e->shade, 3)), spv_x(m, c, 3)));
      }
      if (cfg->legacy_blending)
         spv_endif(m, &b);
   }

   if (cfg->n64_depth_compare)
   {
      spv_var keep = spv_local(m, T_I1);
      spv_if_t rt, k;
      if (cfg->interlock)
         spv_begin_interlock(m);
      if (spv_if(m, &rt, ne0(e, u(e, U_renderTarget))))
         spv_store(m, keep, depth_render(e, spv_x(m, spv_load(m, e->frag_color), 0), frag_depth_val));
      if (spv_else(m, &rt))
         spv_store(m, keep, depth_compare(e, frag_depth_val));
      spv_endif(m, &rt);
      if (cfg->interlock)
         spv_end_interlock(m);
      if (spv_if(m, &k, eq_i(e, spv_load(m, keep), 0)))
         spv_kill(m);
      spv_endif(m, &k);
   }
   else if (cfg->fragment_depth_write)
   {
      spv_if_t rt, deep;
      if (spv_if(m, &rt, ne0(e, u(e, U_renderTarget))))
      {
         if (spv_if(m, &deep, spv_cond(spv_gt(m, u(e, U_renderTarget), spv_i(m, 1)))))
         {
            spv_v d = spv_x(m, fetch_frag(e, load_tex(e, e->depth, 0, 0)), 0);
            spv_if_t k;
            if (spv_if(m, &k, spv_cond(spv_ge(m, frag_depth_val, d))))
               spv_kill(m);
            spv_endif(m, &k);
         }
         spv_endif(m, &deep);
         spv_store(m, depth_v, spv_x(m, spv_load(m, e->frag_color), 0));
      }
      spv_endif(m, &rt);
      spv_store(m, e->frag_depth_out, spv_load(m, depth_v));
   }

   spv_end_main(m);
   return spv_finish(m, count);
}

/* ---- vertex shaders ---- */

static spv_v load_in(spv_module *m, spv_v in)
{
   spv_var v;
   v.id = in.id;
   v.t  = in.t;
   return spv_load(m, v);
}

const uint32_t *vkr_write_vertex(spv_module *m, const vkr_shader_config *cfg, int kind, unsigned *count)
{
   E e_;
   E *e = &e_;
   spv_var pos;
   uint32_t clip;

   declare_common(e, m, cfg, SPV_STAGE_VERTEX);
   pos = spv_builtin_out(m, T_F4, SPV_BUILTIN_POSITION);

   if (kind == VKR_VS_TRIANGLE)
   {
      spv_v a_pos = spv_input(m, T_F4, 0);
      spv_v a_col = spv_input(m, T_F4, 1);
      spv_v a_tc  = spv_input(m, T_F2, 2);
      spv_v a_mod = spv_input(m, T_F4, 3);
      spv_v a_nl  = spv_input(m, T_F1, 4);
      spv_var v_shade = spv_output(m, T_F4, 0);
      spv_var v_tc0   = spv_output(m, T_F2, 1);
      spv_var v_tc1   = spv_output(m, T_F2, 2);
      spv_var v_lod   = spv_output(m, T_F2, 3);
      spv_var v_nl    = spv_output(m, T_F1, 4);
      spv_var p = spv_local(m, T_F4);
      spv_var shade = spv_local(m, T_F4);
      spv_var numl  = spv_local(m, T_F1);
      spv_var tc    = spv_local(m, T_F2);
      spv_v in_pos, in_mod, mx, my, mz, mw;
      spv_if_t b, s;
      int t;

      clip = spv_clip_distance(m);
      spv_begin_main(m);
      in_pos = load_in(m, a_pos);
      in_mod = load_in(m, a_mod);
      mx = spv_x(m, in_mod, 0);
      my = spv_x(m, in_mod, 1);
      mz = spv_x(m, in_mod, 2);
      mw = spv_x(m, in_mod, 3);
      spv_store(m, p, in_pos);
      spv_store(m, shade, load_in(m, a_col));
      spv_store(m, numl, load_in(m, a_nl));

      /* texture coordinates */
      {
         spv_c no_persp = spv_and(m, eq_i(e, u(e, U_texturePersp), 0),
               spv_cond(spv_eq(m, mz, spv_f(m, 0.f))));
         spv_store(m, tc, spv_mul(m, load_in(m, a_tc), u(e, U_texScale)));
         if (spv_if(m, &b, no_persp))
            spv_store(m, tc, spv_mul(m, spv_load(m, tc), spv_f(m, 0.5f)));
         spv_endif(m, &b);
      }
      for (t = 0; t < 2; t++)
      {
         static const int off[2]  = { U_texOffset0, U_texOffset1 };
         static const int ss[2]   = { U_cacheShiftScale0, U_cacheShiftScale1 };
         static const int sc[2]   = { U_cacheScale0, U_cacheScale1 };
         static const int co[2]   = { U_cacheOffset0, U_cacheOffset1 };
         spv_v o = spv_sub(m, spv_mul(m, spv_load(m, tc), u(e, ss[t])), u(e, off[t]));
         spv_v r = spv_mul(m, spv_add(m, u(e, co[t]), o), u(e, sc[t]));
         spv_store(m, t == 0 ? v_tc0 : v_tc1, r);
      }
      spv_store(m, v_lod, spv_load(m, tc));

      if (spv_if(m, &b, spv_not(m, spv_cond(spv_eq(m, spv_spread(m, spv_f(m, 0.f), 1), mx)))))
      {
         spv_v q = spv_load(m, p);
         spv_v xy_ = spv_add(m, spv_mul(m, spv_xy(m, q), u(e, U_screenCoordsScale)), spv_vec(m, T_F2, -1.f, 1.f, 0, 0));
         xy_ = spv_mul(m, xy_, spv_x(m, q, 3));
         spv_store(m, p, spv_vec4(m, spv_x(m, xy_, 0), spv_x(m, xy_, 1), spv_x(m, q, 2), spv_x(m, q, 3)));
      }
      spv_endif(m, &b);
      if (spv_if(m, &b, spv_not(m, spv_cond(spv_eq(m, my, spv_f(m, 0.f))))))
      {
         spv_v q = spv_load(m, p);
         spv_store(m, p, spv_vec4(m, spv_x(m, q, 0), spv_x(m, q, 1), spv_mul(m, spv_x(m, q, 2), spv_x(m, q, 3)), spv_x(m, q, 3)));
      }
      spv_endif(m, &b);
      if (spv_if(m, &b, spv_not(m, spv_cond(spv_eq(m, mw, spv_f(m, 0.f))))))
         spv_store(m, numl, spv_f(m, 0.f));
      spv_endif(m, &b);

      /* gl_Position.y = -gl_Position.y */
      {
         spv_v q = spv_load(m, p);
         spv_store(m, p, spv_vec4(m, spv_x(m, q, 0), spv_neg(m, spv_x(m, q, 1)), spv_x(m, q, 2), spv_x(m, q, 3)));
      }
      /* a temporal upscaler's sub-pixel jitter, as a part of the viewport:
       * zero without one */
      {
         spv_v q = spv_load(m, p);
         spv_v j = spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_JITTER, T_F2);
         spv_v w = spv_x(m, q, 3);
         spv_store(m, p, spv_vec4(m, spv_add(m, spv_x(m, q, 0), spv_mul(m, spv_x(m, j, 0), w)),
                  spv_add(m, spv_x(m, q, 1), spv_mul(m, spv_x(m, j, 1), w)), spv_x(m, q, 2), w));
      }

      if (spv_if(m, &b, spv_cond(spv_gt(m, u(e, U_fogUsage), spv_i(m, 0)))))
      {
         spv_v scale = u(e, U_fogScale);
         spv_v az    = spv_x(m, in_pos, 2);
         spv_v aw    = spv_x(m, in_pos, 3);
         spv_var fp  = spv_local(m, T_F1);
         spv_if_t near_;
         if (spv_if(m, &near_, spv_and(m, spv_cond(spv_lt(m, az, spv_neg(m, aw))),
                     spv_cond(spv_eq(m, my, spv_f(m, 0.f))))))
            spv_store(m, fp, spv_add(m, spv_neg(m, spv_x(m, scale, 0)), spv_x(m, scale, 1)));
         if (spv_else(m, &near_))
            spv_store(m, fp, spv_add(m, spv_mul(m, spv_div(m, az, aw), spv_x(m, scale, 0)), spv_x(m, scale, 1)));
         spv_endif(m, &near_);
         {
            spv_v f = spv_clamp(m, spv_load(m, fp), 0.f, 1.f);
            spv_v c = spv_load(m, shade);
            if (spv_if(m, &s, eq_i(e, u(e, U_fogUsage), 1)))
               spv_store(m, shade, spv_vec3_1(m, spv_xyz(m, c), f));
            if (spv_else(m, &s))
               spv_store(m, shade, spv_vec4(m, f, f, f, spv_x(m, c, 3)));
            spv_endif(m, &s);
         }
      }
      spv_endif(m, &b);

      spv_store(m, v_shade, spv_load(m, shade));
      spv_store(m, v_nl, spv_load(m, numl));
      {
         spv_v q = spv_load(m, p);
         spv_v w = spv_x(m, q, 3);
         spv_v z = spv_x(m, q, 2);
         spv_var cd = spv_local(m, T_F1);
         spv_if_t c;
         spv_store(m, cd, spv_f(m, 1.f));
         if (spv_if(m, &c, ne0(e, spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_CLIP, T_I1))))
            spv_store(m, cd, spv_sub(m, w, z));
         spv_endif(m, &c);
         spv_store_clip(m, clip, spv_load(m, cd));
         /* to Vulkan's depth: GL's -1..1 is 0..1 here */
         spv_store(m, pos, spv_vec4(m, spv_x(m, q, 0), spv_x(m, q, 1),
                  spv_mul(m, spv_add(m, z, w), spv_f(m, 0.5f)), w));
      }
      spv_end_main(m);
      return spv_finish(m, count);
   }

   if (kind == VKR_VS_RECT)
   {
      spv_v a_pos = spv_input(m, T_F4, 0);
      spv_v a_t0  = spv_input(m, T_F2, 1);
      spv_v a_t1  = spv_input(m, T_F2, 2);
      spv_var v_shade = spv_output(m, T_F4, 0);
      spv_var v_tc0   = spv_output(m, T_F2, 1);
      spv_var v_tc1   = spv_output(m, T_F2, 2);
      spv_var v_lod   = spv_output(m, T_F2, 3);
      spv_var v_nl    = spv_output(m, T_F1, 4);
      spv_v q, z, w;

      clip = spv_clip_distance(m);
      spv_begin_main(m);
      q = load_in(m, a_pos);
      z = spv_x(m, q, 2);
      w = spv_x(m, q, 3);
      spv_store(m, v_shade, u(e, U_rectColor));
      spv_store(m, v_tc0, load_in(m, a_t0));
      spv_store(m, v_tc1, load_in(m, a_t1));
      spv_store(m, v_lod, spv_zero(m, T_F2));
      spv_store(m, v_nl, spv_f(m, 0.f));
      {
         spv_var cd = spv_local(m, T_F1);
         spv_if_t c;
         spv_store(m, cd, spv_f(m, 1.f));
         if (spv_if(m, &c, ne0(e, spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_CLIP, T_I1))))
            spv_store(m, cd, spv_sub(m, w, z));
         spv_endif(m, &c);
         spv_store_clip(m, clip, spv_load(m, cd));
      }
      spv_store(m, pos, spv_vec4(m, spv_x(m, q, 0), spv_x(m, q, 1),
               spv_mul(m, spv_add(m, z, w), spv_f(m, 0.5f)), w));
      spv_end_main(m);
      return spv_finish(m, count);
   }

   /* special shaders: a rect and at most its first coordinate */
   {
      spv_v a_pos = spv_input(m, T_F4, 0);
      spv_v q, z, w;

      if (kind == VKR_VS_FXAA)
      {
         spv_var nw = spv_output(m, T_F2, 0);
         spv_var ne = spv_output(m, T_F2, 1);
         spv_var sw = spv_output(m, T_F2, 2);
         spv_var se = spv_output(m, T_F2, 3);
         spv_var mm = spv_output(m, T_F2, 4);
         spv_v uv, fc, inv, size;

         spv_begin_main(m);
         q = load_in(m, a_pos);
         z = spv_x(m, q, 2);
         w = spv_x(m, q, 3);
         size = spv_xy(m, spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_SPECIAL, T_F4));
         uv  = spv_mul(m, spv_add(m, spv_xy(m, q), spv_f(m, 1.f)), spv_f(m, 0.5f));
         fc  = spv_mul(m, uv, size);
         inv = spv_div(m, spv_vec(m, T_F2, 1.f, 1.f, 0, 0), size);
         spv_store(m, nw, spv_mul(m, spv_add(m, fc, spv_vec(m, T_F2, -1.f, -1.f, 0, 0)), inv));
         spv_store(m, ne, spv_mul(m, spv_add(m, fc, spv_vec(m, T_F2, 1.f, -1.f, 0, 0)), inv));
         spv_store(m, sw, spv_mul(m, spv_add(m, fc, spv_vec(m, T_F2, -1.f, 1.f, 0, 0)), inv));
         spv_store(m, se, spv_mul(m, spv_add(m, fc, spv_vec(m, T_F2, 1.f, 1.f, 0, 0)), inv));
         spv_store(m, mm, spv_mul(m, fc, inv));
      }
      else
      {
         spv_v a_t0  = spv_input(m, T_F2, 1);
         spv_var v_tc0 = spv_output(m, T_F2, 1);
         spv_begin_main(m);
         q = load_in(m, a_pos);
         z = spv_x(m, q, 2);
         w = spv_x(m, q, 3);
         spv_store(m, v_tc0, load_in(m, a_t0));
      }
      spv_store(m, pos, spv_vec4(m, spv_x(m, q, 0), spv_x(m, q, 1),
               spv_mul(m, spv_add(m, z, w), spv_f(m, 0.5f)), w));
      spv_end_main(m);
      return spv_finish(m, count);
   }
}

/* ---- special fragment shaders ---- */

static spv_v pc_special(E *e)
{
   return spv_load_member(e->m, e->pc, SPV_PUSH_CONSTANT, PC_SPECIAL, T_F4);
}

static void texrect_filter(E *e, uint32_t s, spv_v tc, spv_var out, spv_v test)
{
   spv_module *m = e->m;
   spv_v c = spv_sample(m, s, tc);
   spv_if_t b;
   spv_v size, off, ox, oy;
   spv_v taps[4];
   int i;

   if (spv_if(m, &b, spv_cond(spv_all(m, spv_eq(m, c, test)))))
      spv_kill(m);
   spv_endif(m, &b);
   if (spv_if(m, &b, spv_and(m, eq_i(e, spv_to_int(m, spv_x(m, pc_special(e), 0)), 1),
               spv_not(m, spv_cond(spv_gt(m, spv_x(m, c, 3), spv_f(m, 0.f)))))))
      spv_kill(m);
   spv_endif(m, &b);

   size = spv_to_float(m, spv_texture_size(m, s, spv_i(m, 0)));
   off  = spv_fract(m, spv_sub(m, spv_mul(m, tc, size), spv_vec(m, T_F2, 0.5f, 0.5f, 0, 0)));
   off  = spv_sub(m, off, spv_step(m, spv_f(m, 1.f), spv_add(m, spv_x(m, off, 0), spv_x(m, off, 1))));
   ox = spv_x(m, off, 0);
   oy = spv_x(m, off, 1);
   {
      spv_v sx = spv_sub(m, ox, spv_sign(m, ox));
      spv_v sy = spv_sub(m, oy, spv_sign(m, oy));
      taps[0] = off;
      taps[1] = spv_vec2(m, sx, oy);
      taps[2] = spv_vec2(m, ox, sy);
      taps[3] = spv_vec2(m, sx, sy);
   }
   for (i = 0; i < 4; i++)
   {
      spv_v t;
      spv_v eqv;
      if (i == 3 && e->cfg->three_point)
         break;
      t   = tex_at(e, s, tc, size, taps[i]);
      eqv = spv_bool_to_float(m, spv_eq(m, t, test));
      taps[i] = spv_add(m, spv_mul(m, c, eqv), spv_mul(m, t, spv_sub(m, spv_vec(m, T_F4, 1.f, 1.f, 1.f, 1.f), eqv)));
   }
   if (e->cfg->three_point)
      spv_store(m, out, spv_add(m, spv_add(m, taps[0], spv_mul(m, spv_sub(m, taps[1], taps[0]), spv_abs(m, ox))),
               spv_mul(m, spv_sub(m, taps[2], taps[0]), spv_abs(m, oy))));
   else
   {
      spv_v fx = spv_abs(m, ox);
      spv_v fy = spv_abs(m, oy);
      spv_v q0 = spv_mix(m, taps[0], taps[1], fx);
      spv_v q1 = spv_mix(m, taps[2], taps[3], fx);
      spv_store(m, out, spv_mix(m, q0, q1, fy));
   }
}

const uint32_t *vkr_write_special(spv_module *m, const vkr_shader_config *cfg, int kind, unsigned *count)
{
   E e_;
   E *e = &e_;
   spv_var color;
   uint32_t tex0, zlut, tlut, depth;

   declare_common(e, m, cfg, SPV_STAGE_FRAGMENT);
   color = spv_output(m, T_F4, 0);
   tex0  = declare_sampler(e, 0, 0, VKR_BIND_TEX0);

   if (kind == VKR_FS_SHADOW_MAP)
   {
      spv_v fc = spv_builtin_in(m, T_F4, SPV_BUILTIN_FRAGCOORD);
      uint32_t zimg = 0, zimg_var = 0;
      zlut  = declare_sampler(e, 1, 0, VKR_BIND_ZLUT);
      tlut  = declare_sampler(e, 1, 0, VKR_BIND_TLUT);
      /* read from texture unit 0, where GLideN64 binds it for this */
      depth = tex0;
      if (cfg->n64_depth_compare)
      {
         zimg     = spv_storage_image(m);
         zimg_var = declare_storage(e, VKR_BIND_DEPTH_Z);
      }
      spv_begin_main(m);
      {
         spv_v coord = spv_to_int(m, spv_xy(m, load_in(m, fc)));
         spv_var buf = spv_local(m, T_F1);
         spv_v iz, y0, x0, tex_coord, n64z, index, a, fog;
         if (cfg->n64_depth_compare)
            spv_store(m, buf, spv_x(m, spv_image_load(m, spv_load_sampler(m, zimg_var, zimg), coord), 0));
         else
            spv_store(m, buf, spv_x(m, spv_fetch(m, load_tex(e, depth, 0, 0), coord), 0));
         {
            spv_v z = spv_load(m, buf);
            iz = spv_select(m, spv_gt(m, z, spv_f(m, 0.999f)), spv_i(m, 262143),
                  spv_to_int(m, spv_floor(m, spv_mul(m, z, spv_f(m, 262143.f)))));
         }
         y0 = spv_div(m, iz, spv_i(m, 512));
         y0 = spv_to_int(m, spv_clamp(m, spv_to_float(m, y0), 0.f, 511.f));
         x0 = spv_sub(m, iz, spv_mul(m, spv_i(m, 512), y0));
         tex_coord = spv_vec2(m, x0, y0);
         n64z = spv_clamp(m, spv_div(m, spv_to_float(m, spv_fetch_uint(m, load_tex(e, zlut, 1, 0), tex_coord)), spv_f(m, 65532.f)), 0.f, 1.f);
         index = spv_to_int(m, spv_min(m, spv_f(m, 255.f), spv_mul(m, n64z, spv_f(m, 255.f))));
         a = spv_fetch_uint(m, load_tex(e, tlut, 1, 0), spv_vec2(m, index, spv_i(m, 0)));
         a = spv_div(m, spv_to_float(m, spv_shr(m, a, spv_i(m, 8))), spv_f(m, 255.f));
         fog = pc_special(e);
         spv_store(m, color, spv_vec3_1(m, spv_xyz(m, fog), a));
      }
      spv_end_main(m);
      return spv_finish(m, count);
   }

   if (kind == VKR_FS_FXAA)
   {
      spv_v v_nw = spv_input(m, T_F2, 0);
      spv_v v_ne = spv_input(m, T_F2, 1);
      spv_v v_sw = spv_input(m, T_F2, 2);
      spv_v v_se = spv_input(m, T_F2, 3);
      spv_v v_m  = spv_input(m, T_F2, 4);
      spv_v fc   = spv_builtin_in(m, T_F4, SPV_BUILTIN_FRAGCOORD);
      spv_begin_main(m);
      {
         uint32_t s  = load_tex(e, tex0, 0, 0);
         spv_v size  = spv_xy(m, pc_special(e));
         spv_v inv   = spv_div(m, spv_vec(m, T_F2, 1.f, 1.f, 0, 0), size);
         spv_v rgbNW = spv_xyz(m, spv_sample(m, s, load_in(m, v_nw)));
         spv_v rgbNE = spv_xyz(m, spv_sample(m, s, load_in(m, v_ne)));
         spv_v rgbSW = spv_xyz(m, spv_sample(m, s, load_in(m, v_sw)));
         spv_v rgbSE = spv_xyz(m, spv_sample(m, s, load_in(m, v_se)));
         spv_v tex   = spv_sample(m, s, load_in(m, v_m));
         spv_v rgbM  = spv_xyz(m, tex);
         spv_v l     = spv_vec(m, T_F3, 0.299f, 0.587f, 0.114f, 0);
         spv_v lNW   = spv_dot(m, rgbNW, l);
         spv_v lNE   = spv_dot(m, rgbNE, l);
         spv_v lSW   = spv_dot(m, rgbSW, l);
         spv_v lSE   = spv_dot(m, rgbSE, l);
         spv_v lM    = spv_dot(m, rgbM, l);
         spv_v lMin  = spv_min(m, lM, spv_min(m, spv_min(m, lNW, lNE), spv_min(m, lSW, lSE)));
         spv_v lMax  = spv_max(m, lM, spv_max(m, spv_max(m, lNW, lNE), spv_max(m, lSW, lSE)));
         spv_v dirx  = spv_neg(m, spv_sub(m, spv_add(m, lNW, lNE), spv_add(m, lSW, lSE)));
         spv_v diry  = spv_sub(m, spv_add(m, lNW, lSW), spv_add(m, lNE, lSE));
         spv_v red   = spv_max(m, spv_mul(m, spv_add(m, spv_add(m, spv_add(m, lNW, lNE), lSW), lSE),
                  spv_f(m, 0.25f * (1.0f / 8.0f))), spv_f(m, 1.0f / 128.0f));
         spv_v rcp   = spv_div(m, spv_f(m, 1.f), spv_add(m, spv_min(m, spv_abs(m, dirx), spv_abs(m, diry)), red));
         spv_v dir   = spv_mul(m, spv_min(m, spv_vec(m, T_F2, 8.f, 8.f, 0, 0),
                  spv_max(m, spv_vec(m, T_F2, -8.f, -8.f, 0, 0), spv_mul(m, spv_vec2(m, dirx, diry), rcp))), inv);
         spv_v base  = spv_mul(m, spv_xy(m, load_in(m, fc)), inv);
         spv_v rgbA  = spv_mul(m, spv_f(m, 0.5f), spv_add(m,
                  spv_xyz(m, spv_sample(m, s, spv_add(m, base, spv_mul(m, dir, spv_f(m, 1.f / 3.f - 0.5f))))),
                  spv_xyz(m, spv_sample(m, s, spv_add(m, base, spv_mul(m, dir, spv_f(m, 2.f / 3.f - 0.5f)))))));
         spv_v rgbB  = spv_add(m, spv_mul(m, rgbA, spv_f(m, 0.5f)), spv_mul(m, spv_f(m, 0.25f), spv_add(m,
                  spv_xyz(m, spv_sample(m, s, spv_add(m, base, spv_mul(m, dir, spv_f(m, -0.5f))))),
                  spv_xyz(m, spv_sample(m, s, spv_add(m, base, spv_mul(m, dir, spv_f(m, 0.5f))))))));
         spv_v lB    = spv_dot(m, rgbB, l);
         spv_var res = spv_local(m, T_F4);
         spv_if_t b;
         if (spv_if(m, &b, spv_or(m, spv_cond(spv_lt(m, lB, lMin)), spv_cond(spv_gt(m, lB, lMax)))))
            spv_store(m, res, spv_vec3_1(m, rgbA, spv_x(m, tex, 3)));
         if (spv_else(m, &b))
            spv_store(m, res, spv_vec3_1(m, rgbB, spv_x(m, tex, 3)));
         spv_endif(m, &b);
         spv_store(m, color, spv_load(m, res));
      }
      spv_end_main(m);
      return spv_finish(m, count);
   }

   {
      spv_v v_tc0;
      spv_var depth_out;

      depth_out.id = 0;
      depth_out.t  = 0;
      v_tc0 = spv_input(m, T_F2, 1);
      if (kind == VKR_FS_TEXRECT_DRAW && cfg->fragment_depth_write && !cfg->n64_depth_compare)
      {
         depth_out = spv_builtin_out(m, T_F1, SPV_BUILTIN_FRAGDEPTH);
         spv_mode(m, SPV_MODE_DEPTH_REPLACING);
      }
      spv_begin_main(m);
      {
         spv_v tc = load_in(m, v_tc0);
         switch (kind)
         {
            case VKR_FS_TEXRECT_DRAW:
               texrect_filter(e, load_tex(e, tex0, 0, 0), tc, color,
                     spv_vec(m, T_F4, 4.f / 255.f, 2.f / 255.f, 1.f / 255.f, 0.f));
               if (depth_out.id)
                  spv_store(m, depth_out, spv_x(m, pc_special(e), 1));
               break;
            case VKR_FS_TEXRECT_CLEAR:
               spv_store(m, color, spv_vec(m, T_F4, 4.f / 255.f, 2.f / 255.f, 1.f / 255.f, 0.f));
               break;
            case VKR_FS_GAMMA:
            {
               spv_v c = spv_sample(m, load_tex(e, tex0, 0, 0), tc);
               spv_v p = spv_pow(m, spv_xyz(m, c), spv_spread(m, spv_div(m, spv_f(m, 1.f), spv_x(m, pc_special(e), 0)), 3));
               spv_store(m, color, spv_vec3_1(m, p, spv_x(m, c, 3)));
               break;
            }
            default:
               spv_store(m, color, spv_sample(m, load_tex(e, tex0, 0, 0), tc));
               break;
         }
      }
      spv_end_main(m);
      return spv_finish(m, count);
   }
}
