#include <stddef.h>

#include "vk_shaders.h"
#include "glitch64_vk_shaders.h"

/* the uniform block's members, in glitch64_vk_values' order */
enum
{
   U_PRIM = 0, U_ENV, U_CONSTS, U_LEVEL, U_KEY_CENTER, U_KEY_SCALE, U_FOG_COLOR,
   U_TEX_SIZES, U_EXACT_SIZES, U_WINDOW, U_TEST, U_BLEND_COLOR, U_CONVERT, U_COUNT
};

/* the push block's, as vk_shaders.h has it */
enum { PC_OPS = 0, PC_FLAGS, PC_COUNT };

typedef struct g64_emit
{
   spv_module *m;
   uint32_t ubo, pc;
   /* a combiner's own shader: its ops and flags known, else read as pushed */
   const int *key;
} g64_emit;

static void declare_values(g64_emit *e)
{
   spv_module *m = e->m;
   uint32_t members[U_COUNT];
   uint32_t block;
   unsigned i;
   for (i = 0; i < U_COUNT; i++)
      members[i] = spv_type(m, T_F4);
   block = spv_struct(m, members, U_COUNT);
   spv_decorate(m, block, SPV_DEC_BLOCK);
   for (i = 0; i < U_COUNT; i++)
      spv_member_decorate(m, block, i, SPV_DEC_OFFSET, i * 16u);
   e->ubo = spv_global(m, block, SPV_UNIFORM);
   spv_decorate1(m, e->ubo, SPV_DEC_SET, 0);
   spv_decorate1(m, e->ubo, SPV_DEC_BINDING, VKR_BIND_VALUES);
}

static void declare_push(g64_emit *e)
{
   spv_module *m = e->m;
   uint32_t members[PC_COUNT];
   uint32_t block;
   members[PC_OPS]   = spv_array_type(m, spv_type(m, T_I1), 16, 4);
   members[PC_FLAGS] = spv_type(m, T_I1);
   block = spv_struct(m, members, PC_COUNT);
   spv_decorate(m, block, SPV_DEC_BLOCK);
   spv_member_decorate(m, block, PC_OPS, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, ops));
   spv_member_decorate(m, block, PC_FLAGS, SPV_DEC_OFFSET, (uint32_t)offsetof(vkr_push, flags));
   e->pc = spv_global(m, block, SPV_PUSH_CONSTANT);
}

static uint32_t declare_sampler(spv_module *m, uint32_t binding)
{
   uint32_t id = spv_global(m, spv_sampled_image(m, 0, 0), SPV_UNIFORM_CONSTANT);
   spv_decorate1(m, id, SPV_DEC_SET, 0);
   spv_decorate1(m, id, SPV_DEC_BINDING, binding);
   return id;
}

static spv_v u(g64_emit *e, int member)
{
   return spv_load_member(e->m, e->ubo, SPV_UNIFORM, (uint32_t)member, T_F4);
}

static spv_v load_in(spv_module *m, spv_v in)
{
   spv_var v;
   v.id = in.id;
   v.t  = in.t;
   return spv_load(m, v);
}

static spv_c flag(g64_emit *e, int bit)
{
   spv_module *m = e->m;
   spv_v f;
   if (e->key)
      return spv_known((e->key[16] & bit) != 0);
   f = spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_FLAGS, T_I1);
   return spv_cond(spv_ne(m, spv_band(m, f, spv_i(m, bit)), spv_i(m, 0)));
}

/* a flag as 1.0 or 0.0 */
static spv_v flag_f(g64_emit *e, int bit)
{
   spv_module *m = e->m;
   spv_v f;
   if (e->key)
      return spv_f(m, (e->key[16] & bit) ? 1.f : 0.f);
   f = spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_FLAGS, T_I1);
   return spv_bool_to_float(m, spv_ne(m, spv_band(m, f, spv_i(m, bit)), spv_i(m, 0)));
}

