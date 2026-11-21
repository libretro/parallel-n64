#include <string.h>

#include "glitch64_n64_combiner.h"

/* the texels a cycle reads: the second cycle of two swaps them */
static const char *texel(int second, int which)
{
   return (which ^ second) ? "n64T1" : "n64T0";
}

static const char *rgb_input(int second, int slot, unsigned v, char *buf)
{
   switch (slot)
   {
      case 0: /* sub a */
         switch (v)
         {
            case 0: return "n64C.rgb";
            case 1: case 2: strcpy(buf, texel(second, v - 1)); strcat(buf, ".rgb"); return buf;
            case 3: return "n64Prim.rgb";
            case 4: return "vFrontColor.rgb";
            case 5: return "n64Env.rgb";
            case 6: return "vec3(1.0)";
            case 7: return "vec3(n64Noise)";
         }
         return "vec3(0.0)";
      case 1: /* sub b */
         switch (v)
         {
            case 0: return "n64C.rgb";
            case 1: case 2: strcpy(buf, texel(second, v - 1)); strcat(buf, ".rgb"); return buf;
            case 3: return "n64Prim.rgb";
            case 4: return "vFrontColor.rgb";
            case 5: return "n64Env.rgb";
            case 6: return "n64KeyCenter";
            case 7: return "vec3(n64Const.x)";
         }
         return "vec3(0.0)";
      case 2: /* mul */
         switch (v)
         {
            case 0: return "n64C.rgb";
            case 1: case 2: strcpy(buf, texel(second, v - 1)); strcat(buf, ".rgb"); return buf;
            case 3: return "n64Prim.rgb";
            case 4: return "vFrontColor.rgb";
            case 5: return "n64Env.rgb";
            case 6: return "n64KeyScale";
            case 7: return "vec3(n64C.a)";
            case 8: case 9: strcpy(buf, "vec3("); strcat(buf, texel(second, v - 8)); strcat(buf, ".a)"); return buf;
            case 10: return "vec3(n64Prim.a)";
            case 11: return "vec3(vFrontColor.a)";
            case 12: return "vec3(n64Env.a)";
            case 13: return "vec3(n64LodFrac)";
            case 14: return "vec3(n64Const.z)";
            case 15: return "vec3(n64Const.y)";
         }
         return "vec3(0.0)";
      default: /* add */
         switch (v)
         {
            case 0: return "n64C.rgb";
            case 1: case 2: strcpy(buf, texel(second, v - 1)); strcat(buf, ".rgb"); return buf;
            case 3: return "n64Prim.rgb";
            case 4: return "vFrontColor.rgb";
            case 5: return "n64Env.rgb";
            case 6: return "vec3(1.0)";
         }
         return "vec3(0.0)";
   }
}

static const char *alpha_input(int second, int slot, unsigned v, char *buf)
{
   if (slot == 2) /* mul */
   {
      switch (v)
      {
         case 0: return "n64LodFrac";
         case 1: case 2: strcpy(buf, texel(second, v - 1)); strcat(buf, ".a"); return buf;
         case 3: return "n64Prim.a";
         case 4: return "vFrontColor.a";
         case 5: return "n64Env.a";
         case 6: return "n64Const.z";
      }
      return "0.0";
   }
   switch (v)
   {
      case 0: return "n64C.a";
      case 1: case 2: strcpy(buf, texel(second, v - 1)); strcat(buf, ".a"); return buf;
      case 3: return "n64Prim.a";
      case 4: return "vFrontColor.a";
      case 5: return "n64Env.a";
      case 6: return "1.0";
   }
   return "0.0";
}

void glitch64_n64_key(const glitch64_n64_combiner *c, uint32_t key[GLITCH64_N64_KEY_WORDS])
{
   unsigned i;
   key[0] = 0;
   key[1] = 0;
   for (i = 0; i < 2; i++)
   {
      key[0] |= (uint32_t)((c->rgb[i][0] & 15) | ((c->rgb[i][1] & 15) << 4)
            | ((c->rgb[i][2] & 31) << 8) | ((c->rgb[i][3] & 7) << 13)) << (16 * i);
      key[1] |= (uint32_t)((c->alpha[i][0] & 7) | ((c->alpha[i][1] & 7) << 3)
            | ((c->alpha[i][2] & 7) << 6) | ((c->alpha[i][3] & 7) << 9)) << (12 * i);
   }
   key[1] |= (uint32_t)(c->two_cycle ? 1 : 0) << 24;
   /* a key is never 0: 0 is no N64 combiner */
   key[1] |= 1u << 31;
   key[2] = (uint32_t)c->flags | ((uint32_t)c->blend[0] << 8) | ((uint32_t)c->blend[1] << 16);
}

