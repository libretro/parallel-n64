/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else // _WIN32
#include <string.h>
#include <stdlib.h>
#endif // _WIN32
#include <math.h>

#include <streams/file_stream.h>
#include <encodings/crc32.h>
#include "../../../mupen64plus-video-gliden64/src/CombinerKeyPath.h"
#include "../../../mupen64plus-core/src/plugin/plugin.h"
#include "glide.h"
#include "glitchmain.h"
#include "glitch64_vk.h"
#include "../../libretro/libretro_private.h"

#include "../../Graphics/RDP/RDP_state.h"

float glide64_pow(float a, float b);

typedef struct _shader_program_key
{
   int index;

   int dither_enabled;
   int three_point_filter0;
   int three_point_filter1;
   /* the N64 combiner (glitch64_n64_key), or 0 for the Glide one */
   uint32_t n64_key0;
   uint32_t n64_key1;
   uint32_t n64_key2;
   GLuint program_object;
   int texture0_location;
   int texture1_location;
   int vertexOffset_location;
   int textureSizes_location;
   int exactSizes_location;
   int fogColor_location;
   int alphaRef_location;

   int n64Prim_location;
   int n64Env_location;
   int n64Const_location;
   int n64Level_location;
   int n64KeyCenter_location;
   int n64KeyScale_location;
   int fogAlpha_location;
   int n64Blend_location;
   /* the shared program's own: the inputs and options it reads */
   int n64Ops_location;
   int n64Flags0_location;
   int n64Flags1_location;
   int n64Flags2_location;
   int n64Blend0_location;
   int n64Blend1_location;
   /* its program still linking (1), or left for the next session (2):
    * the shared program draws for it until then */
   int pending;
} shader_program_key;

static int alpha_ref, alpha_func;
bool alpha_test = 0;

static shader_program_key *shader_programs = NULL;
static shader_program_key *current_shader  = NULL;
/* The shared program: every combiner and option read from uniforms, built
 * at load. A combiner's own program is never compiled on the draw path:
 * with parallel shader compiles one is started and the shared program
 * draws until it has linked; without, the key is kept for the next
 * session to build at load (AGENTS.md). */
static shader_program_key uber_program;
static int parallel_compile;
/* the core option: the shared program built and drawing while combiners'
 * own programs are not ready; off, every combiner's own program is
 * compiled when met, as ever. Read at the window's opening. */
int glitch64_shared_shader = 0;
/* 1: everything through the shared program (tools: GLITCH64_COMBINER=uber,
 * with the option on); 2: the option off */
static int combiner_mode;

static int number_of_programs = 0;

static int dither_enabled;

/* the N64 combiner the next draws run, and its
 * inputs: prim, env, K4 K5 PRIM_LOD_FRAC LOD_FRACTION, the levels */
static uint32_t n64_key[GLITCH64_N64_KEY_WORDS];
static float n64_values[4][4];
static float n64_key_values[2][3];
/* the fog colour the uniforms have */
static uint32_t n64_fog_rgba;
static uint32_t n64_blend_rgba;
/* dFdx and dFdy, for LOD_FRACTION per pixel */
static int n64_derivatives;


// shaders variables
int need_to_compile;

static char *fragment_shader;
static GLuint vertex_shader_object;
GLuint program_object_default;

static void init_depth_shader(void);
static void n64_upload(const shader_program_key *prog);

#if defined(HAVE_OPENGLES) // Desktop GL fix
#define GLSL_VERSION "100"
#else
#define GLSL_VERSION "120"
#endif

#define SHADER_HEADER \
"#version " GLSL_VERSION          "\n"

/* The shade, and the clip W it is multiplied by: the RDP steps the shade
 * evenly across the screen, so the GPU's perspective correction is undone
 * (vFrontColor, below). */
#define SHADER_VARYING \
"varying highp vec4 vFrontColorW; \n" \
"varying highp float vColorW;     \n" \
"varying highp vec4 vTexCoord[4]; \n"

static const char* fragment_shader_header =
SHADER_HEADER
#if defined(HAVE_OPENGLES) // Desktop GL fix
"precision lowp float;          \n"
#else
"#define highp                  \n"
#endif
"uniform sampler2D texture0;    \n"
"uniform sampler2D texture1;    \n"
"uniform vec4 exactSizes;     \n"  //textureSizes doesn't contain the correct sizes, use this one instead for offset calculations
"uniform vec3 fogColor;         \n"
"uniform float alphaRef;        \n"
"uniform vec4 n64Prim;          \n"
"uniform vec4 n64Env;           \n"
"uniform vec4 n64Const;         \n"
"uniform vec4 n64Level;         \n"
"uniform vec3 n64KeyCenter;     \n"
"uniform vec3 n64KeyScale;      \n"
"uniform float fogAlpha;        \n"
"uniform vec3 n64Blend;         \n"
"#define TEX0             texture2D(texture0, vTexCoord[0].xy) \n" \
"#define TEX0_OFFSET(off) texture2D(texture0, vTexCoord[0].xy - off/exactSizes.xy) \n" \
"#define TEX1             texture2D(texture1, vTexCoord[1].xy) \n" \
"#define TEX1_OFFSET(off) texture2D(texture1, vTexCoord[1].xy - off/exactSizes.zw) \n" \



SHADER_VARYING
"#define vFrontColor (vFrontColorW / vColorW) \n"
"\n"
"\n"
"\n"
"void main()\n"
"{\n"
"  vec2 offset; \n"
"  vec4 c0,c1,c2; \n"
;

// using gl_FragCoord is terribly slow on ATI and varying variables don't work for some unknown
// reason, so we use the unused components of the texture2 coordinates
static const char* fragment_shader_dither =
"  highp float temp=abs(sin((vTexCoord[2].a)+sin((vTexCoord[2].a)+(vTexCoord[2].b))))*170.0; \n"
"  if ((fract(temp)+fract(temp/2.0)+fract(temp/4.0))>1.5) discard; \n"
;

static const char* fragment_shader_default =
"  gl_FragColor = TEX0; \n"
;
static const char* fragment_shader_readtex0color =
"  vec4 readtex0 = TEX0; \n"
;
static const char* fragment_shader_readtex0color_3point =
"  offset=fract(vTexCoord[0].xy*exactSizes.xy-vec2(0.5,0.5)); \n"
"  offset-=step(1.0,offset.x+offset.y); \n"
"  c0=TEX0_OFFSET(offset); \n"
"  c1=TEX0_OFFSET(vec2(offset.x-sign(offset.x),offset.y)); \n"
"  c2=TEX0_OFFSET(vec2(offset.x,offset.y-sign(offset.y))); \n"
"  vec4 readtex0 =c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); \n"
;