static spv_v xy_(spv_module *m, spv_v v) { return spv_xy(m, v); }
static spv_v zw_(spv_module *m, spv_v v) { return spv_vec2(m, spv_x(m, v, 2), spv_x(m, v, 3)); }

const uint32_t *glitch64_vk_vertex(spv_module *m, unsigned *count)
{
   g64_emit e;
   spv_v a_pos, a_col, a_tc0, a_tc1;
   spv_var pos, v_shade, v_tc0, v_tc1, v_xy;

   e.m = m;
   e.key = NULL;
   spv_begin(m, SPV_STAGE_VERTEX);
   declare_values(&e);
   pos     = spv_builtin_out(m, T_F4, SPV_BUILTIN_POSITION);
   a_pos   = spv_input(m, T_F4, 0);
   a_col   = spv_input(m, T_F4, 1);
   a_tc0   = spv_input(m, T_F2, 2);
   a_tc1   = spv_input(m, T_F2, 3);
   v_shade = spv_output(m, T_F4, 0);
   v_tc0   = spv_output(m, T_F2, 1);
   v_tc1   = spv_output(m, T_F2, 2);
   v_xy    = spv_output(m, T_F2, 4);
   /* the RDP steps the shade evenly across the screen */
   spv_decorate(m, v_shade.id, SPV_DEC_NOPERSPECTIVE);

   spv_begin_main(m);
   {
      spv_v p    = load_in(m, a_pos);
      spv_v q    = spv_x(m, p, 3);
      spv_v win  = u(&e, U_WINDOW);
      spv_v wx   = spv_x(m, win, 0), wy = spv_x(m, win, 1);
      spv_v x    = spv_div(m, spv_sub(m, spv_x(m, p, 0), wx), wx);
      spv_v y    = spv_mul(m, spv_x(m, win, 2), spv_neg(m, spv_div(m, spv_sub(m, spv_x(m, p, 1), wy), wy)));
      spv_v z    = spv_div(m, spv_x(m, p, 2), spv_f(m, 65536.f));
      spv_v inv  = spv_div(m, spv_f(m, 1.f), q);
      spv_v sizes = u(&e, U_TEX_SIZES);

      /* GL's clip space over q, its depth's -1..1 as Vulkan's 0..1 */
      spv_store(m, pos, spv_vec4(m, spv_mul(m, x, inv), spv_mul(m, y, inv),
               spv_mul(m, spv_add(m, spv_mul(m, z, inv), inv), spv_f(m, 0.5f)), inv));
      spv_store(m, v_shade, load_in(m, a_col));
      spv_store(m, v_tc0, spv_div(m, spv_div(m, load_in(m, a_tc0), q), xy_(m, sizes)));
      spv_store(m, v_tc1, spv_div(m, spv_div(m, load_in(m, a_tc1), q), zw_(m, sizes)));
      spv_store(m, v_xy, spv_xy(m, p));
   }
   spv_end_main(m);
   return spv_finish(m, count);
}

/* the texel, or Glitch64's 3-point filter of it: the nearest three
 * texels' plane, from the texture filtered as nearest */
static spv_v read_texel(g64_emit *e, uint32_t var, spv_v tc, spv_v size, int bit)
{
   spv_module *m = e->m;
   uint32_t s = spv_load_sampler(m, var, spv_sampled_image(m, 0, 0));
   spv_var out = spv_local(m, T_F4);
   spv_if_t b;
   if (spv_if(m, &b, flag(e, bit)))
   {
      spv_v off = spv_fract(m, spv_sub(m, spv_mul(m, tc, size), spv_vec(m, T_F2, 0.5f, 0.5f, 0, 0)));
      spv_v ox, oy, c0, c1, c2;
      off = spv_sub(m, off, spv_spread(m, spv_step(m, spv_f(m, 1.f), spv_add(m, spv_x(m, off, 0), spv_x(m, off, 1))), 2));
      ox = spv_x(m, off, 0);
      oy = spv_x(m, off, 1);
      c0 = spv_sample(m, s, spv_sub(m, tc, spv_div(m, off, size)));
      c1 = spv_sample(m, s, spv_sub(m, tc, spv_div(m, spv_vec2(m, spv_sub(m, ox, spv_sign(m, ox)), oy), size)));
      c2 = spv_sample(m, s, spv_sub(m, tc, spv_div(m, spv_vec2(m, ox, spv_sub(m, oy, spv_sign(m, oy))), size)));
      spv_store(m, out, spv_add(m, spv_add(m, c0, spv_mul(m, spv_abs(m, ox), spv_sub(m, c1, c0))),
               spv_mul(m, spv_abs(m, oy), spv_sub(m, c2, c0))));
   }
   if (spv_else(m, &b))
      spv_store(m, out, spv_sample(m, s, tc));
   spv_endif(m, &b);
   return spv_load(m, out);
}