void glitch64_n64_from_key(const uint32_t key[GLITCH64_N64_KEY_WORDS], glitch64_n64_combiner *c)
{
   unsigned i;
   for (i = 0; i < 2; i++)
   {
      const uint32_t r = key[0] >> (16 * i), a = key[1] >> (12 * i);
      c->rgb[i][0] = (uint8_t)(r & 15);
      c->rgb[i][1] = (uint8_t)((r >> 4) & 15);
      c->rgb[i][2] = (uint8_t)((r >> 8) & 31);
      c->rgb[i][3] = (uint8_t)((r >> 13) & 7);
      c->alpha[i][0] = (uint8_t)(a & 7);
      c->alpha[i][1] = (uint8_t)((a >> 3) & 7);
      c->alpha[i][2] = (uint8_t)((a >> 6) & 7);
      c->alpha[i][3] = (uint8_t)((a >> 9) & 7);
   }
   c->two_cycle = (uint8_t)((key[1] >> 24) & 1);
   c->flags = (uint8_t)(key[2] & 0xff);
   c->blend[0] = (uint8_t)((key[2] >> 8) & 0xff);
   c->blend[1] = (uint8_t)((key[2] >> 16) & 0xff);
}

static int texel_in(int second, unsigned which, int alpha)
{
   if ((which ^ (unsigned)second) & 1)
      return alpha ? GLITCH64_IN_TEXEL1_A : GLITCH64_IN_TEXEL1;
   return alpha ? GLITCH64_IN_TEXEL0_A : GLITCH64_IN_TEXEL0;
}

static int rgb_in(int second, int slot, unsigned v)
{
   if (v <= 5)
   {
      static const int common[6] = { GLITCH64_IN_COMBINED, -1, -1, GLITCH64_IN_PRIM, GLITCH64_IN_SHADE, GLITCH64_IN_ENV };
      if (v == 1 || v == 2)
         return texel_in(second, v - 1, 0);
      return common[v];
   }
   switch (slot)
   {
      case 0:
         return v == 6 ? GLITCH64_IN_ONE : v == 7 ? GLITCH64_IN_NOISE : GLITCH64_IN_ZERO;
      case 1:
         return v == 6 ? GLITCH64_IN_CENTER : v == 7 ? GLITCH64_IN_K4 : GLITCH64_IN_ZERO;
      case 2:
         switch (v)
         {
            case 6:  return GLITCH64_IN_SCALE;
            case 7:  return GLITCH64_IN_COMBINED_A;
            case 8:  case 9: return texel_in(second, v - 8, 1);
            case 10: return GLITCH64_IN_PRIM_A;
            case 11: return GLITCH64_IN_SHADE_A;
            case 12: return GLITCH64_IN_ENV_A;
            case 13: return GLITCH64_IN_LOD;
            case 14: return GLITCH64_IN_PRIM_LOD;
            case 15: return GLITCH64_IN_K5;
         }
         return GLITCH64_IN_ZERO;
      default:
         return v == 6 ? GLITCH64_IN_ONE : GLITCH64_IN_ZERO;
   }
}

/* the alpha's: the shader takes .w, where the colour inputs have it */
static int alpha_in(int second, int slot, unsigned v)
{
   if (slot == 2)
   {
      switch (v)
      {
         case 0: return GLITCH64_IN_LOD;
         case 1: case 2: return texel_in(second, v - 1, 0);
         case 3: return GLITCH64_IN_PRIM;
         case 4: return GLITCH64_IN_SHADE;
         case 5: return GLITCH64_IN_ENV;
         case 6: return GLITCH64_IN_PRIM_LOD;
      }
      return GLITCH64_IN_ZERO;
   }
   switch (v)
   {
      case 0: return GLITCH64_IN_COMBINED;
      case 1: case 2: return texel_in(second, v - 1, 0);
      case 3: return GLITCH64_IN_PRIM;
      case 4: return GLITCH64_IN_SHADE;
      case 5: return GLITCH64_IN_ENV;
      case 6: return GLITCH64_IN_ONE;
   }
   return GLITCH64_IN_ZERO;
}