static const char* fragment_shader_readtex1color =
"  vec4 readtex1 = TEX1; \n"
;

static const char* fragment_shader_readtex1color_3point =
"  offset=fract(vTexCoord[1].xy*exactSizes.zw-vec2(0.5,0.5)); \n"
"  offset-=step(1.0,offset.x+offset.y); \n"
"  c0=TEX1_OFFSET(offset); \n"
"  c1=TEX1_OFFSET(vec2(offset.x-sign(offset.x),offset.y)); \n"
"  c2=TEX1_OFFSET(vec2(offset.x,offset.y-sign(offset.y))); \n"
"  vec4 readtex1 =c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); \n";


static const char* fragment_shader_end =
"if(gl_FragColor.a <= alphaRef) {discard;}   \n"
"}\n"
;

/* an N64 program's: its alpha test is on the combiner's alpha, before the
 * blender (glitch64_n64_fragment) */
static const char* fragment_shader_close =
"}\n"
;

/* The shared program's: glitch64_n64_inputs' indices read through
 * n64In, the options from n64Flags0 (two cycles, the blender's first
 * cycle run, its second run whole, per-pixel LOD), n64Flags1 (unused,
 * dither, the 3-point filter on unit 0 and on unit 1) and n64Flags2
 * (noise read, the pixel's alpha into the blender its full coverage);
 * the blender's cycles as P, A, M, B in n64Blend0 and n64Blend1. Goes
 * between the header's declarations and main(). */
static const char *uber_inputs =
"uniform int n64Ops[16];        \n"
"uniform vec4 n64Flags0;        \n"
"uniform vec4 n64Flags1;        \n"
"uniform vec4 n64Flags2;        \n"
"uniform vec4 n64Blend0;        \n"
"uniform vec4 n64Blend1;        \n"
"vec3 n64BlC(float s, vec3 C) { return s < 1.5 ? C : (s < 2.5 ? n64Blend : fogColor); } \n"
"float n64BlAl(float s, float a, float sh) \n"
"{ return s < 0.5 ? a : (s < 1.5 ? fogAlpha : (s < 2.5 ? clamp(sh, 0.0, 1.0) : 0.0)); } \n"
"float n64BlB(float s, float a) { return s < 0.5 ? 1.0 - a : (s < 2.5 ? 1.0 : 0.0); } \n"
"vec4 n64In(int i, vec4 C, vec4 T0, vec4 T1, vec4 sh, float lod, float noise) \n"
"{ \n"
"  if (i < 8) { \n"
"    if (i == 0) return C; if (i == 1) return T0; if (i == 2) return T1; \n"
"    if (i == 3) return n64Prim; if (i == 4) return sh; if (i == 5) return n64Env; \n"
"    if (i == 6) return vec4(1.0); return vec4(0.0); \n"
"  } \n"
"  if (i < 13) { \n"
"    if (i == 8) return vec4(noise); if (i == 9) return vec4(n64KeyCenter, 0.0); \n"
"    if (i == 10) return vec4(n64KeyScale, 0.0); if (i == 11) return vec4(n64Const.x); \n"
"    return vec4(n64Const.y); \n"
"  } \n"
"  if (i == 13) return vec4(C.a); if (i == 14) return vec4(T0.a); if (i == 15) return vec4(T1.a); \n"
"  if (i == 16) return vec4(n64Prim.a); if (i == 17) return vec4(sh.a); if (i == 18) return vec4(n64Env.a); \n"
"  if (i == 19) return vec4(lod); return vec4(n64Const.z); \n"
"} \n"
;

#if defined(HAVE_OPENGLES2)
/* GLSL ES 1.00 need not index a fragment shader's arrays by a uniform: the
 * inputs are picked by n64In */
#define UBER_INPUTS ""
#define UBER_CYCLE(k) \
"    { vec4 a = n64In(n64Ops[" #k " * 8 + 0], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 b = n64In(n64Ops[" #k " * 8 + 1], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 c = n64In(n64Ops[" #k " * 8 + 2], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 d = n64In(n64Ops[" #k " * 8 + 3], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 aa = n64In(n64Ops[" #k " * 8 + 4], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 ab = n64In(n64Ops[" #k " * 8 + 5], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 ac = n64In(n64Ops[" #k " * 8 + 6], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      vec4 ad = n64In(n64Ops[" #k " * 8 + 7], n64C, n64T0, n64T1, sh, n64LodFrac, n64Noise); \n" \
"      n64C = vec4((a.rgb - b.rgb) * c.rgb + d.rgb, (aa.a - ab.a) * ac.a + ad.a); } \n"
#else
/* the inputs in an array, indexed: far less code than picking each of the
 * sixteen through n64In, which a driver building a shader's code at its
 * first draw takes a long time over */
#define UBER_INPUTS \
"  vec4 n64Inp[21]; \n" \
"  n64Inp[1] = n64T0; n64Inp[2] = n64T1; n64Inp[3] = n64Prim; n64Inp[4] = sh; \n" \
"  n64Inp[5] = n64Env; n64Inp[6] = vec4(1.0); n64Inp[7] = vec4(0.0); n64Inp[8] = vec4(n64Noise); \n" \
"  n64Inp[9] = vec4(n64KeyCenter, 0.0); n64Inp[10] = vec4(n64KeyScale, 0.0); \n" \
"  n64Inp[11] = vec4(n64Const.x); n64Inp[12] = vec4(n64Const.y); n64Inp[14] = vec4(n64T0.a); \n" \
"  n64Inp[15] = vec4(n64T1.a); n64Inp[16] = vec4(n64Prim.a); n64Inp[17] = vec4(sh.a); \n" \
"  n64Inp[18] = vec4(n64Env.a); n64Inp[19] = vec4(n64LodFrac); n64Inp[20] = vec4(n64Const.z); \n"
#define UBER_CYCLE(k) \
"    { n64Inp[0] = n64C; n64Inp[13] = vec4(n64C.a); \n" \
"      vec4 a = n64Inp[n64Ops[" #k " * 8 + 0]]; vec4 b = n64Inp[n64Ops[" #k " * 8 + 1]]; \n" \
"      vec4 c = n64Inp[n64Ops[" #k " * 8 + 2]]; vec4 d = n64Inp[n64Ops[" #k " * 8 + 3]]; \n" \
"      vec4 aa = n64Inp[n64Ops[" #k " * 8 + 4]]; vec4 ab = n64Inp[n64Ops[" #k " * 8 + 5]]; \n" \
"      vec4 ac = n64Inp[n64Ops[" #k " * 8 + 6]]; vec4 ad = n64Inp[n64Ops[" #k " * 8 + 7]]; \n" \
"      n64C = vec4((a.rgb - b.rgb) * c.rgb + d.rgb, (aa.a - ab.a) * ac.a + ad.a); } \n"
#endif