static spv_v splat4(spv_module *m, spv_v a) { return spv_spread(m, a, 4); }

/* tile 1's texel, or tile 0's converted (GLITCH64_N64_CONVERT1): through
 * K0-K3 at the RDP's 8-bit steps, or its blue */
static spv_v convert_texel(g64_emit *e, spv_v t0, spv_v t1)
{
   spv_module *m = e->m;
   spv_var out = spv_local(m, T_F4);
   spv_if_t b;
   spv_store(m, out, t1);
   if (spv_if(m, &b, flag(e, G64VK_CONVERT1)))
   {
      spv_v k = u(e, U_CONVERT);
      spv_v y = spv_floor(m, spv_add(m, spv_mul(m, spv_xyz(m, t0), spv_f(m, 255.f)), spv_spread(m, spv_f(m, 0.5f), 3)));
      spv_v yr = spv_x(m, y, 0), yg = spv_x(m, y, 1), yb = spv_x(m, y, 2);
      spv_v half = spv_f(m, 0.5f);
      spv_v r = spv_add(m, yb, spv_floor(m, spv_add(m, spv_mul(m, spv_x(m, k, 0), yg), half)));
      spv_v g = spv_add(m, yb, spv_floor(m, spv_add(m, spv_add(m, spv_mul(m, spv_x(m, k, 1), yr),
                        spv_mul(m, spv_x(m, k, 2), yg)), half)));
      spv_v bl = spv_add(m, yb, spv_floor(m, spv_add(m, spv_mul(m, spv_x(m, k, 3), yr), half)));
      spv_store(m, out, spv_div(m, spv_vec4(m, r, g, bl, yb), spv_spread(m, spv_f(m, 255.f), 4)));
   }
   spv_endif(m, &b);
   if (spv_if(m, &b, flag(e, G64VK_CONVERT1_Y)))
      spv_store(m, out, splat4(m, spv_x(m, t0, 2)));
   spv_endif(m, &b);
   return spv_load(m, out);
}

static spv_v op_at(g64_emit *e, int i)
{
   if (e->key)
      return spv_i(e->m, e->key[i]);
   return spv_load_member_at(e->m, e->pc, SPV_PUSH_CONSTANT, PC_OPS, spv_i(e->m, i), T_I1);
}

/* a field of the blender's cycles in the flags: 0..3 */
static spv_v bl_field(g64_emit *e, int shift)
{
   spv_module *m = e->m;
   spv_v f;
   if (e->key)
      return spv_i(m, (int)(((uint32_t)e->key[16] >> shift) & 3u));
   f = spv_load_member(m, e->pc, SPV_PUSH_CONSTANT, PC_FLAGS, T_I1);
   return spv_band(m, spv_shr(m, f, spv_i(m, shift)), spv_i(m, 3));
}

/* 1.0 where @sel is @v, else 0.0 */
static spv_v is_(spv_module *m, spv_v sel, int v)
{
   return spv_bool_to_float(m, spv_eq(m, sel, spv_i(m, v)));
}

/* P or M: the pixel, the blend colour, the fog colour (never memory here) */
static spv_v bl_colour(spv_module *m, spv_v sel, spv_v in, spv_v blend, spv_v fog)
{
   spv_v c = spv_mix(m, in, blend, spv_spread(m, is_(m, sel, GLITCH64_BL_BLEND), 3));
   return spv_mix(m, c, fog, spv_spread(m, is_(m, sel, GLITCH64_BL_FOG), 3));
}