void glitch64_n64_inputs(const glitch64_n64_combiner *c, int ops[16])
{
   int i, k;
   for (i = 0; i < 2; i++)
   {
      const int second = c->two_cycle && i == 1;
      for (k = 0; k < 4; k++)
      {
         ops[8 * i + k] = rgb_in(second, k, c->rgb[i][k]);
         ops[8 * i + 4 + k] = alpha_in(second, k, c->alpha[i][k]);
      }
   }
}

/* appends @s; nonzero while it fits */
static int put(char *out, size_t cap, size_t *len, const char *s)
{
   const size_t n = strlen(s);
   if (*len + n + 1 > cap)
      return 0;
   memcpy(out + *len, s, n + 1);
   *len += n;
   return 1;
}

static int uses_lod(const glitch64_n64_combiner *c, unsigned i)
{
   return c->rgb[i][2] == 13 || c->alpha[i][2] == 0;
}

static int uses_noise(const glitch64_n64_combiner *c, unsigned i)
{
   return c->rgb[i][0] == 7;
}

static int cycle(const glitch64_n64_combiner *c, unsigned i, int second, char *out, size_t cap, size_t *len)
{
   char b[4][24];
   int ok = 1;
   unsigned k;
   const char *in[4];

   for (k = 0; k < 4; k++)
      in[k] = rgb_input(second, (int)k, c->rgb[i][k], b[k]);
   ok &= put(out, cap, len, "  n64C = vec4((");
   ok &= put(out, cap, len, in[0]);
   ok &= put(out, cap, len, " - ");
   ok &= put(out, cap, len, in[1]);
   ok &= put(out, cap, len, ") * ");
   ok &= put(out, cap, len, in[2]);
   ok &= put(out, cap, len, " + ");
   ok &= put(out, cap, len, in[3]);
   ok &= put(out, cap, len, ", (");
   for (k = 0; k < 4; k++)
      in[k] = alpha_input(second, (int)k, c->alpha[i][k], b[k]);
   ok &= put(out, cap, len, in[0]);
   ok &= put(out, cap, len, " - ");
   ok &= put(out, cap, len, in[1]);
   ok &= put(out, cap, len, ") * ");
   ok &= put(out, cap, len, in[2]);
   ok &= put(out, cap, len, " + ");
   ok &= put(out, cap, len, in[3]);
   ok &= put(out, cap, len, "); \n");
   return ok;
}

/* the blender's colour P or M selects (never the memory colour here) */
static const char *bl_colour(unsigned s)
{
   switch (s)
   {
      case GLITCH64_BL_BLEND: return "n64Blend";
      case GLITCH64_BL_FOG:   return "fogColor";
   }
   return "n64C.rgb";
}

/* the alpha A selects */
static const char *bl_alpha(const glitch64_n64_combiner *c, unsigned s)
{
   switch (s)
   {
      case GLITCH64_BL_A_IN:    return (c->flags & GLITCH64_N64_CVG_ALPHA) ? "1.0" : "n64C.a";
      case GLITCH64_BL_A_FOG:   return "fogAlpha";
      case GLITCH64_BL_A_SHADE: return "clamp(vFrontColor.a, 0.0, 1.0)";
   }
   return "0.0";
}

/* A blender cycle: with @whole, P*A + M*B, the pixel's colour then;
 * else the colour P selects and the alpha A selects, gl_FragColor, for
 * the GPU to blend with memory. The last one sets gl_FragColor. */