/* main()'s body, after the header's own first lines */
static const char *uber_body_a =
"  if (n64Flags1.y > 0.5) { \n"
"    highp float temp=abs(sin((vTexCoord[2].a)+sin((vTexCoord[2].a)+(vTexCoord[2].b))))*170.0; \n"
"    if ((fract(temp)+fract(temp/2.0)+fract(temp/4.0))>1.5) discard; \n"
"  } \n"
"  vec4 readtex0; \n"
"  vec4 readtex1; \n"
#ifndef DISABLE_3POINT
"  if (n64Flags1.z > 0.5) { \n"
"    offset=fract(vTexCoord[0].xy*exactSizes.xy-vec2(0.5,0.5)); \n"
"    offset-=step(1.0,offset.x+offset.y); \n"
"    c0=TEX0_OFFSET(offset); \n"
"    c1=TEX0_OFFSET(vec2(offset.x-sign(offset.x),offset.y)); \n"
"    c2=TEX0_OFFSET(vec2(offset.x,offset.y-sign(offset.y))); \n"
"    readtex0 =c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); \n"
"  } else \n"
#endif
"    readtex0 = TEX0; \n"
#ifndef DISABLE_3POINT
"  if (n64Flags1.w > 0.5) { \n"
"    offset=fract(vTexCoord[1].xy*exactSizes.zw-vec2(0.5,0.5)); \n"
"    offset-=step(1.0,offset.x+offset.y); \n"
"    c0=TEX1_OFFSET(offset); \n"
"    c1=TEX1_OFFSET(vec2(offset.x-sign(offset.x),offset.y)); \n"
"    c2=TEX1_OFFSET(vec2(offset.x,offset.y-sign(offset.y))); \n"
"    readtex1 =c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); \n"
"  } else \n"
#endif
"    readtex1 = TEX1; \n"
"  vec4 n64T0 = readtex1; \n"
"  vec4 n64T1 = readtex0; \n"
"  vec4 sh = vFrontColor; \n"
"  float n64LodFrac = n64Const.w; \n"
;

/* and the rest, after the LOD from the derivatives where the shader has them */
static const char *uber_body_b =
"  float n64Noise = 0.0; \n"
"  if (n64Flags2.x > 0.5) \n"
"    n64Noise = fract(sin(dot(gl_FragCoord.xy + n64Level.w, vec2(12.9898, 78.233))) * 43758.5453); \n"
"  vec4 n64C = vec4(0.0); \n"
UBER_INPUTS
"  if (n64Flags0.x > 0.5) \n"
UBER_CYCLE(0)
UBER_CYCLE(1)
"  n64C = clamp(n64C, 0.0, 1.0); \n"
"  if (n64C.a <= alphaRef) discard; \n"
"  float n64InA = n64Flags2.y > 0.5 ? 1.0 : n64C.a; \n"
"  if (n64Flags0.y > 0.5) { \n"
"    float a = n64BlAl(n64Blend0.y, n64InA, sh.a); \n"
"    n64C.rgb = clamp(n64BlC(n64Blend0.x, n64C.rgb) * a + n64BlC(n64Blend0.z, n64C.rgb) * n64BlB(n64Blend0.w, a), 0.0, 1.0); \n"
"  } \n"
"  float n64A = n64BlAl(n64Blend1.y, n64InA, sh.a); \n"
"  if (n64Flags0.z > 0.5) \n"
"    n64C.rgb = clamp(n64BlC(n64Blend1.x, n64C.rgb) * n64A + n64BlC(n64Blend1.z, n64C.rgb) * n64BlB(n64Blend1.w, n64A), 0.0, 1.0); \n"
"  else \n"
"    n64C.rgb = n64BlC(n64Blend1.x, n64C.rgb); \n"
"  gl_FragColor = vec4(n64C.rgb, n64A); \n"
;

static const char *uber_lod =
"  if (n64Flags0.w > 0.5) { \n"
"    vec2 n64St = vTexCoord[1].xy * exactSizes.zw; \n"
"    vec2 n64Dx = abs(dFdx(n64St)), n64Dy = abs(dFdy(n64St)); \n"
"    float n64Lod = max(max(n64Dx.x, n64Dx.y), max(n64Dy.x, n64Dy.y)); \n"
"    if (n64Lod < 1.0) \n"
"      n64LodFrac = n64Level.z > 0.5 ? max(n64Lod, n64Level.x) : (n64Level.y < 0.5 ? 1.0 : 0.0); \n"
"    else { \n"
"      float n64Tile = floor(log2(n64Lod)); \n"
"      n64LodFrac = (n64Tile >= n64Level.y && n64Level.z < 0.5) ? 1.0 : n64Lod / exp2(n64Tile) - 1.0; \n"
"    } \n"
"  } \n"
;

static const char* vertex_shader =
SHADER_HEADER
#if !defined(HAVE_OPENGLES) // Desktop GL fix
"#define highp                         \n"
#endif
"#define Z_MAX 65536.0                 \n"
"attribute highp vec4 aPosition;         \n"
"attribute highp vec4 aColor;          \n"
"attribute highp vec4 aMultiTexCoord0; \n"
"attribute highp vec4 aMultiTexCoord1; \n"
"uniform vec3 vertexOffset;            \n" //Moved some calculations from grDrawXXX to shader
"uniform vec4 textureSizes;            \n"
SHADER_VARYING
"\n"
"void main()\n"
"{\n"
"  highp float q = aPosition.w;                                                     \n"
"  highp float invertY = vertexOffset.z;                                          \n" //Usually 1.0 but -1.0 when rendering to a texture (see inverted_culling grRenderBuffer)
"  gl_Position.x = (aPosition.x - vertexOffset.x) / vertexOffset.x;           \n"
"  gl_Position.y = invertY *-(aPosition.y - vertexOffset.y) / vertexOffset.y; \n"
"  gl_Position.z = aPosition.z / Z_MAX;                                       \n"
"  gl_Position.w = 1.0;                                                     \n"
"  gl_Position /= q;                                                        \n"
"  vColorW = 1.0 / q;                                                     \n"
"  vFrontColorW = aColor.bgra * vColorW;                                  \n"
"\n"
"  vTexCoord[0] = vec4(aMultiTexCoord0.xy / q / textureSizes.xy,0,1);     \n"
"  vTexCoord[1] = vec4(aMultiTexCoord1.xy / q / textureSizes.zw,0,1);     \n"
"\n"
"  vTexCoord[2].b = aPosition.x;                                            \n"
"  vTexCoord[2].a = aPosition.y;                                            \n"
"}                                                                          \n"
;