/* A: the pixel's alpha, the fog alpha, the shade alpha, 0 */
static spv_v bl_alpha(spv_module *m, spv_v sel, spv_v in, spv_v fog, spv_v shade)
{
   spv_v a = spv_mul(m, in, is_(m, sel, GLITCH64_BL_A_IN));
   a = spv_add(m, a, spv_mul(m, fog, is_(m, sel, GLITCH64_BL_A_FOG)));
   return spv_add(m, a, spv_mul(m, shade, is_(m, sel, GLITCH64_BL_A_SHADE)));
}

/* B: 1 - A, the memory alpha (full), 1, 0 */
static spv_v bl_b(spv_module *m, spv_v sel, spv_v a)
{
   spv_v one = spv_sub(m, spv_f(m, 1.f), is_(m, sel, GLITCH64_BL_B_ZERO));
   return spv_sub(m, one, spv_mul(m, a, is_(m, sel, GLITCH64_BL_B_1MA)));
}

/* a blender cycle on @rgb: with @whole, P*A + M*B; else the colour P
 * selects. Its A in @a. */
static spv_v bl_cycle(g64_emit *e, int shift, spv_c whole, spv_v rgb, spv_v in_a, spv_v fogc, spv_v blendc,
      spv_v shade_a, spv_var *a_out)
{
   spv_module *m = e->m;
   spv_var res = spv_local(m, T_F3);
   spv_v a = bl_alpha(m, bl_field(e, shift + 4), in_a, spv_x(m, fogc, 3), shade_a);
   spv_v p = bl_colour(m, bl_field(e, shift + 6), rgb, spv_xyz(m, blendc), spv_xyz(m, fogc));
   spv_if_t w;
   spv_store(m, *a_out, a);
   spv_store(m, res, p);
   if (spv_if(m, &w, whole))
   {
      spv_v mm = bl_colour(m, bl_field(e, shift + 2), rgb, spv_xyz(m, blendc), spv_xyz(m, fogc));
      spv_v b = bl_b(m, bl_field(e, shift), a);
      spv_store(m, res, spv_clamp(m, spv_add(m, spv_mul(m, p, spv_spread(m, a, 3)),
               spv_mul(m, mm, spv_spread(m, b, 3))), 0.f, 1.f));
   }
   spv_endif(m, &w);
   return spv_load(m, res);
}