static int blend_cycle(const glitch64_n64_combiner *c, unsigned x, int whole, char *out, size_t cap, size_t *len)
{
   int ok = 1;
   const char *a = bl_alpha(c, GLITCH64_BL_A(x));
   ok &= put(out, cap, len, "  { float n64BlA = ");
   ok &= put(out, cap, len, a);
   ok &= put(out, cap, len, "; \n");
   if (whole)
   {
      static const char *const b[4] = { "(1.0 - n64BlA)", "1.0", "1.0", "0.0" };
      ok &= put(out, cap, len, "    n64C.rgb = clamp(");
      ok &= put(out, cap, len, bl_colour(GLITCH64_BL_P(x)));
      ok &= put(out, cap, len, " * n64BlA + ");
      ok &= put(out, cap, len, bl_colour(GLITCH64_BL_M(x)));
      ok &= put(out, cap, len, " * ");
      ok &= put(out, cap, len, b[GLITCH64_BL_B(x)]);
      ok &= put(out, cap, len, ", 0.0, 1.0); \n");
   }
   else
   {
      ok &= put(out, cap, len, "    n64C.rgb = ");
      ok &= put(out, cap, len, bl_colour(GLITCH64_BL_P(x)));
      ok &= put(out, cap, len, "; \n");
   }
   ok &= put(out, cap, len, "    gl_FragColor = vec4(n64C.rgb, n64BlA); } \n");
   return ok;
}

size_t glitch64_n64_fragment(const glitch64_n64_combiner *c, char *out, size_t cap)
{
   size_t len = strlen(out);
   const size_t start = len;
   /* the cycles run: one-cycle runs the second cycle's settings */
   const unsigned first = c->two_cycle ? 0 : 1;
   unsigned i;
   int ok = 1, lod = 0, noise = 0;

   for (i = first; i < 2; i++)
   {
      lod |= uses_lod(c, i);
      noise |= uses_noise(c, i);
   }

   /* COMBINED in the first cycle run is the previous pixel's result on
    * the RDP: nothing a shader has, so 0 */
   ok &= put(out, cap, &len, "  vec4 n64T0 = readtex1; \n  vec4 n64T1 = readtex0; \n  vec4 n64C = vec4(0.0); \n");
   if (c->flags & GLITCH64_N64_CONVERT1_Y)
      ok &= put(out, cap, &len, "  n64T1 = vec4(n64T0.b); \n");
   else if (c->flags & GLITCH64_N64_CONVERT1)
      ok &= put(out, cap, &len, GLITCH64_N64_CONVERT_GLSL);
   if (lod)
   {
      if (c->flags & GLITCH64_N64_LOD)
         ok &= put(out, cap, &len,
               /* the RDP's LOD: the largest step of tile 0's texel
                * coordinates to the next pixel across or down */
               "  vec2 n64St = vTexCoord[1].xy * exactSizes.zw; \n"
               "  vec2 n64Dx = abs(dFdx(n64St)), n64Dy = abs(dFdy(n64St)); \n"
               "  float n64Lod = max(max(n64Dx.x, n64Dx.y), max(n64Dy.x, n64Dy.y)); \n"
               "  float n64LodFrac; \n"
               "  if (n64Lod < 1.0) \n"
               /* magnified: the fraction itself with detail or sharpen
                * on, else the last level's 1 or the first's 0 */
               "    n64LodFrac = n64Level.z > 0.5 ? max(n64Lod, n64Level.x) : (n64Level.y < 0.5 ? 1.0 : 0.0); \n"
               "  else { \n"
               "    float n64Tile = floor(log2(n64Lod)); \n"
               /* past the last level, the last one whole */
               "    n64LodFrac = (n64Tile >= n64Level.y && n64Level.z < 0.5) ? 1.0 : n64Lod / exp2(n64Tile) - 1.0; \n"
               "  } \n");
      else
         ok &= put(out, cap, &len, "  float n64LodFrac = n64Const.w; \n");
   }
   if (noise)
      ok &= put(out, cap, &len,
            "  float n64Noise = fract(sin(dot(gl_FragCoord.xy + n64Level.w, vec2(12.9898, 78.233))) * 43758.5453); \n");

   for (i = first; i < 2; i++)
      ok &= cycle(c, i, c->two_cycle && i == 1, out, cap, &len);

   /* the alpha test, on the combiner's alpha, before the blender */
   ok &= put(out, cap, &len, "  n64C = clamp(n64C, 0.0, 1.0); \n  if (n64C.a <= alphaRef) discard; \n");
   if (c->flags & GLITCH64_N64_BLEND0)
      ok &= blend_cycle(c, c->blend[0], 1, out, cap, &len);
   ok &= blend_cycle(c, c->blend[1], (c->flags & GLITCH64_N64_BLEND1) != 0, out, cap, &len);

   if (!ok)
   {
      out[start] = '\0';
      return 0;
   }
   return len - start;
}