static char shader_log[2048];

void check_compile(GLuint shader)
{
   GLint success;
   glGetShaderiv(shader,GL_COMPILE_STATUS,&success);

   if (!success)
   {
      char log[1024];
      glGetShaderInfoLog(shader,1024,NULL,log);
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, log);
   }
}

void check_link(GLuint program)
{
   GLint success;
   glGetProgramiv(program,GL_LINK_STATUS,&success);

   if (!success)
   {
      char log[1024];
      glGetProgramInfoLog(program,1024,NULL,log);
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, log);
   }
}

#define SHADER_PROGRAMS_RESERVED 1024
static int shader_programs_capacity = 0;

static void append_shader_program(shader_program_key *shader)
{
   int curr_index = -1;
   int                   index = number_of_programs;

   if (current_shader && current_shader != &uber_program)
      curr_index = current_shader->index;

   shader->index = index;

   /* room for a game's worth is taken at init: this stays off the draw path */
   if (index >= shader_programs_capacity)
   {
      int capacity = shader_programs_capacity ? shader_programs_capacity * 2 : SHADER_PROGRAMS_RESERVED;
      shader_program_key *new_ptr = (shader_program_key*)
         realloc(shader_programs, capacity * sizeof(shader_program_key));
      if (!new_ptr)
         return;

      shader_programs          = new_ptr;
      shader_programs_capacity = capacity;
   }

   if (curr_index >= 0)
      current_shader = &shader_programs[curr_index];

   shader_programs[index] = *shader;

   ++number_of_programs;
}

static void shader_bind_attributes(shader_program_key *shader)
{
   GLuint prog = shader->program_object;

   glBindAttribLocation(prog, POSITION_ATTR,   "aPosition");
   glBindAttribLocation(prog, COLOUR_ATTR,     "aColor");
   glBindAttribLocation(prog, TEXCOORD_0_ATTR, "aMultiTexCoord0");
   glBindAttribLocation(prog, TEXCOORD_1_ATTR, "aMultiTexCoord1");
}

static void use_shader_program(shader_program_key *shader)
{
   current_shader = &shader_programs[shader->index];
   glUseProgram(shader->program_object);
}

static void shader_find_uniforms(shader_program_key *shader)
{
   GLuint prog = shader->program_object;

   /* vertex shader uniforms */
   shader->vertexOffset_location    = glGetUniformLocation(prog, "vertexOffset");
   shader->textureSizes_location    = glGetUniformLocation(prog, "textureSizes");

   /* fragment shader uniforms */
   shader->texture0_location       = glGetUniformLocation(prog, "texture0");
   shader->texture1_location       = glGetUniformLocation(prog, "texture1");
   shader->exactSizes_location     = glGetUniformLocation(prog, "exactSizes");
   shader->fogColor_location       = glGetUniformLocation(prog, "fogColor");
   shader->alphaRef_location       = glGetUniformLocation(prog, "alphaRef");
   shader->n64Prim_location        = glGetUniformLocation(prog, "n64Prim");
   shader->n64Env_location         = glGetUniformLocation(prog, "n64Env");
   shader->n64Const_location       = glGetUniformLocation(prog, "n64Const");
   shader->n64Level_location       = glGetUniformLocation(prog, "n64Level");
   shader->n64KeyCenter_location   = glGetUniformLocation(prog, "n64KeyCenter");
   shader->n64KeyScale_location    = glGetUniformLocation(prog, "n64KeyScale");
   shader->fogAlpha_location       = glGetUniformLocation(prog, "fogAlpha");
   shader->n64Blend_location       = glGetUniformLocation(prog, "n64Blend");
   shader->n64Blend0_location      = glGetUniformLocation(prog, "n64Blend0");
   shader->n64Blend1_location      = glGetUniformLocation(prog, "n64Blend1");
   shader->n64Ops_location         = glGetUniformLocation(prog, "n64Ops");
   shader->n64Flags0_location      = glGetUniformLocation(prog, "n64Flags0");
   shader->n64Flags1_location      = glGetUniformLocation(prog, "n64Flags1");
   shader->n64Flags2_location      = glGetUniformLocation(prog, "n64Flags2");
}

/* fragment_shader compiled and linked with the vertex shader; with parallel
 * shader compiles this returns before either is done */
static void start_shader_program(shader_program_key *shader)
{
   GLuint fragshader = glCreateShader(GL_FRAGMENT_SHADER);

   glShaderSource(fragshader, 1, (const GLchar**)&fragment_shader, NULL);
   glCompileShader(fragshader);

   shader->program_object = glCreateProgram();
   glAttachShader(shader->program_object, vertex_shader_object);
   glAttachShader(shader->program_object, fragshader);

   shader_bind_attributes(shader);

   glLinkProgram(shader->program_object);
   /* it goes with the program */
   glDeleteShader(fragshader);
}

/* the program linked: bound, and its uniforms found */
static void complete_shader_program(shader_program_key *shader)
{
   check_link(shader->program_object);
   glUseProgram(shader->program_object);
   shader_find_uniforms(shader);
}

static void finish_shader_program_setup(shader_program_key *shader)
{
   start_shader_program(shader);
   complete_shader_program(shader);
   append_shader_program(shader);
   /* the program bound now: the uniforms set until the next switch go to
    * it (they went to the program before, and GL refused them) */
   current_shader = &shader_programs[shader->index];
}

/* What a game compiled, keyed as compile_shader looks it up and kept as
 * source: the fragment text depends on the order of the state calls, so a key
 * cannot be turned back into it. Written at the end of a session. */
#define FRAGMENT_SHADER_SIZE (4096 * 2)
#define SHADER_CACHE_SIZE    (4u * 1024u * 1024u)
#define SHADER_CACHE_MAGIC   0x47324c49u
#define SHADER_CACHE_FIELDS  6
#define SHADER_CACHE_RECORD  ((SHADER_CACHE_FIELDS + 1) * sizeof(int32_t))