const uint32_t *glitch64_vk_fragment(spv_module *m, int depth, const int *key, unsigned *count)
{
   g64_emit e;
   spv_v v_shade, v_tc0, v_tc1, v_xy, frag;
   spv_var color, depth_out;
   uint32_t tex0, tex1;

   e.m = m;
   e.key = depth ? NULL : key;
   spv_begin(m, SPV_STAGE_FRAGMENT);
   declare_values(&e);
   declare_push(&e);
   tex0 = declare_sampler(m, VKR_BIND_TEX0);

   if (depth)
   {
      /* the depth image's texel, as the depth it is: the CPU turned the
       * words into window depth */
      depth_out = spv_builtin_out(m, T_F1, SPV_BUILTIN_FRAGDEPTH);
      v_tc0 = spv_input(m, T_F2, 1);
      spv_mode(m, SPV_MODE_DEPTH_REPLACING);
      spv_begin_main(m);
      {
         uint32_t s = spv_load_sampler(m, tex0, spv_sampled_image(m, 0, 0));
         spv_v t = spv_to_int(m, spv_floor(m, load_in(m, v_tc0)));
         spv_store(m, depth_out, spv_x(m, spv_fetch(m, s, t), 0));
      }
      spv_end_main(m);
      return spv_finish(m, count);
   }

   tex1    = declare_sampler(m, VKR_BIND_TEX1);
   color   = spv_output(m, T_F4, 0);
   v_shade = spv_input(m, T_F4, 0);
   v_tc0   = spv_input(m, T_F2, 1);
   v_tc1   = spv_input(m, T_F2, 2);
   v_xy    = spv_input(m, T_F2, 4);
   frag    = spv_builtin_in(m, T_F4, SPV_BUILTIN_FRAGCOORD);
   spv_decorate(m, v_shade.id, SPV_DEC_NOPERSPECTIVE);

   spv_begin_main(m);
   {
      spv_var in = spv_local_array(m, T_F4, GLITCH64_IN_COUNT);
      spv_var out = spv_local(m, T_F4);
      spv_v tc0 = load_in(m, v_tc0), tc1 = load_in(m, v_tc1);
      spv_v exact = u(&e, U_EXACT_SIZES);
      spv_v testu = u(&e, U_TEST);
      spv_if_t b;

      if (spv_if(m, &b, flag(&e, G64VK_DITHER)))
      {
         spv_v p = load_in(m, v_xy);
         spv_v px = spv_x(m, p, 0), py = spv_x(m, p, 1);
         spv_v t = spv_mul(m, spv_abs(m, spv_sin(m, spv_add(m, py, spv_sin(m, spv_add(m, py, px))))), spv_f(m, 170.f));
         spv_v sum = spv_add(m, spv_add(m, spv_fract(m, t), spv_fract(m, spv_div(m, t, spv_f(m, 2.f)))),
               spv_fract(m, spv_div(m, t, spv_f(m, 4.f))));
         spv_if_t k;
         if (spv_if(m, &k, spv_cond(spv_gt(m, sum, spv_f(m, 1.5f)))))
            spv_kill(m);
         spv_endif(m, &k);
      }
      spv_endif(m, &b);

      if (spv_if(m, &b, flag(&e, G64VK_COPY)))
      {
         uint32_t s = spv_load_sampler(m, tex0, spv_sampled_image(m, 0, 0));
         spv_if_t k;
         spv_store(m, out, spv_sample(m, s, tc0));
         /* the alpha test: -1 is none */
         if (spv_if(m, &k, spv_cond(spv_le(m, spv_x(m, spv_load(m, out), 3), spv_x(m, testu, 3)))))
            spv_kill(m);
         spv_endif(m, &k);
      }
      if (spv_else(m, &b))
      {
         spv_v t0 = read_texel(&e, tex0, tc0, xy_(m, exact), G64VK_3POINT0);
         spv_v t1 = convert_texel(&e, t0, read_texel(&e, tex1, tc1, zw_(m, exact), G64VK_3POINT1));
         spv_v prim = u(&e, U_PRIM), env = u(&e, U_ENV), consts = u(&e, U_CONSTS);
         spv_v level = u(&e, U_LEVEL);
         spv_v shade = load_in(m, v_shade);
         spv_var lod = spv_local(m, T_F1);
         spv_var noise = spv_local(m, T_F1);
         spv_if_t l;
         int c;

         spv_store(m, lod, spv_x(m, consts, 3));
         if (spv_if(m, &l, flag(&e, G64VK_LOD_PIXEL)))
         {
            /* the RDP's LOD: the largest step of tile 0's texel
             * coordinates to the next pixel across or down */
            spv_v st = spv_mul(m, tc0, xy_(m, exact));
            spv_v dx = spv_abs(m, spv_dfdx(m, st)), dy = spv_abs(m, spv_dfdy(m, st));
            spv_v n = spv_max(m, spv_max(m, spv_x(m, dx, 0), spv_x(m, dx, 1)), spv_max(m, spv_x(m, dy, 0), spv_x(m, dy, 1)));
            spv_v detail = spv_gt(m, spv_x(m, level, 2), spv_f(m, 0.5f));
            spv_if_t mg;
            if (spv_if(m, &mg, spv_cond(spv_lt(m, n, spv_f(m, 1.f)))))
            {
               /* magnified: the fraction itself with detail or sharpen
                * on, else the last level's 1 or the first's 0 */
               spv_v last = spv_select(m, spv_lt(m, spv_x(m, level, 1), spv_f(m, 0.5f)), spv_f(m, 1.f), spv_f(m, 0.f));
               spv_store(m, lod, spv_select(m, detail, spv_max(m, n, spv_x(m, level, 0)), last));
            }
            if (spv_else(m, &mg))
            {
               spv_v tile = spv_floor(m, spv_log2(m, n));
               spv_v past = spv_ge(m, tile, spv_x(m, level, 1));
               spv_v frac = spv_sub(m, spv_div(m, n, spv_pow(m, spv_f(m, 2.f), tile)), spv_f(m, 1.f));
               spv_store(m, lod, frac);
               {
                  spv_if_t w;
                  if (spv_if(m, &w, spv_and(m, spv_cond(past), spv_not(m, spv_cond(detail)))))
                     spv_store(m, lod, spv_f(m, 1.f));
                  spv_endif(m, &w);
               }
            }
            spv_endif(m, &mg);
         }
         spv_endif(m, &l);

         spv_store(m, noise, spv_f(m, 0.f));
         if (spv_if(m, &l, flag(&e, G64VK_NOISE)))
         {
            spv_v fc = spv_add(m, spv_xy(m, load_in(m, frag)), spv_spread(m, spv_x(m, level, 3), 2));
            spv_v d = spv_dot(m, fc, spv_vec(m, T_F2, 12.9898f, 78.233f, 0, 0));
            spv_store(m, noise, spv_fract(m, spv_mul(m, spv_sin(m, d), spv_f(m, 43758.5453f))));
         }
         spv_endif(m, &l);

         {
            spv_v lf = spv_load(m, lod);
            spv_v center = u(&e, U_KEY_CENTER), scale = u(&e, U_KEY_SCALE);
            spv_store_at(m, in, GLITCH64_IN_COMBINED, spv_zero(m, T_F4));
            spv_store_at(m, in, GLITCH64_IN_TEXEL0, t0);
            spv_store_at(m, in, GLITCH64_IN_TEXEL1, t1);
            spv_store_at(m, in, GLITCH64_IN_PRIM, prim);
            spv_store_at(m, in, GLITCH64_IN_SHADE, shade);
            spv_store_at(m, in, GLITCH64_IN_ENV, env);
            spv_store_at(m, in, GLITCH64_IN_ONE, spv_vec(m, T_F4, 1.f, 1.f, 1.f, 1.f));
            spv_store_at(m, in, GLITCH64_IN_ZERO, spv_zero(m, T_F4));
            spv_store_at(m, in, GLITCH64_IN_NOISE, splat4(m, spv_load(m, noise)));
            spv_store_at(m, in, GLITCH64_IN_CENTER, spv_vec3_1(m, spv_xyz(m, center), spv_f(m, 0.f)));
            spv_store_at(m, in, GLITCH64_IN_SCALE, spv_vec3_1(m, spv_xyz(m, scale), spv_f(m, 0.f)));
            spv_store_at(m, in, GLITCH64_IN_K4, splat4(m, spv_x(m, consts, 0)));
            spv_store_at(m, in, GLITCH64_IN_K5, splat4(m, spv_x(m, consts, 1)));
            spv_store_at(m, in, GLITCH64_IN_COMBINED_A, spv_zero(m, T_F4));
            spv_store_at(m, in, GLITCH64_IN_TEXEL0_A, splat4(m, spv_x(m, t0, 3)));
            spv_store_at(m, in, GLITCH64_IN_TEXEL1_A, splat4(m, spv_x(m, t1, 3)));
            spv_store_at(m, in, GLITCH64_IN_PRIM_A, splat4(m, spv_x(m, prim, 3)));
            spv_store_at(m, in, GLITCH64_IN_SHADE_A, splat4(m, spv_x(m, shade, 3)));
            spv_store_at(m, in, GLITCH64_IN_ENV_A, splat4(m, spv_x(m, env, 3)));
            spv_store_at(m, in, GLITCH64_IN_LOD, splat4(m, lf));
            spv_store_at(m, in, GLITCH64_IN_PRIM_LOD, splat4(m, spv_x(m, consts, 2)));
         }

         /* the cycles: one-cycle mode runs the second cycle's settings */
         spv_store(m, out, spv_zero(m, T_F4));
         for (c = 0; c < 2; c++)
         {
            spv_if_t run;
            if (spv_if(m, &run, c == 0 ? flag(&e, G64VK_TWO_CYCLE) : spv_known(1)))
            {
               spv_v a = spv_load_at(m, in, op_at(&e, 8 * c + 0));
               spv_v bb = spv_load_at(m, in, op_at(&e, 8 * c + 1));
               spv_v cc = spv_load_at(m, in, op_at(&e, 8 * c + 2));
               spv_v d = spv_load_at(m, in, op_at(&e, 8 * c + 3));
               spv_v aa = spv_x(m, spv_load_at(m, in, op_at(&e, 8 * c + 4)), 3);
               spv_v ab = spv_x(m, spv_load_at(m, in, op_at(&e, 8 * c + 5)), 3);
               spv_v ac = spv_x(m, spv_load_at(m, in, op_at(&e, 8 * c + 6)), 3);
               spv_v ad = spv_x(m, spv_load_at(m, in, op_at(&e, 8 * c + 7)), 3);
               spv_v rgb = spv_add(m, spv_mul(m, spv_sub(m, spv_xyz(m, a), spv_xyz(m, bb)), spv_xyz(m, cc)), spv_xyz(m, d));
               spv_v al = spv_add(m, spv_mul(m, spv_sub(m, aa, ab), ac), ad);
               spv_v comb = spv_vec3_1(m, rgb, al);
               spv_store(m, out, comb);
               spv_store_at(m, in, GLITCH64_IN_COMBINED, comb);
               spv_store_at(m, in, GLITCH64_IN_COMBINED_A, splat4(m, al));
            }
            spv_endif(m, &run);
         }
         {
            spv_v fc = u(&e, U_FOG_COLOR), bc = u(&e, U_BLEND_COLOR);
            spv_var rgb = spv_local(m, T_F3), bl_a = spv_local(m, T_F1);
            spv_v comb, in_a, sh_a;
            spv_if_t k;
            spv_store(m, out, spv_clamp(m, spv_load(m, out), 0.f, 1.f));
            comb = spv_load(m, out);
            /* the alpha test, on the combiner's alpha, before the blender: -1 is none */
            if (spv_if(m, &k, spv_cond(spv_le(m, spv_x(m, comb, 3), spv_x(m, testu, 3)))))
               spv_kill(m);
            spv_endif(m, &k);
            in_a = spv_x(m, comb, 3);
            in_a = spv_add(m, in_a, spv_mul(m, spv_sub(m, spv_f(m, 1.f), in_a),
                     flag_f(&e, G64VK_CVG_ALPHA)));
            sh_a = spv_clamp(m, spv_x(m, shade, 3), 0.f, 1.f);
            spv_store(m, rgb, spv_xyz(m, comb));
            if (spv_if(m, &k, flag(&e, G64VK_BLEND0)))
               spv_store(m, rgb, bl_cycle(&e, G64VK_BLEND0_SHIFT, spv_known(1), spv_load(m, rgb), in_a, fc, bc,
                        sh_a, &bl_a));
            spv_endif(m, &k);
            spv_store(m, rgb, bl_cycle(&e, G64VK_BLEND1_SHIFT, flag(&e, G64VK_BLEND1), spv_load(m, rgb), in_a, fc, bc,
                     sh_a, &bl_a));
            spv_store(m, out, spv_vec3_1(m, spv_load(m, rgb), spv_load(m, bl_a)));
         }
      }
      spv_endif(m, &b);
      spv_store(m, color, spv_load(m, out));
   }
   spv_end_main(m);
   return spv_finish(m, count);
}

const uint32_t *glitch64_vk_write_own(spv_module *m, const int *key, unsigned *count)
{
   return glitch64_vk_fragment(m, 0, key, count);
}