static char   *cache_arena = NULL;
static size_t  cache_used  = 0;
static int     cache_dirty = 0;
static char    cache_path[PATH_MAX_LENGTH];

static uint32_t combiner_cache_version(void)
{
   uint32_t crc = encoding_crc32(0, (const uint8_t*)vertex_shader, strlen(vertex_shader));
   return encoding_crc32(crc, (const uint8_t*)fragment_shader_header, strlen(fragment_shader_header));
}

static void combiner_cache_key(const shader_program_key *shader, int32_t *fields)
{
   fields[0] = shader->dither_enabled;
   fields[1] = shader->three_point_filter0;
   fields[2] = shader->three_point_filter1;
   fields[3] = (int32_t)shader->n64_key0;
   fields[4] = (int32_t)shader->n64_key1;
   fields[5] = (int32_t)shader->n64_key2;
}

static void combiner_cache_record(const shader_program_key *shader)
{
   int32_t fields[SHADER_CACHE_FIELDS + 1];
   size_t  len = strlen(fragment_shader);

   if (!cache_arena || cache_used + SHADER_CACHE_RECORD + len > SHADER_CACHE_SIZE)
      return;
   combiner_cache_key(shader, fields);
   fields[SHADER_CACHE_FIELDS] = (int32_t)len;
   memcpy(cache_arena + cache_used, fields, SHADER_CACHE_RECORD);
   memcpy(cache_arena + cache_used + SHADER_CACHE_RECORD, fragment_shader, len);
   cache_used  += SHADER_CACHE_RECORD + len;
   cache_dirty  = 1;
}

static void combiner_cache_load(void)
{
   void    *data = NULL;
   int64_t  size = 0;
   size_t   pos  = 8;

   cache_used  = 0;
   cache_dirty = 0;
   if (!cache_arena)
      cache_arena = (char*)malloc(SHADER_CACHE_SIZE);
   combiner_key_path(cache_path, sizeof(cache_path), "glide64", gfx_info.HEADER);

   if (!cache_arena || !filestream_exists(cache_path)
         || !filestream_read_file(cache_path, &data, &size) || !data)
      return;

   if (      size > 8
         && ((const uint32_t*)data)[0] == SHADER_CACHE_MAGIC
         && ((const uint32_t*)data)[1] == combiner_cache_version())
   {
      const char *p = (const char*)data;

      while (pos + SHADER_CACHE_RECORD <= (size_t)size)
      {
         int32_t fields[SHADER_CACHE_FIELDS + 1];
         shader_program_key shader;
         size_t len;

         memcpy(fields, p + pos, SHADER_CACHE_RECORD);
         len = (size_t)(uint32_t)fields[SHADER_CACHE_FIELDS];
         if (      len >= FRAGMENT_SHADER_SIZE
               || pos + SHADER_CACHE_RECORD + len > (size_t)size
               || cache_used + SHADER_CACHE_RECORD + len > SHADER_CACHE_SIZE)
            break;

         memset(&shader, 0, sizeof(shader));
         shader.dither_enabled      = fields[0];
         shader.three_point_filter0 = fields[1];
         shader.three_point_filter1 = fields[2];
         shader.n64_key0            = (uint32_t)fields[3];
         shader.n64_key1            = (uint32_t)fields[4];
         shader.n64_key2            = (uint32_t)fields[5];

         memcpy(fragment_shader, p + pos + SHADER_CACHE_RECORD, len);
         fragment_shader[len] = '\0';
         finish_shader_program_setup(&shader);

         memcpy(cache_arena + cache_used, p + pos, SHADER_CACHE_RECORD + len);
         cache_used += SHADER_CACHE_RECORD + len;
         pos        += SHADER_CACHE_RECORD + len;
      }
      use_shader_program(&shader_programs[0]);
   }
   free(data);
}

static void combiner_cache_save(void)
{
   if (cache_arena && cache_dirty)
   {
      RFILE *f = filestream_open(cache_path, RETRO_VFS_FILE_ACCESS_WRITE,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (f)
      {
         uint32_t head[2];
         head[0] = SHADER_CACHE_MAGIC;
         head[1] = combiner_cache_version();
         filestream_write(f, head, sizeof(head));
         filestream_write(f, cache_arena, (int64_t)cache_used);
         filestream_close(f);
      }
   }
   free(cache_arena);
   cache_arena = NULL;
   cache_dirty = 0;
}

/* Built at load; without it (it did not link) a miss compiles as before */
static void init_uber_program(void)
{
   const char *main_at = strstr(fragment_shader_header, "void main()");
   const char *ext = (const char*)glGetString(GL_EXTENSIONS);
   size_t head;

   memset(&uber_program, 0, sizeof(uber_program));
   uber_program.index = -1;
   {
      const char *mode = getenv("GLITCH64_COMBINER");
      combiner_mode = !glitch64_shared_shader ? 2 : mode && !strcmp(mode, "uber") ? 1 : 0;
   }
   if (combiner_mode == 2)
      return;
   parallel_compile = ext && (strstr(ext, "GL_KHR_parallel_shader_compile")
         || strstr(ext, "GL_ARB_parallel_shader_compile"));
   if (!main_at)
      return;
   fragment_shader[0] = '\0';
#if defined(HAVE_OPENGLES)
   if (n64_derivatives)
   {
      strcpy(fragment_shader, SHADER_HEADER);
      strcat(fragment_shader, "#extension GL_OES_standard_derivatives : enable\n");
      head = (size_t)(main_at - fragment_shader_header) - strlen(SHADER_HEADER);
      strncat(fragment_shader, fragment_shader_header + strlen(SHADER_HEADER), head);
   }
   else
#endif
   {
      head = (size_t)(main_at - fragment_shader_header);
      strncat(fragment_shader, fragment_shader_header, head);
   }
   strcat(fragment_shader, uber_inputs);
   strcat(fragment_shader, main_at);
   strcat(fragment_shader, uber_body_a);
   if (n64_derivatives)
      strcat(fragment_shader, uber_lod);
   strcat(fragment_shader, uber_body_b);
   strcat(fragment_shader, fragment_shader_close);

   start_shader_program(&uber_program);
   {
      GLint ok = 0;
      glGetProgramiv(uber_program.program_object, GL_LINK_STATUS, &ok);
      if (!ok)
      {
         check_link(uber_program.program_object);
         glDeleteProgram(uber_program.program_object);
         uber_program.program_object = 0;
         return;
      }
   }
   complete_shader_program(&uber_program);

   /* one draw, writing nothing, so that a driver that builds a shader's
    * code at its first draw does that here, at load */
   {
      static const float tri[12] = { 0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f, 1.0f };
      const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
      glUniform1i(uber_program.texture0_location, 0);
      glUniform1i(uber_program.texture1_location, 1);
      glColorMask(0, 0, 0, 0);
      glDisable(GL_DEPTH_TEST);
      glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, 4 * sizeof(float), tri);
      glEnableVertexAttribArray(POSITION_ATTR);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glDisableVertexAttribArray(POSITION_ATTR);
      glColorMask(1, 1, 1, 1);
      if (depth)
         glEnable(GL_DEPTH_TEST);
   }
}

void init_combiner(void)
{
   shader_program_key shader;

   if (shader_programs)
      free(shader_programs);

   number_of_programs = 0;
   shader_programs    = NULL;
   shader_programs_capacity = 0;
   current_shader     = NULL;
   fragment_shader    = (char*)malloc(FRAGMENT_SHADER_SIZE);
   need_to_compile    = true;
   {
      /* until the first Combine: TEXEL0, (0 - 0) * 0 + TEXEL0 */
      glitch64_n64_combiner c;
      memset(&c, 0, sizeof(c));
      c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = 1;
      c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = 1;
      glitch64_n64_key(&c, n64_key);
   }
#if defined(HAVE_OPENGLES)
   {
      const char *ext = (const char*)glGetString(GL_EXTENSIONS);
      n64_derivatives = ext && strstr(ext, "GL_OES_standard_derivatives") != NULL;
   }
#else
   n64_derivatives = 1;
#endif

   /* default shader */
   memset(&shader, 0, sizeof(shader));

   strcpy(fragment_shader, fragment_shader_header);
   strcat(fragment_shader, fragment_shader_default);
   strcat(fragment_shader, fragment_shader_end);

   vertex_shader_object = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(vertex_shader_object, 1, (const GLchar**)&vertex_shader, NULL);
   glCompileShader(vertex_shader_object);
   check_compile(vertex_shader_object);

   finish_shader_program_setup(&shader);
   program_object_default = shader.program_object;

   init_depth_shader();
   init_uber_program();

   use_shader_program(&shader);

   glUniform1i(shader.texture0_location, 0);
   glUniform1i(shader.texture1_location, 1);

   need_to_compile = 1;
   dither_enabled = 0;

   combiner_cache_load();
}

static void n64_upload(const shader_program_key *prog)
{
   glUniform4fv(prog->n64Prim_location, 1, n64_values[0]);
   glUniform4fv(prog->n64Env_location, 1, n64_values[1]);
   glUniform4fv(prog->n64Const_location, 1, n64_values[2]);
   glUniform4fv(prog->n64Level_location, 1, n64_values[3]);
   glUniform3fv(prog->n64KeyCenter_location, 1, n64_key_values[0]);
   glUniform3fv(prog->n64KeyScale_location, 1, n64_key_values[1]);
   glUniform3f(prog->fogColor_location, g_gdp.fog_color.r / 255.0f, g_gdp.fog_color.g / 255.0f,
         g_gdp.fog_color.b / 255.0f);
   glUniform1f(prog->fogAlpha_location, g_gdp.fog_color.a / 255.0f);
   glUniform3f(prog->n64Blend_location, g_gdp.blend_color.r / 255.0f, g_gdp.blend_color.g / 255.0f,
         g_gdp.blend_color.b / 255.0f);
}

static void update_uniforms(const shader_program_key *prog)
{
   glUniform1i(prog->texture0_location, 0);
   glUniform1i(prog->texture1_location, 1);
   glUniform3f(prog->vertexOffset_location, (GLfloat)width / 2.f, (GLfloat)height / 2.f, 1.0f);
   glUniform4f(prog->textureSizes_location,
         (float)tex_width[0], (float)tex_height[0], (float)tex_width[1], (float)tex_height[1]);
   glUniform4f(prog->exactSizes_location,
         (float)tex_exactWidth[0], (float)tex_exactHeight[0],
         (float)tex_exactWidth[1], (float)tex_exactHeight[1]);
   glUniform1f(prog->alphaRef_location, alpha_test ? alpha_ref/255.0f : -1.0f);
   n64_upload(prog);
}

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

/* the shared program, reading the combiner and the options as now */
static void use_uber_program(void)
{
   glitch64_n64_combiner n64;
   GLint ops[16];
   int k[16], i;

   glitch64_n64_from_key(n64_key, &n64);
   glitch64_n64_inputs(&n64, k);
   for (i = 0; i < 16; i++)
      ops[i] = k[i];
   current_shader = &uber_program;
   glUseProgram(uber_program.program_object);
   glUniform1iv(uber_program.n64Ops_location, 16, ops);
   glUniform4f(uber_program.n64Flags0_location, n64.two_cycle ? 1.0f : 0.0f,
         (n64.flags & GLITCH64_N64_BLEND0) ? 1.0f : 0.0f, (n64.flags & GLITCH64_N64_BLEND1) ? 1.0f : 0.0f,
         (n64.flags & GLITCH64_N64_LOD) ? 1.0f : 0.0f);
   for (i = 0; i < 2; i++)
      glUniform4f(i ? uber_program.n64Blend1_location : uber_program.n64Blend0_location,
            (float)GLITCH64_BL_P(n64.blend[i]), (float)GLITCH64_BL_A(n64.blend[i]),
            (float)GLITCH64_BL_M(n64.blend[i]), (float)GLITCH64_BL_B(n64.blend[i]));
   glUniform4f(uber_program.n64Flags1_location, 0.0f, dither_enabled ? 1.0f : 0.0f,
         three_point_filter[0] ? 1.0f : 0.0f, three_point_filter[1] ? 1.0f : 0.0f);
   /* computed only where read: the hash costs, and lowp llvmpipe fell
    * over it in a shader this size */
   glUniform4f(uber_program.n64Flags2_location,
         ((n64.two_cycle && n64.rgb[0][0] == 7) || n64.rgb[1][0] == 7) ? 1.0f : 0.0f,
         (n64.flags & GLITCH64_N64_CVG_ALPHA) ? 1.0f : 0.0f, 0.0f, 0.0f);
   update_uniforms(&uber_program);
}

void compile_shader(void)
{
   shader_program_key shader;
   glitch64_n64_combiner n64;
   int i;

   need_to_compile = 0;

   for (i = 0; i < number_of_programs; i++)
   {
      shader_program_key *program = &shader_programs[i];
      if (program->n64_key0 == n64_key[0] &&
            program->n64_key1 == n64_key[1] &&
            program->n64_key2 == n64_key[2] &&
            program->dither_enabled == dither_enabled &&
            program->three_point_filter0 == three_point_filter[0] &&
            program->three_point_filter1 == three_point_filter[1])
      {
         if (program->pending == 1)
         {
            GLint done = 0;
            glGetProgramiv(program->program_object, GL_COMPLETION_STATUS_KHR, &done);
            if (done)
            {
               complete_shader_program(program);
               program->pending = 0;
            }
         }
         if (program->pending || combiner_mode == 1)
         {
            use_uber_program();
            return;
         }
         use_shader_program(program);
         update_uniforms(program);
         return;
      }
   }

   {
      static int strict = -1;
      if (strict < 0)
         strict = getenv("PARALLEL_N64_COMBINER_STRICT") != NULL;
      if (strict)
         abort();
   }

   memset(&shader, 0, sizeof(shader));
   shader.dither_enabled        = dither_enabled;
   shader.three_point_filter0   = three_point_filter[0];
   shader.three_point_filter1   = three_point_filter[1];
   shader.n64_key0              = n64_key[0];
   shader.n64_key1              = n64_key[1];
   shader.n64_key2              = n64_key[2];

   glitch64_n64_from_key(n64_key, &n64);
#if defined(HAVE_OPENGLES)
   if (n64.flags & GLITCH64_N64_LOD)
   {
      /* the extension goes straight after #version */
      strcpy(fragment_shader, SHADER_HEADER);
      strcat(fragment_shader, "#extension GL_OES_standard_derivatives : enable\n");
      strcat(fragment_shader, fragment_shader_header + strlen(SHADER_HEADER));
   }
   else
#endif
      strcpy(fragment_shader, fragment_shader_header);
   if (dither_enabled)
      strcat(fragment_shader, fragment_shader_dither);
   strcat(fragment_shader, three_point_filter[0] ? fragment_shader_readtex0color_3point : fragment_shader_readtex0color);
   strcat(fragment_shader, three_point_filter[1] ? fragment_shader_readtex1color_3point : fragment_shader_readtex1color);
   glitch64_n64_fragment(&n64, fragment_shader, FRAGMENT_SHADER_SIZE);
   strcat(fragment_shader, fragment_shader_close);

   /* kept for the next session's load either way */
   combiner_cache_record(&shader);
   if (uber_program.program_object)
   {
      /* never compiled here: started where the driver compiles behind,
       * else left for the next load; the shared program draws for it */
      if (parallel_compile && combiner_mode != 1)
      {
         start_shader_program(&shader);
         shader.pending = 1;
      }
      else
         shader.pending = 2;
      append_shader_program(&shader);
      use_uber_program();
      return;
   }
   finish_shader_program_setup(&shader);

   update_uniforms(&shader);
}

void free_combiners(void)
{
   combiner_cache_save();

   if (shader_programs)
   {
      shader_program_key *s = shader_programs;

      while (number_of_programs--)
      {
         if (glIsProgram(s->program_object))
            glDeleteProgram(s->program_object);
         s++;
      }

      free(shader_programs);
   }

   if (fragment_shader)
      free(fragment_shader);

   shader_programs = NULL;
   shader_programs_capacity = 0;
   current_shader  = NULL;
   fragment_shader = NULL;
   if (uber_program.program_object && glIsProgram(uber_program.program_object))
      glDeleteProgram(uber_program.program_object);
   uber_program.program_object = 0;

   number_of_programs = 0;
}

void set_copy_shader(void)
{
   int texture0_location;
   int alphaRef_location;

   glUseProgram(program_object_default);
   texture0_location = glGetUniformLocation(program_object_default, "texture0");
   glUniform1i(texture0_location, 0);

   alphaRef_location = glGetUniformLocation(program_object_default, "alphaRef");
   if(alphaRef_location != -1)
      glUniform1f(alphaRef_location,alpha_test ? alpha_ref/255.0f : -1.0f);
}

/* The program grDepthImageWrite draws with: the game's vertex shader, and
 * a fragment shader turning an N64 depth image into window depth. 0 where
 * the fragment depth cannot be written (GLSL ES 1.00 without
 * GL_EXT_frag_depth). */
GLuint program_object_depth;
/* its uniforms' locations, found at link */
GLint depth_vertexOffset_location, depth_textureSizes_location, depth_exactSizes_location;

/* The depth image is uploaded as it lies in RDRAM, two bytes a texel of
 * luminance (low byte) and alpha (high byte) on a little endian host, and
 * each pair of words swapped: the word for pixel i, counted along the rows,
 * is texel i ^ 1; a pair straddles two rows of odd length. A vertex
 * z of w lands at w / 65536 in clip space, (w / 65536 + 1) / 2 in the
 * window: the image's words go to the same depths. */
static const char *fragment_shader_depth_image =
SHADER_HEADER
#if defined(HAVE_OPENGLES)
"#extension GL_EXT_frag_depth : enable \n"
"#ifdef GL_FRAGMENT_PRECISION_HIGH     \n"
"precision highp float;                \n"
"#else                                 \n"
"precision mediump float;              \n"
"#endif                                \n"
"#define DEPTH_OUT gl_FragDepthEXT     \n"
#else
"#define highp                         \n"
"#define DEPTH_OUT gl_FragDepth        \n"
#endif
"uniform sampler2D texture0;           \n"
"uniform vec4 exactSizes;              \n" /* width, height, pairs swapped, high byte first */
SHADER_VARYING
"void main()                           \n"
"{                                     \n"
"  vec2 t = floor(vTexCoord[0].xy);    \n"
"  float i = t.y * exactSizes.x + t.x; \n"
"  float y; \n"
"  i += exactSizes.z * (1.0 - 2.0 * mod(i, 2.0)); \n"
"  y = floor((i + 0.5) / exactSizes.x); \n"
"  vec4 c = texture2D(texture0, (vec2(i - y * exactSizes.x, y) + 0.5) / exactSizes.xy); \n"
"  vec2 b = floor(c.ra * 255.0 + 0.5); \n"
"  float w = mix(b.y * 256.0 + b.x, b.x * 256.0 + b.y, exactSizes.w); \n"
"  DEPTH_OUT = w / 131072.0 + 0.5;     \n"
"  gl_FragColor = vec4(0.0);           \n"
"}                                     \n"
;

void set_depth_shader(void)
{
   glUseProgram(program_object_depth);
}

static void init_depth_shader(void)
{
   GLuint fragshader;
   GLint ok = 0;

   program_object_depth = 0;
#if defined(HAVE_OPENGLES2)
   {
      const char *ext = (const char*)glGetString(GL_EXTENSIONS);
      if (!ext || !strstr(ext, "GL_EXT_frag_depth"))
         return;
   }
#endif
   fragshader = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(fragshader, 1, (const GLchar**)&fragment_shader_depth_image, NULL);
   glCompileShader(fragshader);
   glGetShaderiv(fragshader, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      check_compile(fragshader);
      glDeleteShader(fragshader);
      return;
   }
   program_object_depth = glCreateProgram();
   glAttachShader(program_object_depth, vertex_shader_object);
   glAttachShader(program_object_depth, fragshader);
   glBindAttribLocation(program_object_depth, POSITION_ATTR,   "aPosition");
   glBindAttribLocation(program_object_depth, COLOUR_ATTR,     "aColor");
   glBindAttribLocation(program_object_depth, TEXCOORD_0_ATTR, "aMultiTexCoord0");
   glBindAttribLocation(program_object_depth, TEXCOORD_1_ATTR, "aMultiTexCoord1");
   glLinkProgram(program_object_depth);
   glDeleteShader(fragshader);
   glGetProgramiv(program_object_depth, GL_LINK_STATUS, &ok);
   if (!ok)
   {
      check_link(program_object_depth);
      glDeleteProgram(program_object_depth);
      program_object_depth = 0;
      return;
   }
   depth_vertexOffset_location = glGetUniformLocation(program_object_depth, "vertexOffset");
   depth_textureSizes_location = glGetUniformLocation(program_object_depth, "textureSizes");
   depth_exactSizes_location   = glGetUniformLocation(program_object_depth, "exactSizes");
   glUseProgram(program_object_depth);
   glUniform1i(glGetUniformLocation(program_object_depth, "texture0"), 0);
   /* texels of the image */
   glUniform4f(depth_textureSizes_location, 1.0f, 1.0f, 1.0f, 1.0f);
}

void grN64Combine(const glitch64_n64_combiner *c)
{
   GLITCH64_VK(vk_grN64Combine(c));
   glitch64_n64_combiner m = *c;
   uint32_t key[GLITCH64_N64_KEY_WORDS];
   if (!n64_derivatives)
      m.flags &= ~GLITCH64_N64_LOD;
   glitch64_n64_key(&m, key);
   if (memcmp(key, n64_key, sizeof(key)))
   {
      glitch64_flush();
      memcpy(n64_key, key, sizeof(key));
      need_to_compile = 1;
   }
}

void grN64Constants(const float prim[4], const float env[4], const float consts[4],
      const float level[4], const float key_center[3], const float key_scale[3])
{
   GLITCH64_VK(vk_grN64Constants(prim, env, consts, level, key_center, key_scale));
   if (!memcmp(n64_values[0], prim, sizeof(n64_values[0]))
         && !memcmp(n64_values[1], env, sizeof(n64_values[1]))
         && !memcmp(n64_values[2], consts, sizeof(n64_values[2]))
         && !memcmp(n64_values[3], level, sizeof(n64_values[3]))
         && !memcmp(n64_key_values[0], key_center, sizeof(n64_key_values[0]))
         && !memcmp(n64_key_values[1], key_scale, sizeof(n64_key_values[1]))
         && n64_fog_rgba == g_gdp.fog_color.total
         && n64_blend_rgba == g_gdp.blend_color.total)
      return;
   glitch64_flush();
   n64_fog_rgba = g_gdp.fog_color.total;
   n64_blend_rgba = g_gdp.blend_color.total;
   memcpy(n64_values[0], prim, sizeof(n64_values[0]));
   memcpy(n64_values[1], env, sizeof(n64_values[1]));
   memcpy(n64_values[2], consts, sizeof(n64_values[2]));
   memcpy(n64_values[3], level, sizeof(n64_values[3]));
   memcpy(n64_key_values[0], key_center, sizeof(n64_key_values[0]));
   memcpy(n64_key_values[1], key_scale, sizeof(n64_key_values[1]));
   if (!need_to_compile && current_shader)
      n64_upload(current_shader);
}

/* the alpha test's reference is a uniform: a change sends it up again
 * (need_to_compile finds the program and updates its uniforms) */
void grAlphaTestReferenceValue(uint8_t value)
{
   GLITCH64_VK(vk_grAlphaTestReferenceValue(value));
   if (alpha_ref != value)
   {
      glitch64_flush();
      alpha_ref = value;
      need_to_compile = 1;
   }
}

void grAlphaTestFunction( int32_t function, uint8_t value, int set_alpha_ref)
{
   GLITCH64_VK(vk_grAlphaTestFunction(function, value, set_alpha_ref));
   const bool test = (function == GR_CMP_ALWAYS) ? false : true;
   const int ref = (set_alpha_ref) ? value : alpha_ref;
   if (alpha_func == function && alpha_test == test && alpha_ref == ref)
      return;
   glitch64_flush();
   alpha_func = function;
   alpha_test = test;
   alpha_ref = ref;
   need_to_compile = 1;
}

void grStippleMode( int32_t mode )
{
   GLITCH64_VK(vk_grStippleMode(mode));
   const int dither = mode != GR_STIPPLE_DISABLE;
   if (dither == dither_enabled)
      return;
   glitch64_flush();
   dither_enabled = dither;
   need_to_compile = 1;
}

void grAlphaBlendFunction(uint32_t rgb_sf, uint32_t rgb_df, uint32_t alpha_sf, uint32_t alpha_df)
{
   GLITCH64_VK(vk_grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df));
   static GLenum last[4];
   if (last[0] == rgb_sf && last[1] == rgb_df && last[2] == alpha_sf && last[3] == alpha_df
         && glIsEnabled(GL_BLEND))
      return;
   last[0] = rgb_sf; last[1] = rgb_df; last[2] = alpha_sf; last[3] = alpha_df;
   glitch64_flush();
   glEnable(GL_BLEND);
   glBlendFuncSeparate(rgb_sf, rgb_df, alpha_sf, alpha_df);
}
