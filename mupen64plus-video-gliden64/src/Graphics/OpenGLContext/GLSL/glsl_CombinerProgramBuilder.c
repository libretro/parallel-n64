/* GLideN64's GL combiner programs: a combine key's fragment shader written
 * from its stages, the four vertex shaders, the ubershader that draws a
 * key while the driver compiles the key's own program, and the programs
 * made of them. A shader is handed to GL as the strings it is made of,
 * its parts' lines, as few strings as C89 lets them be, and its stages'
 * inputs, none copied. C89. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Types.h>
#include <Config.h>
#include <gDP.h>
#include <GBI.h>
#include <CombinerKey.h>
#include <CombinerMux.h>
#include <gli_combiner.h>
#include <NoiseTexture.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/GLFunctions.h>
#include <Graphics/OpenGLContext/opengl_GLInfo.h>
#include <Graphics/OpenGLContext/opengl_CachedFunctions.h>
#include "glsl_Utils.h"
#include "glsl_CombinerProgramImpl.h"
#include "glsl_Programs.h"

#define NOISE_STR_(x) #x
#define NOISE_STR(x) NOISE_STR_(x)

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

/*---------------The shaders' lines-------------*/

static const char *const vh_gles2[] = {
	"#version 100			\n"
	"#if (__VERSION__ > 120)	\n"
	"# define IN in				\n"
	"# define OUT out			\n"
	"#else						\n"
	"# define IN attribute		\n"
	"# define OUT varying		\n"
	"#endif // __VERSION		\n",
	NULL
};

static const char *const vs_textured_triangle[] = {
	"IN highp vec4 aPosition;							\n"
	"IN lowp vec4 aColor;								\n"
	"IN highp vec2 aTexCoord;							\n"
	"IN lowp float aNumLights;							\n"
	"IN highp vec4 aModify;								\n"
	"													\n"
	"uniform int uTexturePersp;							\n"
	"													\n"
	"uniform lowp int uFogUsage;						\n"
	"uniform mediump vec2 uFogScale;					\n"
	"uniform mediump vec2 uScreenCoordsScale;			\n"
	"													\n"
	"uniform mediump vec2 uTexScale;					\n"
	"uniform mediump vec2 uTexOffset[2];				\n"
	"uniform mediump vec2 uCacheScale[2];				\n"
	"uniform mediump vec2 uCacheOffset[2];				\n",
	"uniform mediump vec2 uCacheShiftScale[2];			\n"
	"uniform lowp ivec2 uCacheFrameBuffer;				\n"
	"SHADE OUT lowp vec4 vShadeColor;							\n"
	"OUT highp vec2 vTexCoord0;						\n"
	"OUT highp vec2 vTexCoord1;						\n"
	"OUT mediump vec2 vLodTexCoord;						\n"
	"OUT lowp float vNumLights;							\n"
	"mediump vec2 calcTexCoord(in vec2 texCoord, in int idx)		\n"
	"{																\n"
	"    vec2 texCoordOut = texCoord*uCacheShiftScale[idx];			\n"
	"    texCoordOut -= uTexOffset[idx];							\n"
	"    return (uCacheOffset[idx] + texCoordOut)* uCacheScale[idx];\n",
	"}																\n"
	"																\n"
	"void main()													\n"
	"{																\n"
	"  gl_Position = aPosition;										\n"
	"  vShadeColor = aColor;										\n"
	"  vec2 texCoord = aTexCoord;									\n"
	"  texCoord *= uTexScale;										\n"
	"  if (uTexturePersp == 0 && aModify[2] == 0.0) texCoord *= 0.5;\n"
	"  vTexCoord0 = calcTexCoord(texCoord, 0);						\n"
	"  vTexCoord1 = calcTexCoord(texCoord, 1);						\n"
	"  vLodTexCoord = texCoord;										\n"
	"  vNumLights = aNumLights;										\n"
	"  if (aModify != vec4(0.0)) {									\n",
	"    if ((aModify[0]) != 0.0) {									\n"
	"      gl_Position.xy = gl_Position.xy * uScreenCoordsScale + vec2(-1.0, 1.0);	\n"
	"      gl_Position.xy *= gl_Position.w;							\n"
	"    }															\n"
	"    if ((aModify[1]) != 0.0)									\n"
	"      gl_Position.z *= gl_Position.w;							\n"
	"    if ((aModify[3]) != 0.0)									\n"
	"      vNumLights = 0.0;										\n"
	"  }															\n"
	"  gl_Position.y = -gl_Position.y;								\n"
	"  if (uFogUsage > 0) {											\n"
	"    lowp float fp;												\n",
	"    if (aPosition.z < -aPosition.w && aModify[1] == 0.0)		\n"
	"      fp = -uFogScale.s + uFogScale.t;							\n"
	"    else														\n"
	"      fp = aPosition.z/aPosition.w*uFogScale.s + uFogScale.t;	\n"
	"    fp = clamp(fp, 0.0, 1.0);									\n"
	"    if (uFogUsage == 1)										\n"
	"      vShadeColor.a = fp;										\n"
	"    else														\n"
	"      vShadeColor.rgb = vec3(fp);								\n"
	"  }															\n",
	NULL
};

static const char *const vs_triangle[] = {
	"IN highp vec4 aPosition;			\n"
	"IN lowp vec4 aColor;				\n"
	"IN lowp float aNumLights;			\n"
	"IN highp vec4 aModify;				\n"
	"									\n"
	"uniform lowp int uFogUsage;		\n"
	"uniform mediump vec2 uFogScale;	\n"
	"uniform mediump vec2 uScreenCoordsScale;\n"
	"									\n"
	"SHADE OUT lowp vec4 vShadeColor;			\n"
	"OUT lowp float vNumLights;			\n"
	"																\n"
	"void main()													\n"
	"{																\n"
	"  gl_Position = aPosition;										\n"
	"  vShadeColor = aColor;										\n"
	"  vNumLights = aNumLights;										\n"
	"  if (aModify != vec4(0.0)) {									\n",
	"    if ((aModify[0]) != 0.0) {									\n"
	"      gl_Position.xy = gl_Position.xy * uScreenCoordsScale + vec2(-1.0, 1.0);	\n"
	"      gl_Position.xy *= gl_Position.w;							\n"
	"    }															\n"
	"    if ((aModify[1]) != 0.0) 									\n"
	"      gl_Position.z *= gl_Position.w;							\n"
	"    if ((aModify[3]) != 0.0)									\n"
	"      vNumLights = 0.0;										\n"
	"  }															\n"
	"  gl_Position.y = -gl_Position.y;								\n"
	"  if (uFogUsage > 0) {											\n"
	"    lowp float fp;												\n",
	"    if (aPosition.z < -aPosition.w && aModify[1] == 0.0)		\n"
	"      fp = -uFogScale.s + uFogScale.t;							\n"
	"    else														\n"
	"      fp = aPosition.z/aPosition.w*uFogScale.s + uFogScale.t;	\n"
	"    fp = clamp(fp, 0.0, 1.0);									\n"
	"    if (uFogUsage == 1)										\n"
	"      vShadeColor.a = fp;										\n"
	"    else														\n"
	"      vShadeColor.rgb = vec3(fp);								\n"
	"  }															\n",
	NULL
};

static const char *const vs_textured_rect[] = {
	"IN highp vec4 aRectPosition;						\n"
	"IN highp vec2 aTexCoord0;							\n"
	"IN highp vec2 aTexCoord1;							\n"
	"													\n"
	"SHADE OUT lowp vec4 vShadeColor;							\n"
	"OUT highp vec2 vTexCoord0;						\n"
	"OUT highp vec2 vTexCoord1;						\n"
	"uniform lowp vec4 uRectColor;						\n"
	"void main()										\n"
	"{													\n"
	"  gl_Position = aRectPosition;						\n"
	"  vShadeColor = uRectColor;						\n"
	"  vTexCoord0 = aTexCoord0;							\n"
	"  vTexCoord1 = aTexCoord1;							\n",
	NULL
};

static const char *const vs_rect[] = {
	"IN highp vec4 aRectPosition;						\n"
	"													\n"
	"SHADE OUT lowp vec4 vShadeColor;							\n"
	"uniform lowp vec4 uRectColor;						\n"
	"void main()										\n"
	"{													\n"
	"  gl_Position = aRectPosition;						\n"
	"  vShadeColor = uRectColor;						\n",
	NULL
};

static const char *const vs_end_clip[] = {
	"  gl_ClipDistance[0] = gl_Position.w - gl_Position.z;	\n",
	NULL
};

static const char *const vs_end_zcoord[] = {
	"  vZCoord = gl_Position.z / gl_Position.w;	\n"
	"  if (uClampMode > 0)	\n"
	"    gl_Position.z = 0.0;	\n",
	NULL
};

static const char *const fh_gles2_version[] = {
	"#version 100 \n",
	NULL
};

static const char *const fh_gles2_lod[] = {
	"#extension GL_EXT_shader_texture_lod : enable \n"
	"#extension GL_OES_standard_derivatives : enable \n",
	NULL
};

static const char *const fh_gles2[] = {
	"#if (__VERSION__ > 120)		\n"
	"# define IN in					\n"
	"# define OUT out				\n"
	"# define texture2D texture		\n"
	"#else							\n"
	"# define IN varying			\n"
	"# define OUT					\n"
	"#ifndef GL_FRAGMENT_PRECISION_HIGH \n"
	"precision mediump float;		\n"
	"# define highp mediump			\n"
	"#endif							\n"
	"#endif // __VERSION __			\n",
	NULL
};

static const char *const blender1[] = {
	"  muxPM[0] = clampedColor;													\n"
	"  lowp vec4 vprobe = vec4(0.0, 1.0, 2.0, 3.0);								\n"
	"  if (uForceBlendCycle1 != 0) {											\n"
	"    muxA[0] = clampedColor.a;												\n"
	"    lowp float muxa = dot(muxA, vec4(equal(vec4(uBlendMux1[1]), vprobe)));	\n"
	"    muxB[0] = 1.0 - muxa;													\n"
	"    lowp vec4 muxpm0 = muxPM * vec4(equal(vec4(uBlendMux1[0]), vprobe));	\n"
	"    lowp vec4 muxpm2 = muxPM * vec4(equal(vec4(uBlendMux1[2]), vprobe));	\n",
	"    lowp float muxb = dot(muxB, vec4(equal(vec4(uBlendMux1[3]), vprobe)));	\n"
	"    lowp vec4 blend1 = (muxpm0 * muxa) + (muxpm2 * muxb);					\n"
	"    clampedColor.rgb = clamp(blend1.rgb, 0.0, 1.0);						\n"
	"  } else {																	\n",
	NULL
};

#if defined(OS_MAC_OS_X)
static const char *const blender1_mac[] = {
	"      clampedColor.rgb = muxPM[uBlendMux1[0]].rgb;							\n",
	NULL
};
#endif

#if !defined(OS_MAC_OS_X)
static const char *const blender1_pc[] = {
	"      lowp vec4 muxpm0 = muxPM * vec4(equal(vec4(uBlendMux1[0]), vprobe));	\n"
	"      clampedColor.rgb = muxpm0.rgb;										\n",
	NULL
};
#endif

static const char *const blender_end[] = {
	"  }																		\n",
	NULL
};

static const char *const blender2[] = {
	"  muxPM[0] = clampedColor;													\n"
	"  muxPM[1] = vec4(0.0);													\n"
	"  if (uForceBlendCycle2 != 0) {											\n"
	"    muxA[0] = clampedColor.a;												\n"
	"    lowp float muxa = dot(muxA, vec4(equal(vec4(uBlendMux2[1]), vprobe)));	\n"
	"    muxB[0] = 1.0 - muxa;													\n"
	"    lowp vec4 muxpm0 = muxPM*vec4(equal(vec4(uBlendMux2[0]), vprobe));		\n"
	"    lowp vec4 muxpm2 = muxPM*vec4(equal(vec4(uBlendMux2[2]), vprobe));		\n"
	"    lowp float muxb = dot(muxB,vec4(equal(vec4(uBlendMux2[3]), vprobe)));	\n",
	"    lowp vec4 blend2 = muxpm0 * muxa + muxpm2 * muxb;						\n"
	"    clampedColor.rgb = clamp(blend2.rgb, 0.0, 1.0);						\n"
	"  } else {																	\n",
	NULL
};

#if defined(OS_MAC_OS_X)
static const char *const blender2_mac[] = {
	"      clampedColor.rgb = muxPM[uBlendMux2[0]].rgb;							\n",
	NULL
};
#endif

#if !defined(OS_MAC_OS_X)
static const char *const blender2_pc[] = {
	"      lowp vec4 muxpm0 = muxPM * vec4(equal(vec4(uBlendMux2[0]), vprobe));	\n"
	"      clampedColor.rgb = muxpm0.rgb;										\n",
	NULL
};
#endif

static const char *const legacy_blender[] = {
	"  if (uFogUsage == 1) \n"
	"    fragColor.rgb = mix(fragColor.rgb, uFogColor.rgb, vShadeColor.a); \n",
	NULL
};

static const char *const clamp[] = {
	"  lowp vec4 wrappedColor = cmbRes + 2.0 * step(cmbRes, vec4(-0.51)) - 2.0*step(vec4(1.51), cmbRes); \n"
	"  lowp vec4 clampedColor = clamp(wrappedColor, 0.0, 1.0); \n",
	NULL
};

static const char *const sign_extend_color_c[] = {
	"  color1 = color1 - 2.0*(vec3(1.0) - step(color1, vec3(1.0)));	\n",
	NULL
};

static const char *const sign_extend_alpha_c[] = {
	"  alpha1 = alpha1 - 2.0*(1.0 - step(alpha1, 1.0));					\n",
	NULL
};

static const char *const sign_extend_color_abd[] = {
	"  color1 = color1 + 2.0*step(color1, vec3(-0.51)) - 2.0*step(vec3(1.51), color1); \n",
	NULL
};

static const char *const sign_extend_alpha_abd[] = {
	"  alpha1 = alpha1 + 2.0*step(alpha1, -0.51) - 2.0*step(1.51, alpha1); \n",
	NULL
};

static const char *const alpha_test[] = {
	"  if (uEnableAlphaTest != 0) {							\n"
	"    lowp float alphaTestValue = (uAlphaCompareMode == 3) ? snoise() : uAlphaTestValue;	\n"
	"    lowp float alphaValue;								\n"
	"    if ((uAlphaCvgSel != 0) && (uCvgXAlpha == 0)) {	\n"
	"      alphaValue = 0.125;								\n"
	"    } else {											\n"
	"      alphaValue = clamp(alpha1, 0.0, 1.0);			\n"
	"    }													\n"
	"    if (alphaValue < alphaTestValue) discard;			\n"
	"  }													\n",
	NULL
};

static const char *const call_dither[] = {
	"  if (uColorDitherMode == 2) colorNoiseDither(snoise(), clampedColor.rgb);	\n"
	"  if (uAlphaDitherMode == 2) alphaNoiseDither(snoise(), clampedColor.a);	\n",
	NULL
};

static const char *const tex_vars[] = {
	"uniform sampler2D uTex0;		\n"
	"uniform sampler2D uTex1;		\n"
	"uniform lowp vec4 uFogColor;	\n"
	"uniform lowp vec4 uCenterColor;\n"
	"uniform lowp vec4 uScaleColor;	\n"
	"uniform lowp vec4 uBlendColor;	\n"
	"uniform lowp vec4 uEnvColor;	\n"
	"uniform lowp vec4 uPrimColor;	\n"
	"uniform lowp float uPrimLod;	\n"
	"uniform lowp float uK4;		\n"
	"uniform lowp float uK5;		\n"
	"uniform lowp int uAlphaCompareMode;	\n"
	"uniform lowp ivec2 uFbMonochrome;		\n"
	"uniform lowp ivec2 uFbFixedAlpha;		\n"
	"uniform lowp int uEnableAlphaTest;		\n"
	"uniform lowp int uCvgXAlpha;			\n",
	"uniform lowp int uAlphaCvgSel;			\n"
	"uniform lowp float uAlphaTestValue;	\n"
	"uniform lowp int uDepthSource;			\n"
	"uniform highp float uPrimDepth;		\n"
	"uniform mediump vec2 uScreenScale;		\n"
	"uniform lowp int uFogUsage;			\n",
	NULL
};

static const char *const vars_blend_mux[] = {
	"uniform lowp ivec4 uBlendMux1;		\n"
	"uniform lowp int uForceBlendCycle1;\n",
	NULL
};

static const char *const vars_gl[] = {
	"uniform sampler2D uDepthTex;		\n"
	"uniform lowp int uAlphaDitherMode;	\n"
	"uniform lowp int uColorDitherMode;	\n"
	"uniform lowp int uRenderTarget;	\n"
	"uniform mediump vec2 uDepthScale;	\n",
	NULL
};

static const char *const vars_depth_compare[] = {
	"uniform lowp int uEnableDepthCompare;	\n",
	NULL
};

static const char *const vars_gles2[] = {
	"lowp int nCurrentTile;			\n",
	NULL
};

static const char *const tex_vars_ms[] = {
	"uniform lowp ivec2 uMSTexEnabled;	\n"
	"uniform lowp sampler2DMS uMSTex0;	\n"
	"uniform lowp sampler2DMS uMSTex1;	\n",
	NULL
};

static const char *const tex_vars_in[] = {
	"SHADE IN lowp vec4 vShadeColor;	\n"
	"IN highp vec2 vTexCoord0;\n"
	"IN highp vec2 vTexCoord1;\n"
	"IN mediump vec2 vLodTexCoord;\n"
	"IN lowp float vNumLights;	\n",
	NULL
};

static const char *const vars_fetch_out[] = {
	"layout(location = 0) OUT lowp vec4 fragColor;	\n"
	"layout(location = 1) inout highp vec4 depthZ;	\n"
	"layout(location = 2) inout highp vec4 depthDeltaZ;	\n",
	NULL
};

static const char *const vars_out[] = {
	"OUT lowp vec4 fragColor;	\n",
	NULL
};

static const char *const notex_vars[] = {
	"uniform lowp vec4 uFogColor;	\n"
	"uniform lowp vec4 uCenterColor;\n"
	"uniform lowp vec4 uScaleColor;	\n"
	"uniform lowp vec4 uBlendColor;	\n"
	"uniform lowp vec4 uEnvColor;	\n"
	"uniform lowp vec4 uPrimColor;	\n"
	"uniform lowp float uPrimLod;	\n"
	"uniform lowp float uK4;		\n"
	"uniform lowp float uK5;		\n"
	"uniform lowp int uAlphaCompareMode;	\n"
	"uniform lowp ivec2 uFbMonochrome;		\n"
	"uniform lowp ivec2 uFbFixedAlpha;		\n"
	"uniform lowp int uEnableAlphaTest;		\n"
	"uniform lowp int uCvgXAlpha;			\n"
	"uniform lowp int uAlphaCvgSel;			\n",
	"uniform lowp float uAlphaTestValue;	\n"
	"uniform lowp int uDepthSource;			\n"
	"uniform highp float uPrimDepth;		\n"
	"uniform mediump vec2 uScreenScale;		\n",
	NULL
};

static const char *const notex_vars_fog_usage[] = {
	"uniform lowp int uFogUsage;		\n",
	NULL
};

static const char *const notex_vars_in[] = {
	"SHADE IN lowp vec4 vShadeColor;	\n"
	"IN lowp float vNumLights;	\n",
	NULL
};

static const char *const h_noise[] = {
	"lowp float snoise();\n",
	NULL
};

static const char *const h_write_depth[] = {
	"highp float writeDepth();\n",
	NULL
};

static const char *const h_write_depth_nopersp[] = {
	"noperspective IN highp float vZCoord;	\n"
	"uniform lowp float uPolygonOffset;	\n"
	"uniform lowp int uClampMode;	\n",
	NULL
};

static const char *const h_calc_light[] = {
	"void calc_light(in lowp float fLights, in lowp vec3 input_color, out lowp vec3 output_color);\n",
	NULL
};

static const char *const h_mipmap[] = {
	"mediump float mipmap(out lowp vec4 readtex0, out lowp vec4 readtex1);\n",
	NULL
};

static const char *const h_read_ms_tex[] = {
	"lowp vec4 readTexMS(in lowp sampler2DMS mstex, in highp vec2 texCoord, in lowp int fbMonochrome, in lowp int fbFixedAlpha);\n",
	NULL
};

static const char *const h_dither[] = {
	"void colorNoiseDither(in lowp float _noise, inout lowp vec3 _color);\n"
	"void alphaNoiseDither(in lowp float _noise, inout lowp float _alpha);\n",
	NULL
};

static const char *const h_depth_compare[] = {
	"bool depth_compare(highp float curZ);	\n"
	"bool depth_render(highp float Z, highp float curZ);	\n",
	NULL
};

static const char *const h_depth_compare_image[] = {
	"coherent layout(binding = 2, r32f) highp uniform restrict image2D uDepthImageZ;		\n"
	"coherent layout(binding = 3, r32f) highp uniform restrict image2D uDepthImageDeltaZ;	\n",
	NULL
};

static const char *const h_filter_mode[] = {
	"uniform lowp int uTextureFilterMode;								\n",
	NULL
};

static const char *const h_filter_3point[] = {
	"#define TEX_OFFSET(off, tex, texCoord) texture(tex, texCoord - (off)/texSize)			\n"
	"#define TEX_FILTER(name, tex, texCoord)												\\\n"
	"  {																					\\\n"
	"  mediump vec2 texSize = vec2(textureSize(tex,0));										\\\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));							\\\n"
	"  offset -= step(1.0, offset.x + offset.y);											\\\n"
	"  lowp vec4 c0 = TEX_OFFSET(offset, tex, texCoord);									\\\n"
	"  lowp vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord);	\\\n",
	"  lowp vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord);	\\\n"
	"  name = c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0); 							\\\n"
	"  }																					\n",
	NULL
};

static const char *const h_filter_standard[] = {
	"#define TEX_OFFSET(off, tex, texCoord) texture(tex, texCoord - (off)/texSize)									\n"
	"#define TEX_FILTER(name, tex, texCoord)																		\\\n"
	"{																												\\\n"
	"  mediump vec2 texSize = vec2(textureSize(tex,0));																\\\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));													\\\n"
	"  offset -= step(1.0, offset.x + offset.y);																	\\\n"
	"  lowp vec4 zero = vec4(0.0);																					\\\n"
	"																												\\\n",
	"  lowp vec4 p0q0 = TEX_OFFSET(offset, tex, texCoord);															\\\n"
	"  lowp vec4 p1q0 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord);						\\\n"
	"																												\\\n"
	"  lowp vec4 p0q1 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord);						\\\n"
	"  lowp vec4 p1q1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y - sign(offset.y)), tex, texCoord);		\\\n"
	"																												\\\n"
	"  mediump vec2 interpolationFactor = abs(offset);																\\\n",
	"  lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x ); 											\\\n"
	"  lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x ); 											\\\n"
	"  name = mix( pInterp_q0, pInterp_q1, interpolationFactor.y ); 												\\\n"
	"}																												\n",
	NULL
};

static const char *const h_filter_3point_bleed[] = {
	"#define TEX_OFFSET(off, tex, texCoord) texture(tex, texCoord - (off)/texSize)									\n"
	"#define TEX_FILTER(name, tex, texCoord)												\\\n"
	"{																						\\\n"
	"  mediump vec2 texSize = vec2(textureSize(tex,0));										\\\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));							\\\n"
	"  offset -= step(1.0, offset.x + offset.y);											\\\n"
	"  lowp vec4 c0 = TEX_OFFSET(offset, tex, texCoord);									\\\n"
	"  lowp vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord);	\\\n",
	"  lowp vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord);	\\\n"
	"																						\\\n"
	"  if(uEnableAlphaTest == 1 ){															\\\n"
	"    c0.rgb *= c0.a;																	\\\n"
	"    c1.rgb *= c1.a;																	\\\n"
	"    c2.rgb *= c2.a;																	\\\n"
	"    name = c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0); 						\\\n"
	"    name.rgb /= name.a;																\\\n"
	"  }																					\\\n"
	"  else name = c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0); 						\\\n"
	"}																						\n",
	NULL
};

static const char *const h_filter_standard_bleed[] = {
	"#define TEX_OFFSET(off, tex, texCoord) texture(tex, texCoord - (off)/texSize)									\n"
	"#define TEX_FILTER(name, tex, texCoord)																		\\\n"
	"{																												\\\n"
	"  mediump vec2 texSize = vec2(textureSize(tex,0));																\\\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));													\\\n"
	"  offset -= step(1.0, offset.x + offset.y);																	\\\n"
	"  lowp vec4 zero = vec4(0.0);																					\\\n"
	"																												\\\n",
	"  lowp vec4 p0q0 = TEX_OFFSET(offset, tex, texCoord);															\\\n"
	"  lowp vec4 p1q0 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord);						\\\n"
	"																												\\\n"
	"  lowp vec4 p0q1 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord);						\\\n"
	"  lowp vec4 p1q1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y - sign(offset.y)), tex, texCoord);		\\\n"
	"																												\\\n"
	"  if(uEnableAlphaTest == 1){																					\\\n",
	"    p0q0.rgb *= p0q0.a;																						\\\n"
	"    p1q0.rgb *= p1q0.a;																						\\\n"
	"    p0q1.rgb *= p0q1.a;																						\\\n"
	"    p1q1.rgb *= p1q1.a;																						\\\n"
	"																												\\\n"
	"    mediump vec2 interpolationFactor = abs(offset);															\\\n"
	"    lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x ); 											\\\n"
	"    lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x ); 											\\\n",
	"    name = mix( pInterp_q0, pInterp_q1, interpolationFactor.y ); 												\\\n"
	"    name.rgb /= name.a;																						\\\n"
	"  }																											\\\n"
	"  else if(uCvgXAlpha == 1){																					\\\n"
	"    if(p0q0.a > p1q0.a) p1q0.rgb = p0q0.rgb;																	\\\n"
	"    if(p1q0.a > p0q0.a) p0q0.rgb = p1q0.rgb;																	\\\n"
	"    if(p0q1.a > p1q1.a) p1q1.rgb = p0q1.rgb;																	\\\n"
	"    if(p1q1.a > p0q1.a) p0q1.rgb = p1q1.rgb;																	\\\n",
	"    if(p0q0.a > p0q1.a) p0q1.rgb = p0q0.rgb;																	\\\n"
	"    if(p0q1.a > p0q0.a) p0q0.rgb = p0q1.rgb;																	\\\n"
	"    if(p1q0.a > p1q1.a) p1q1.rgb = p1q0.rgb;																	\\\n"
	"    if(p1q1.a > p1q0.a) p1q0.rgb = p1q1.rgb;																	\\\n"
	"																												\\\n"
	"    mediump vec2 interpolationFactor = abs(offset);															\\\n"
	"    lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x );											\\\n",
	"    lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x );											\\\n"
	"    name = mix( pInterp_q0, pInterp_q1, interpolationFactor.y );												\\\n"
	"  }																											\\\n"
	"  else{																										\\\n"
	"    mediump vec2 interpolationFactor = abs(offset);															\\\n"
	"    lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x ); 											\\\n"
	"    lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x ); 											\\\n",
	"    name = mix( pInterp_q0, pInterp_q1, interpolationFactor.y ); 												\\\n"
	"  }																											\\\n"
	"}																												\n",
	NULL
};

static const char *const h_read_tex[] = {
	"#define READ_TEX(name, tex, texCoord, fbMonochrome, fbFixedAlpha)	\\\n"
	"  {																\\\n"
	"  if (fbMonochrome == 3) {											\\\n"
	"    mediump ivec2 coord = ivec2(gl_FragCoord.xy);					\\\n"
	"    name = texelFetch(tex, coord, 0);								\\\n"
	"  } else {															\\\n"
	"    if (uTextureFilterMode == 0) name = texture(tex, texCoord);	\\\n"
	"    else TEX_FILTER(name, tex, texCoord);			 				\\\n"
	"  }																\\\n"
	"  if (fbMonochrome == 1) name = vec4(name.r);						\\\n"
	"  else if (fbMonochrome == 2) 										\\\n",
	"    name.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), name.rgb));	\\\n"
	"  else if (fbMonochrome == 3) { 									\\\n"
	"    name.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), name.rgb));	\\\n"
	"    name.a = 0.0;													\\\n"
	"  }																\\\n"
	"  if (fbFixedAlpha == 1) name.a = 0.825;							\\\n"
	"  }																\n",
	NULL
};

static const char *const h_yuv[] = {
	"uniform lowp ivec2 uTextureFormat;									\n"
	"uniform lowp int uTextureConvert;									\n"
	"uniform mediump ivec4 uConvertParams;								\n"
	"#define YUVCONVERT(name, format)									\\\n"
	"  mediump ivec4 icolor = ivec4(name*255.0);						\\\n"
	"  if (format == 1)													\\\n"
	"    icolor.rg -= 128;												\\\n"
	"  mediump ivec4 iconvert;											\\\n"
	"  iconvert.r = icolor.b + (uConvertParams[0]*icolor.g + 128)/256;	\\\n"
	"  iconvert.g = icolor.b + (uConvertParams[1]*icolor.r + uConvertParams[2]*icolor.g + 128)/256;	\\\n",
	"  iconvert.b = icolor.b + (uConvertParams[3]*icolor.r + 128)/256;	\\\n"
	"  iconvert.a = icolor.b;											\\\n"
	"  name = vec4(iconvert)/255.0;										\n"
	"#define YUVCONVERT_TEX0(name, tex, texCoord, format)				\\\n"
	"  {																\\\n"
	"  name = texture(tex, texCoord);									\\\n"
	"  YUVCONVERT(name, format)											\\\n"
	"  }																\n"
	"#define YUVCONVERT_TEX1(name, tex, texCoord, format, prev)			\\\n"
	"  {																\\\n"
	"  if (uTextureConvert != 0) name = prev;							\\\n"
	"  else name = texture(tex, texCoord);								\\\n",
	"  YUVCONVERT(name, format)											\\\n"
	"  }																\n",
	NULL
};

static const char *const h_gles2_filter[] = {
	"uniform lowp int uTextureFilterMode;								\n"
	"lowp vec4 readTex(in sampler2D tex, in highp vec2 texCoord, in lowp int fbMonochrome, in lowp int fbFixedAlpha);	\n",
	NULL
};

static const char *const h_gles2_yuv[] = {
	"uniform lowp ivec2 uTextureFormat;									\n"
	"uniform lowp int uTextureConvert;									\n"
	"uniform mediump ivec4 uConvertParams;								\n"
	"lowp vec4 YUV_Convert(in sampler2D tex, in highp vec2 texCoord, in lowp int convert, in lowp int format, in lowp vec4 prev);	\n",
	NULL
};

static const char *const h_read_tex_copy[] = {
	"#define READ_TEX(name, tex, texCoord, fbMonochrome, fbFixedAlpha)	\\\n"
	"  {																\\\n"
	"  if (fbMonochrome == 3) {											\\\n"
	"    mediump ivec2 coord = ivec2(gl_FragCoord.xy);					\\\n"
	"    name = texelFetch(tex, coord, 0);								\\\n"
	"  } else {															\\\n"
	"    name = texture(tex, texCoord);									\\\n"
	"  }																\\\n"
	"  if (fbMonochrome == 1) name = vec4(name.r);						\\\n"
	"  else if (fbMonochrome == 2) 										\\\n"
	"    name.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), name.rgb));	\\\n",
	"  else if (fbMonochrome == 3) { 									\\\n"
	"    name.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), name.rgb));	\\\n"
	"    name.a = 0.0;													\\\n"
	"  }																\\\n"
	"  if (fbFixedAlpha == 1) name.a = 0.825;							\\\n"
	"  }																\n",
	NULL
};

static const char *const h_read_tex_copy_gles2[] = {
	"lowp vec4 readTex(in sampler2D tex, in highp vec2 texCoord, in lowp int fbMonochrome, in lowp int fbFixedAlpha);	\n",
	NULL
};

static const char *const main_begin[] = {
	"void main() \n"
	"{			 \n",
	NULL
};

static const char *const main_depth[] = {
	"  highp float fragDepth = writeDepth();	\n",
	NULL
};

static const char *const main_vars[] = {
	"  lowp vec4 vec_color;				\n"
	"  lowp float alpha1;				\n"
	"  lowp vec3 color1, input_color;	\n",
	NULL
};

static const char *const main2_depth[] = {
	"  highp float fragDepth = writeDepth(); \n",
	NULL
};

static const char *const main2_vars[] = {
	"  lowp vec4 vec_color, combined_color;		\n"
	"  lowp float alpha1, alpha2;				\n"
	"  lowp vec3 color1, color2, input_color;	\n",
	NULL
};

static const char *const blend_mux[] = {
	"  lowp mat4 muxPM = mat4(vec4(0.0), vec4(0.0), uBlendColor, uFogColor);	\n"
	"  lowp vec4 muxA = vec4(0.0, uFogColor.a, vShadeColor.a, 0.0);				\n"
	"  lowp vec4 muxB = vec4(0.0, 1.0, 1.0, 0.0);								\n",
	NULL
};

static const char *const read_tex_mipmap[] = {
	"  lowp vec4 readtex0, readtex1;						\n"
	"  lowp float lod_frac = mipmap(readtex0, readtex1);	\n",
	NULL
};

static const char *const read_tex_copy_gles2[] = {
	"  nCurrentTile = 0; \n"
	"  lowp vec4 readtex0 = readTex(uTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0]);		\n",
	NULL
};

static const char *const read_tex_copy_ms[] = {
	"  lowp vec4 readtex0;																	\n"
	"  if (uMSTexEnabled[0] == 0) {															\n"
	"      READ_TEX(readtex0, uTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0])		\n"
	"  } else readtex0 = readTexMS(uMSTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0]);\n",
	NULL
};

static const char *const read_tex_copy[] = {
	"  lowp vec4 readtex0;																	\n"
	"  READ_TEX(readtex0, uTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0])			\n",
	NULL
};

static const char *const read_tex0_gles2[] = {
	"  nCurrentTile = 0; \n",
	NULL
};

static const char *const read_tex0_gles2_bilerp[] = {
	"  lowp vec4 readtex0 = readTex(uTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0]);		\n",
	NULL
};

static const char *const read_tex0_gles2_yuv[] = {
	"  lowp vec4 tmpTex = vec4(0.0);																\n"
	"  lowp vec4 readtex0 = YUV_Convert(uTex0, vTexCoord0, 0, uTextureFormat[0], tmpTex);			\n",
	NULL
};

static const char *const read_tex0_yuv[] = {
	"  lowp vec4 readtex0;																			\n"
	"  YUVCONVERT_TEX0(readtex0, uTex0, vTexCoord0, uTextureFormat[0])								\n",
	NULL
};

static const char *const read_tex0_ms[] = {
	"  lowp vec4 readtex0;																				\n"
	"  if (uMSTexEnabled[0] == 0) {																		\n"
	"    READ_TEX(readtex0, uTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0])						\n"
	"  } else readtex0 = readTexMS(uMSTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0]);			\n",
	NULL
};

static const char *const read_tex0[] = {
	"  lowp vec4 readtex0;																		\n"
	"  READ_TEX(readtex0, uTex0, vTexCoord0, uFbMonochrome[0], uFbFixedAlpha[0])				\n",
	NULL
};

static const char *const read_tex1_gles2[] = {
	"  nCurrentTile = 1; \n",
	NULL
};

static const char *const read_tex1_gles2_bilerp[] = {
	"  lowp vec4 readtex1 = readTex(uTex1, vTexCoord1, uFbMonochrome[1], uFbFixedAlpha[1]);				\n",
	NULL
};

static const char *const read_tex1_gles2_yuv[] = {
	"  lowp vec4 readtex1 = YUV_Convert(uTex1, vTexCoord1, uTextureConvert, uTextureFormat[1], readtex0);	\n",
	NULL
};

static const char *const read_tex1_yuv[] = {
	"  lowp vec4 readtex1;																							\n"
	"    YUVCONVERT_TEX1(readtex1, uTex1, vTexCoord1, uTextureFormat[1], readtex0)					\n",
	NULL
};

static const char *const read_tex1_ms[] = {
	"  lowp vec4 readtex1;																						\n"
	"  if (uMSTexEnabled[1] == 0) {																				\n"
	"    READ_TEX(readtex1, uTex1, vTexCoord1, uFbMonochrome[1], uFbFixedAlpha[1])								\n"
	"  } else readtex1 = readTexMS(uMSTex1, vTexCoord1, uFbMonochrome[1], uFbFixedAlpha[1]);					\n",
	NULL
};

static const char *const read_tex1[] = {
	"  lowp vec4 readtex1;																				\n"
	"  READ_TEX(readtex1, uTex1, vTexCoord1, uFbMonochrome[1], uFbFixedAlpha[1])						\n",
	NULL
};

static const char *const n64_depth_begin[] = {
	"  bool should_discard = false;	\n",
	NULL
};

static const char *const n64_depth_interlock_arb[] = {
	"  beginInvocationInterlockARB();	\n",
	NULL
};

static const char *const n64_depth_interlock_nv[] = {
	"  beginInvocationInterlockNV();	\n",
	NULL
};

static const char *const n64_depth_ordering_intel[] = {
	"  beginFragmentShaderOrderingINTEL();	\n",
	NULL
};

static const char *const n64_depth_call[] = {
	"  if (uRenderTarget != 0) { if (!depth_render(fragColor.r, fragDepth)) should_discard = true; } \n"
	"  else if (!depth_compare(fragDepth)) should_discard = true; \n",
	NULL
};

static const char *const n64_depth_end_arb[] = {
	"  endInvocationInterlockARB();	\n",
	NULL
};

static const char *const n64_depth_end_nv[] = {
	"  endInvocationInterlockNV();	\n",
	NULL
};

static const char *const n64_depth_discard[] = {
	"  if (should_discard) discard;	\n",
	NULL
};

static const char *const render_target[] = {
	"  if (uRenderTarget != 0) {					\n"
	"    if (uRenderTarget > 1) {					\n"
	"      ivec2 coord = ivec2(gl_FragCoord.xy);	\n"
	"      if (fragDepth >= texelFetch(uDepthTex, coord, 0).r) discard;	\n"
	"    }											\n"
	"    fragDepth = fragColor.r;				\n"
	"  }											\n"
	"  gl_FragDepth = fragDepth;	\n",
	NULL
};

static const char *const main_end_gles2[] = {
	"  gl_FragColor = fragColor; \n"
	"} \n"
	"\n",
	NULL
};

static const char *const main_end[] = {
	"} \n"
	"\n",
	NULL
};

static const char *const noise_dummy[] = {
	"lowp float snoise()	\n"
	"{						\n"
	"  return 0.5;			\n"
	"}						\n",
	NULL
};

static const char *const dither[] = {
	"void colorNoiseDither(in lowp float _noise, inout lowp vec3 _color)	\n"
	"{															\n"
	"    mediump vec3 tmpColor = _color*255.0;					\n"
	"    mediump ivec3 iColor = ivec3(tmpColor);				\n"
	"    iColor |= ivec3(tmpColor*_noise)&7;					\n"
	"    _color = vec3(iColor)/255.0;							\n"
	"}															\n"
	"void alphaNoiseDither(in lowp float _noise, inout lowp float _alpha)	\n"
	"{															\n"
	"    mediump float tmpAlpha = _alpha*255.0;					\n"
	"    mediump int iAlpha = int(tmpAlpha);					\n"
	"    iAlpha |= int(tmpAlpha*_noise)&7;						\n",
	"    _alpha = float(iAlpha)/255.0;							\n"
	"}															\n",
	NULL
};

static const char *const write_depth_dummy[] = {
	"highp float writeDepth()	    \n"
	"{						\n"
	"  return 0.0;	\n"
	"}						\n",
	NULL
};

static const char *const write_depth_nopersp[] = {
	"highp float writeDepth()																	\n"
	"{																							\n"
	"  if (uClampMode == 1 && (vZCoord > 1.0)) discard;											\n"
	"  if (uDepthSource != 0) return uPrimDepth;												\n"
	"  return clamp((vZCoord - uPolygonOffset) * uDepthScale.s + uDepthScale.t, 0.0, 1.0);		\n"
	"}																							\n",
	NULL
};

static const char *const write_depth[] = {
	"highp float writeDepth()						        									\n"
	"{																							\n"
	"  if (uDepthSource != 0) return uPrimDepth;												\n"
	"  return clamp((gl_FragCoord.z * 2.0 - 1.0) * uDepthScale.s + uDepthScale.t, 0.0, 1.0);	\n"
	"}																							\n",
	NULL
};

static const char *const mipmap_gles2_fake[] = {
	"uniform lowp int uMaxTile;			\n"
	"uniform mediump float uMinLod;		\n"
	"														\n"
	"mediump float mipmap(out lowp vec4 readtex0, out lowp vec4 readtex1) {	\n"
	"  readtex0 = texture2D(uTex0, vTexCoord0);				\n"
	"  readtex1 = texture2D(uTex1, vTexCoord1);				\n"
	"  if (uMaxTile == 0) return 1.0;						\n"
	"  return uMinLod;										\n"
	"}														\n",
	NULL
};

static const char *const mipmap_gles2[] = {
	"uniform lowp int uEnableLod;		\n"
	"uniform mediump float uMinLod;		\n"
	"uniform lowp int uMaxTile;			\n"
	"uniform lowp int uTextureDetail;	\n"
	"														\n"
	"mediump float mipmap(out lowp vec4 readtex0, out lowp vec4 readtex1) {	\n"
	"  readtex0 = texture2D(uTex0, vTexCoord0);				\n"
	"  readtex1 = texture2DLodEXT(uTex1, vTexCoord1, 0.0);		\n"
	"														\n"
	"  mediump float fMaxTile = float(uMaxTile);			\n"
	"  mediump vec2 dx = abs(dFdx(vLodTexCoord));			\n"
	"  dx *= uScreenScale;									\n"
	"  mediump float lod = max(dx.x, dx.y);					\n",
	"  bool magnify = lod < 1.0;							\n"
	"  mediump float lod_tile = magnify ? 0.0 : floor(log2(floor(lod))); \n"
	"  bool distant = lod > 128.0 || lod_tile >= fMaxTile;	\n"
	"  mediump float lod_frac = fract(lod/pow(2.0, lod_tile));	\n"
	"  if (magnify) lod_frac = max(lod_frac, uMinLod);		\n"
	"  if (uTextureDetail == 0)	{							\n"
	"    if (distant) lod_frac = 1.0;						\n"
	"    else if (magnify) lod_frac = 0.0;					\n"
	"  }													\n"
	"  if (magnify && (uTextureDetail == 1 || uTextureDetail == 3))			\n",
	"      lod_frac = 1.0 - lod_frac;						\n"
	"  if (uMaxTile == 0) {									\n"
	"    if (uEnableLod != 0 && uTextureDetail < 2)	\n"
	"      readtex1 = readtex0;								\n"
	"    return lod_frac;									\n"
	"  }													\n"
	"  if (uEnableLod == 0) return lod_frac;				\n"
	"														\n"
	"  lod_tile = min(lod_tile, fMaxTile);					\n"
	"  lowp float lod_tile_m1 = max(0.0, lod_tile - 1.0);	\n"
	"  lowp vec4 lodT = texture2DLodEXT(uTex1, vTexCoord1, lod_tile);	\n"
	"  lowp vec4 lodT_m1 = texture2DLodEXT(uTex1, vTexCoord1, lod_tile_m1);	\n",
	"  lowp vec4 lodT_p1 = texture2DLodEXT(uTex1, vTexCoord1, lod_tile + 1.0);	\n"
	"  if (lod_tile < 1.0) {								\n"
	"    if (magnify) {									\n"
	"      if (uTextureDetail == 0) readtex1 = readtex0;	\n"
	"    } else {											\n"
	"      if (uTextureDetail > 1) {				\n"
	"        readtex0 = lodT;								\n"
	"        readtex1 = lodT_p1;							\n"
	"      }												\n"
	"    }													\n"
	"  } else {												\n"
	"    if (uTextureDetail > 1) {							\n"
	"      readtex0 = lodT;									\n"
	"      readtex1 = lodT_p1;								\n"
	"    } else {											\n",
	"      readtex0 = lodT_m1;								\n"
	"      readtex1 = lodT;									\n"
	"    }													\n"
	"  }													\n"
	"  return lod_frac;										\n"
	"}														\n",
	NULL
};

static const char *const mipmap_fake[] = {
	"uniform lowp int uMaxTile;			\n"
	"uniform mediump float uMinLod;		\n"
	"														\n"
	"mediump float mipmap(out lowp vec4 readtex0, out lowp vec4 readtex1) {	\n"
	"  readtex0 = texture(uTex0, vTexCoord0);				\n"
	"  readtex1 = texture(uTex1, vTexCoord1);				\n"
	"  if (uMaxTile == 0) return 1.0;						\n"
	"  return uMinLod;										\n"
	"}														\n",
	NULL
};

static const char *const mipmap_3point[] = {
	"#define TEX_OFFSET_NORMAL(off, tex, texCoord, lod) texture(tex, texCoord - (off)/texSize)			\n"
	"#define TEX_OFFSET_MIPMAP(off, tex, texCoord, lod) textureLod(tex, texCoord - (off)/texSize, lod)	\n"
	"#define READ_TEX_NORMAL(name, tex, texCoord, lod)											\\\n"
	"  {																								\\\n"
	"  mediump vec2 texSize = vec2(textureSize(tex, int(lod)));											\\\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));										\\\n"
	"  offset -= step(1.0, offset.x + offset.y);														\\\n",
	"  lowp vec4 c0 = TEX_OFFSET_NORMAL(offset, tex, texCoord, lod);									\\\n"
	"  lowp vec4 c1 = TEX_OFFSET_NORMAL(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord, lod);	\\\n"
	"  lowp vec4 c2 = TEX_OFFSET_NORMAL(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord, lod);	\\\n"
	"  name = c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0); 										\\\n"
	"  }																								\n"
	"#define READ_TEX_MIPMAP(name, tex, texCoord, lod)													\\\n"
	"  {																								\\\n",
	"  mediump vec2 texSize = vec2(textureSize(tex, int(lod)));											\\\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));										\\\n"
	"  offset -= step(1.0, offset.x + offset.y);														\\\n"
	"  lowp vec4 c0 = TEX_OFFSET_MIPMAP(offset, tex, texCoord, lod);									\\\n"
	"  lowp vec4 c1 = TEX_OFFSET_MIPMAP(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord, lod);	\\\n"
	"  lowp vec4 c2 = TEX_OFFSET_MIPMAP(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord, lod);	\\\n",
	"  name = c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0); 										\\\n"
	"  }																								\n",
	NULL
};

static const char *const mipmap_fetch[] = {
	"#define TEX_FETCH_NORMAL(tex, texCoord, lod) texture(tex, texCoord)									\n"
	"#define TEX_FETCH_MIPMAP(tex, texCoord, lod) textureLod(tex, texCoord, lod)							\n"
	"#define READ_TEX_NORMAL(name, tex, texCoord, lod) name = TEX_FETCH_NORMAL(tex, texCoord, lod)	\n"
	"#define READ_TEX_MIPMAP(name, tex, texCoord, lod) name = TEX_FETCH_MIPMAP(tex, texCoord, lod)	\n",
	NULL
};

static const char *const mipmap[] = {
	"uniform lowp int uEnableLod;		\n"
	"uniform mediump float uMinLod;		\n"
	"uniform lowp int uMaxTile;			\n"
	"uniform lowp int uTextureDetail;	\n"
	"																		\n"
	"mediump float mipmap(out lowp vec4 readtex0, out lowp vec4 readtex1) {	\n"
	"  READ_TEX_NORMAL(readtex0, uTex0, vTexCoord0, 0.0);					\n"
	"  READ_TEX_MIPMAP(readtex1, uTex1, vTexCoord1, 0.0);					\n"
	"																		\n"
	"  mediump float fMaxTile = float(uMaxTile);							\n"
	"  mediump vec2 dx = abs(dFdx(vLodTexCoord));							\n"
	"  dx *= uScreenScale;													\n",
	"  mediump float lod = max(dx.x, dx.y);									\n"
	"  bool magnify = lod < 1.0;											\n"
	"  mediump float lod_tile = magnify ? 0.0 : floor(log2(floor(lod)));	\n"
	"  bool distant = lod > 128.0 || lod_tile >= fMaxTile;					\n"
	"  mediump float lod_frac = fract(lod/pow(2.0, lod_tile));				\n"
	"  if (magnify) lod_frac = max(lod_frac, uMinLod);						\n"
	"  if (uTextureDetail == 0)	{											\n"
	"    if (distant) lod_frac = 1.0;										\n"
	"    else if (magnify) lod_frac = 0.0;									\n"
	"  }																	\n",
	"  if (magnify && ((uTextureDetail & 1) != 0))							\n"
	"      lod_frac = 1.0 - lod_frac;										\n"
	"  if (uMaxTile == 0) {													\n"
	"    if (uEnableLod != 0) {												\n"
	"      if ((uTextureDetail & 2) == 0) readtex1 = readtex0;				\n"
	"      else if (!magnify) readtex0 = readtex1;							\n"
	"    }																	\n"
	"    return lod_frac;													\n"
	"  }																	\n"
	"  if (uEnableLod == 0) return lod_frac;								\n"
	"																		\n"
	"  lod_tile = min(lod_tile, fMaxTile - 1.0);							\n",
	"  lowp float lod_tile_m1 = max(0.0, lod_tile - 1.0);					\n"
	"  lowp float lod_tile_p1 = min(fMaxTile - 1.0, lod_tile + 1.0);		\n"
	"  lowp vec4 lodT, lodT_m1, lodT_p1;									\n"
	"  READ_TEX_MIPMAP(lodT, uTex1, vTexCoord1, lod_tile);					\n"
	"  READ_TEX_MIPMAP(lodT_m1, uTex1, vTexCoord1, lod_tile_m1);			\n"
	"  READ_TEX_MIPMAP(lodT_p1, uTex1, vTexCoord1, lod_tile_p1);			\n"
	"  if (lod_tile < 1.0) {												\n"
	"    if (magnify) {														\n"
	"      if (uTextureDetail == 0) readtex1 = readtex0;					\n"
	"    } else {															\n",
	"      if ((uTextureDetail & 2) != 0 ) {								\n"
	"        readtex0 = lodT;												\n"
	"        readtex1 = lodT_p1;											\n"
	"      }																\n"
	"    }																	\n"
	"  } else {																\n"
	"    if ((uTextureDetail & 2) != 0 ) {									\n"
	"      readtex0 = lodT;													\n"
	"      readtex1 = lodT_p1;												\n"
	"    } else {															\n"
	"      readtex0 = lodT_m1;												\n"
	"      readtex1 = lodT;													\n"
	"    }																	\n"
	"  }																	\n"
	"  return lod_frac;														\n",
	"}																		\n",
	NULL
};

static const char *const calc_light[] = {
	"uniform mediump vec3 uLightDirection[8];	\n"
	"uniform lowp vec3 uLightColor[8];			\n"
	"void calc_light(in lowp float fLights, in lowp vec3 input_color, out lowp vec3 output_color) {\n"
	"  output_color = input_color;									\n"
	"  lowp int nLights = int(floor(fLights + 0.5));				\n"
	"  if (nLights == 0)											\n"
	"     return;													\n"
	"  output_color = uLightColor[nLights];							\n"
	"  mediump float intensity;										\n"
	"  for (int i = 0; i < nLights; i++)	{						\n",
	"    intensity = max(dot(input_color, uLightDirection[i]), 0.0);\n"
	"    output_color += intensity*uLightColor[i];					\n"
	"  };															\n"
	"  output_color = clamp(output_color, 0.0, 1.0);				\n"
	"}																\n",
	NULL
};

static const char *const readtex_gles2_yuv[] = {
	"lowp vec4 YUV_Convert(in sampler2D tex, in highp vec2 texCoord, in lowp int convert, in lowp int format, in lowp vec4 prev)	\n"
	"{																	\n"
	"  lowp vec4 texColor;												\n"
	"  if (convert != 0) texColor = prev;								\n"
	"  else texColor = texture2D(tex, texCoord);						\n"
	"  mediump ivec4 icolor = ivec4(texColor*255.0);					\n"
	"  if (format == 1)													\n"
	"    icolor.rg -= 128;												\n"
	"  mediump ivec4 iconvert;											\n"
	"  iconvert.r = icolor.b + (uConvertParams[0]*icolor.g + 128)/256;	\n",
	"  iconvert.g = icolor.b + (uConvertParams[1]*icolor.r + uConvertParams[2]*icolor.g + 128)/256;	\n"
	"  iconvert.b = icolor.b + (uConvertParams[3]*icolor.r + 128)/256;	\n"
	"  iconvert.a = icolor.b;											\n"
	"  return vec4(iconvert)/255.0;										\n"
	"  }																\n",
	NULL
};

static const char *const readtex_gles2_3point[] = {
	"uniform mediump vec2 uTextureSize[2];										\n"
	"#define TEX_OFFSET(off) texture2D(tex, texCoord - (off)/texSize)			\n"
	"lowp vec4 TextureFilter(in sampler2D tex, in highp vec2 texCoord)		\n"
	"{																			\n"
	"  mediump vec2 texSize;													\n"
	"  if (nCurrentTile == 0)													\n"
	"    texSize = uTextureSize[0];												\n"
	"  else																		\n"
	"    texSize = uTextureSize[1];												\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));				\n"
	"  offset -= step(1.0, offset.x + offset.y);								\n",
	"  lowp vec4 c0 = TEX_OFFSET(offset);										\n"
	"  lowp vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));	\n"
	"  lowp vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));	\n"
	"  return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);				\n"
	"}																			\n",
	NULL
};

static const char *const readtex_gles2_bilinear[] = {
	"uniform mediump vec2 uTextureSize[2];										\n"
	"#define TEX_OFFSET(off) texture2D(tex, texCoord - (off)/texSize)			\n"
	"lowp vec4 TextureFilter(in sampler2D tex, in highp vec2 texCoord)		\n"
	"{																			\n"
	"  mediump vec2 texSize;													\n"
	"  if (nCurrentTile == 0)													\n"
	"    texSize = uTextureSize[0];												\n"
	"  else																		\n"
	"    texSize = uTextureSize[1];												\n"
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));				\n"
	"  offset -= step(1.0, offset.x + offset.y);								\n",
	"  lowp vec4 zero = vec4(0.0);												\n"
	"																			\n"
	"  lowp vec4 p0q0 = TEX_OFFSET(offset);										\n"
	"  lowp vec4 p1q0 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));	\n"
	"																			\n"
	"  lowp vec4 p0q1 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));	\n"
	"  lowp vec4 p1q1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y - sign(offset.y)));\n"
	"																			\n"
	"  mediump vec2 interpolationFactor = abs(offset);							\n",
	"  lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x ); 		\n"
	"  lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x ); 		\n"
	"  return mix( pInterp_q0, pInterp_q1, interpolationFactor.y ); 			\n"
	"}																			\n",
	NULL
};

static const char *const readtex_gles2[] = {
	"lowp vec4 readTex(in sampler2D tex, in highp vec2 texCoord, in lowp int fbMonochrome, in lowp int fbFixedAlpha)	\n"
	"{																			\n"
	"  lowp vec4 texColor;														\n"
	"  if (uTextureFilterMode == 0) texColor = texture2D(tex, texCoord);		\n"
	"  else texColor = TextureFilter(tex, texCoord);							\n"
	"  if (fbMonochrome == 1) texColor = vec4(texColor.r);						\n"
	"  else if (fbMonochrome == 2) 												\n"
	"    texColor.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), texColor.rgb));	\n",
	"  if (fbFixedAlpha == 1) texColor.a = 0.825;								\n"
	"  return texColor;															\n"
	"}																			\n",
	NULL
};

static const char *const readtex_ms[] = {
	"uniform lowp int uMSAASamples;												\n"
	"lowp vec4 sampleMS(in lowp sampler2DMS mstex, in mediump ivec2 ipos)		\n"
	"{																			\n"
	"  lowp vec4 texel = vec4(0.0);												\n"
	"  for (int i = 0; i < uMSAASamples; ++i)									\n"
	"    texel += texelFetch(mstex, ipos, i);									\n"
	"  return texel / float(uMSAASamples);										\n"
	"}																			\n"
	"																			\n"
	"lowp vec4 readTexMS(in lowp sampler2DMS mstex, in highp vec2 texCoord, in lowp int fbMonochrome, in lowp int fbFixedAlpha)	\n",
	"{																			\n"
	"  mediump ivec2 itexCoord;													\n"
	"  if (fbMonochrome == 3) {													\n"
	"    itexCoord = ivec2(gl_FragCoord.xy);									\n"
	"  } else {																	\n"
	"    mediump vec2 msTexSize = vec2(textureSize(mstex));						\n"
	"    itexCoord = ivec2(msTexSize * texCoord);								\n"
	"  }																		\n"
	"  lowp vec4 texColor = sampleMS(mstex, itexCoord);							\n"
	"  if (fbMonochrome == 1) texColor = vec4(texColor.r);						\n"
	"  else if (fbMonochrome == 2) 												\n",
	"    texColor.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), texColor.rgb));	\n"
	"  else if (fbMonochrome == 3) { 											\n"
	"    texColor.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), texColor.rgb));	\n"
	"    texColor.a = 0.0;														\n"
	"  }																		\n"
	"  if (fbFixedAlpha == 1) texColor.a = 0.825;								\n"
	"  return texColor;															\n"
	"}																			\n",
	NULL
};

static const char *const readtex_copy_gles2[] = {
	"lowp vec4 readTex(in sampler2D tex, in highp vec2 texCoord, in lowp int fbMonochrome, in lowp int fbFixedAlpha)	\n"
	"{																			\n"
	"  lowp vec4 texColor = texture2D(tex, texCoord);							\n"
	"  if (fbMonochrome == 1) texColor = vec4(texColor.r);						\n"
	"  else if (fbMonochrome == 2) 												\n"
	"    texColor.rgb = vec3(dot(vec3(0.2126, 0.7152, 0.0722), texColor.rgb));	\n"
	"  if (fbFixedAlpha == 1) texColor.a = 0.825;								\n"
	"  return texColor;															\n"
	"}																			\n",
	NULL
};

static const char *const depth_compare[] = {
	"uniform lowp int uEnableDepth;							\n"
	"uniform lowp int uDepthMode;							\n"
	"uniform lowp int uEnableDepthUpdate;					\n"
	"uniform mediump float uDeltaZ;							\n"
	"bool depth_compare(highp float curZ)					\n"
	"{														\n"
	"  if (uEnableDepth == 0) return true;					\n",
	NULL
};

static const char *const depth_compare_image_load[] = {
	"  ivec2 coord = ivec2(gl_FragCoord.xy);				\n"
	"  highp vec4 depthZ = imageLoad(uDepthImageZ,coord);	\n"
	"  highp vec4 depthDeltaZ = imageLoad(uDepthImageDeltaZ,coord);\n",
	NULL
};

static const char *const depth_compare_body[] = {
	"  highp float bufZ = depthZ.r;							\n"
	"  highp float dz, dzMin;								\n"
	"  if (uDepthSource == 1) {								\n"
	"     dzMin = dz = uDeltaZ;								\n"
	"  } else {												\n"
	"    dz = 4.0*fwidth(curZ);								\n"
	"    dzMin = min(dz, depthDeltaZ.r);					\n"
	"  }													\n"
	"  bool bInfront = curZ < bufZ;							\n"
	"  bool bFarther = (curZ + dzMin) >= bufZ;				\n"
	"  bool bNearer = (curZ - dzMin) <= bufZ;				\n"
	"  bool bMax = bufZ == 1.0;								\n"
	"  bool bRes = false;									\n"
	"  switch (uDepthMode) {								\n"
	"     case 1:											\n",
	"       bRes = bMax || bNearer;							\n"
	"       break;											\n"
	"     case 0:											\n"
	"     case 2:											\n"
	"       bRes = bMax || bInfront;						\n"
	"       break;											\n"
	"     case 3:											\n"
	"       bRes = bFarther && bNearer && !bMax;			\n"
	"       break;											\n"
	"  }													\n"
	"  bRes = bRes || (uEnableDepthCompare == 0);			\n"
	"  if (uEnableDepthUpdate != 0 && bRes) {				\n",
	NULL
};

static const char *const depth_compare_image_store[] = {
	"    highp vec4 depthOutZ = vec4(curZ, 1.0, 1.0, 1.0);		\n"
	"    highp vec4 depthOutDeltaZ = vec4(dz, 1.0, 1.0, 1.0);	\n"
	"    imageStore(uDepthImageZ, coord, depthOutZ);			\n"
	"    imageStore(uDepthImageDeltaZ, coord, depthOutDeltaZ);	\n",
	NULL
};

static const char *const depth_compare_fetch[] = {
	"    depthZ.r = curZ;									\n"
	"    depthDeltaZ.r = dz;								\n",
	NULL
};

static const char *const depth_compare_end[] = {
	"  }													\n"
	"  return bRes;											\n"
	"}														\n",
	NULL
};

static const char *const depth_render[] = {
	"bool depth_render(highp float Z, highp float curZ)						\n"
	"{														\n"
	"  ivec2 coord = ivec2(gl_FragCoord.xy);				\n"
	"  if (uEnableDepthCompare != 0) {						\n",
	NULL
};

static const char *const depth_render_image_load[] = {
	"    highp vec4 depthZ = imageLoad(uDepthImageZ,coord);	\n",
	NULL
};

static const char *const depth_render_body[] = {
	"    highp float bufZ = depthZ.r;						\n"
	"    if (curZ >= bufZ) return false;					\n"
	"  }													\n",
	NULL
};

static const char *const depth_render_image_store[] = {
	"  highp vec4 depthOutZ = vec4(Z, 1.0, 1.0, 1.0);		\n"
	"  highp vec4 depthOutDeltaZ = vec4(0.0, 1.0, 1.0, 1.0);\n"
	"  imageStore(uDepthImageZ,coord, depthOutZ);			\n"
	"  imageStore(uDepthImageDeltaZ,coord, depthOutDeltaZ);	\n",
	NULL
};

static const char *const depth_render_fetch[] = {
	"  depthZ.r = Z;	\n"
	"  depthDeltaZ.r = 0.0;	\n",
	NULL
};

static const char *const depth_render_end[] = {
	"  return true;											\n"
	"}														\n",
	NULL
};

static const char *const noise_gles2[] = {
	"uniform sampler2D uTexNoise;							\n"
	"uniform mediump vec2 uNoiseOffset;						\n"
	"lowp float snoise()									\n"
	"{														\n"
	"  mediump vec2 texSize = vec2(" NOISE_STR(NOISE_TEX_WIDTH) ".0, " NOISE_STR(NOISE_TEX_HEIGHT) ".0);\n"
	"  mediump vec2 p = gl_FragCoord.xy/uScreenScale - uNoiseOffset;\n"
	"  p += texSize * vec2(lessThan(p, vec2(0.0)));\n"
	"  return texture2D(uTexNoise, fract(p/texSize)).r;\n"
	"}														\n",
	NULL
};

static const char *const noise[] = {
	"uniform sampler2D uTexNoise;		\n"
	"uniform mediump vec2 uNoiseOffset;					\n"
	"lowp float snoise()									\n"
	"{														\n"
	"  ivec2 coord = (ivec2(gl_FragCoord.xy/uScreenScale) + ivec2(uNoiseOffset)) % ivec2(" NOISE_STR(NOISE_TEX_WIDTH) ", " NOISE_STR(NOISE_TEX_HEIGHT) ");\n"
	"  return texelFetch(uTexNoise, coord, 0).r;			\n"
	"}														\n",
	NULL
};

/*---------------Writing a shader-------------*/

/* a shader as the strings it is made of, in order; more than the largest
 * shader written takes */
#define GLI_GLSL_STRINGS 2048
typedef struct gli_glsl_text
{
	const GLchar *s[GLI_GLSL_STRINGS];
	GLsizei n;
} gli_glsl_text;

static void put(gli_glsl_text *_t, const char *_s)
{
	if (_t->n < GLI_GLSL_STRINGS)
		_t->s[_t->n++] = _s;
}

static void put_lines(gli_glsl_text *_t, const char *const *_lines)
{
	for (; *_lines != NULL; ++_lines)
		put(_t, *_lines);
}

static void put_text(gli_glsl_text *_t, const gli_glsl_text *_other)
{
	GLsizei i;
	for (i = 0; i < _other->n; ++i)
		put(_t, _other->s[i]);
}

/* a header, written once, as one string: the special shaders take it so */
typedef struct gli_glsl_header
{
	char s[1024];
	size_t len;
} gli_glsl_header;

static void header_put(gli_glsl_header *_h, const char *_s)
{
	const size_t n = strlen(_s);
	if (_h->len + n < sizeof(_h->s)) {
		memcpy(_h->s + _h->len, _s, n + 1);
		_h->len += n;
	}
}

static void header_lines(gli_glsl_header *_h, const char *const *_lines)
{
	for (; *_lines != NULL; ++_lines)
		header_put(_h, *_lines);
}

/* "#version 330 core \n" and the like */
static void header_version(gli_glsl_header *_h, const gli_gl_info *_info, const char *_profile)
{
	char b[64];
	sprintf(b, "#version %d%d0 %s \n", (int)_info->majorVersion, (int)_info->minorVersion, _profile);
	header_put(_h, b);
}

typedef struct uber_variant
{
	GLuint handle;
	gli_cprog *program;
	struct async_program *owner;
	GLint opsLoc[16];
	GLint flagsLoc;
	u32 inputs;
	u64 key;
} uber_variant;

struct gli_glsl_builder
{
	const gli_gl_info *info;
	gli_gl_cache1 *useProgram;
	u32 optionsBits;
	/* what the shader being written is for: its key's cycle type and
	 * texture convert mode */
	u32 cycleType;
	u32 textureConvert;
	bool uberOff;
	bool uberOnly;
	int uberDelay;
	GLuint vertexRect;
	GLuint vertexTriangle;
	GLuint vertexTexturedRect;
	GLuint vertexTexturedTriangle;
	gli_glsl_header vertexHeader;
	gli_glsl_header fragmentHeader;
	gli_glsl_header fragmentEnd;
	/* [rect][cycle: 1, 2][bilerp][textured][lod][hardware lighting] */
	uber_variant uber[2][2][4][2][2][2];
	/* the shader being written, and its combiner's part */
	gli_glsl_text text;
	gli_glsl_text combiner;
};

/* the key's texture convert mode */
#define BILERP1(_b) (((_b)->textureConvert & 1) != 0)
#define BILERP0(_b) (((_b)->textureConvert & 2) != 0)
#define YUV_CONVERSION(_b) (((_b)->textureConvert & 3) != 3)
#define TEXTURE_FILTERING(_b) (((_b)->textureConvert & 3) != 0)

/*---------------The parts-------------*/

/* The RDP interpolates shade, fog included, in screen space, not with
 * perspective like the texture coordinates: where the language has no
 * noperspective it stays with perspective */
static const char *shade_interpolation(const gli_gl_info *_info)
{
	if (_info->isGLES2 || (_info->isGLESX && !_info->noPerspective))
		return "# define SHADE\n";
	return "# define SHADE noperspective\n";
}

static void make_vertex_header(gli_glsl_header *_h, const gli_gl_info *_info)
{
	if (_info->isGLES2) {
		header_lines(_h, vh_gles2);
	} else if (_info->isGLESX) {
		header_version(_h, _info, "es");
		header_put(_h, "# define IN in\n# define OUT out\n");
		if (_info->noPerspective)
			header_put(_h, "#extension GL_NV_shader_noperspective_interpolation : enable\n"
				"noperspective OUT highp float vZCoord;\nuniform lowp int uClampMode;\n");
	} else {
		header_version(_h, _info, "core");
		header_put(_h, "# define IN in\n# define OUT out\n");
	}
	header_put(_h, shade_interpolation(_info));
}

static void put_vertex_end(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (!_info->isGLESX)
		put_lines(_t, vs_end_clip);
	else if (config.generalEmulation.enableFragmentDepthWrite != 0 && _info->noPerspective)
		put_lines(_t, vs_end_zcoord);
	put(_t, "} \n");
}

static void make_fragment_header(gli_glsl_header *_h, const gli_gl_info *_info)
{
	static const char interlockNV[] = "#extension GL_NV_fragment_shader_interlock : enable\n"
		"layout(pixel_interlock_ordered) in;\n";
	static const char fetch[] = "#extension GL_EXT_shader_framebuffer_fetch : enable\n";
	static const char defines[] = "# define IN in\n# define OUT out\n# define texture2D texture\n";
	if (_info->isGLES2) {
		header_lines(_h, fh_gles2_version);
		if (config.generalEmulation.enableLOD)
			header_lines(_h, fh_gles2_lod);
		header_lines(_h, fh_gles2);
	} else if (_info->isGLESX) {
		header_version(_h, _info, "es");
		if (_info->noPerspective)
			header_put(_h, "#extension GL_NV_shader_noperspective_interpolation : enable\n");
		if (config.frameBufferEmulation.N64DepthCompare != 0) {
			if (_info->imageTextures && _info->fragment_interlockNV)
				header_put(_h, interlockNV);
			else if (_info->ext_fetch)
				header_put(_h, fetch);
		}
		header_put(_h, defines);
	} else {
		header_version(_h, _info, "core");
		if (config.frameBufferEmulation.N64DepthCompare != 0) {
			if (_info->imageTextures) {
				if (_info->majorVersion * 10 + _info->minorVersion < 42)
					header_put(_h, "#extension GL_ARB_shader_image_load_store : enable\n"
						"#extension GL_ARB_shading_language_420pack : enable\n");
				if (_info->fragment_interlock)
					header_put(_h, "#extension GL_ARB_fragment_shader_interlock : enable\n"
						"layout(pixel_interlock_ordered) in;\n");
				else if (_info->fragment_interlockNV)
					header_put(_h, interlockNV);
				else if (_info->fragment_ordering)
					header_put(_h, "#extension GL_INTEL_fragment_shader_ordering : enable\n");
			} else if (_info->ext_fetch)
				header_put(_h, fetch);
		}
		header_put(_h, defines);
	}
	header_put(_h, shade_interpolation(_info));
}

static void put_blender1(gli_glsl_text *_t)
{
	put_lines(_t, blender1);
	/* Workaround for Intel drivers for Mac, issue #1601 */
#if defined(OS_MAC_OS_X)
	put_lines(_t, blender1_mac);
#else
	put_lines(_t, blender1_pc);
#endif
	put_lines(_t, blender_end);
}

static void put_blender2(gli_glsl_text *_t)
{
	put_lines(_t, blender2);
#if defined(OS_MAC_OS_X)
	put_lines(_t, blender2_mac);
#else
	put_lines(_t, blender2_pc);
#endif
	put_lines(_t, blender_end);
}

static void put_call_dither(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (!_info->isGLES2 && config.generalEmulation.enableNoise != 0)
		put_lines(_t, call_dither);
}

static void put_tex_vars(gli_glsl_text *_t, const gli_gl_info *_info)
{
	put_lines(_t, tex_vars);
	if (config.generalEmulation.enableLegacyBlending == 0)
		put_lines(_t, vars_blend_mux);
	if (!_info->isGLES2) {
		put_lines(_t, vars_gl);
		if (config.frameBufferEmulation.N64DepthCompare != 0)
			put_lines(_t, vars_depth_compare);
	} else
		put_lines(_t, vars_gles2);
	if (config.video.multisampling > 0)
		put_lines(_t, tex_vars_ms);
	put_lines(_t, tex_vars_in);
	if (config.frameBufferEmulation.N64DepthCompare != 0 && _info->ext_fetch)
		put_lines(_t, vars_fetch_out);
	else
		put_lines(_t, vars_out);
}

static void put_notex_vars(gli_glsl_text *_t, const gli_gl_info *_info)
{
	put_lines(_t, notex_vars);
	if (config.generalEmulation.enableLegacyBlending != 0)
		put_lines(_t, notex_vars_fog_usage);
	else
		put_lines(_t, vars_blend_mux);
	if (!_info->isGLES2) {
		put_lines(_t, vars_gl);
		if (config.frameBufferEmulation.N64DepthCompare != 0)
			put_lines(_t, vars_depth_compare);
	} else
		put_lines(_t, vars_gles2);
	put_lines(_t, notex_vars_in);
	if (config.frameBufferEmulation.N64DepthCompare != 0 && _info->ext_fetch)
		put_lines(_t, vars_fetch_out);
	else
		put_lines(_t, vars_out);
}

static void put_h_write_depth(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (_info->isGLES2)
		return;
	if (_info->isGLESX && _info->noPerspective)
		put_lines(_t, h_write_depth_nopersp);
	put_lines(_t, h_write_depth);
}

static void put_h_read_ms_tex(gli_glsl_builder *_b, gli_glsl_text *_t)
{
	if (!_b->info->isGLES2 && config.video.multisampling > 0 &&
		(_b->cycleType == G_CYC_COPY || TEXTURE_FILTERING(_b)))
		put_lines(_t, h_read_ms_tex);
}

static void put_h_dither(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (!_info->isGLES2 && config.generalEmulation.enableNoise != 0)
		put_lines(_t, h_dither);
}

static void put_h_depth_compare(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (config.frameBufferEmulation.N64DepthCompare == 0)
		return;
	put_lines(_t, h_depth_compare);
	if (_info->imageTextures)
		put_lines(_t, h_depth_compare_image);
}

static void put_h_read_tex(gli_glsl_builder *_b, gli_glsl_text *_t)
{
	if (!_b->info->isGLES2) {
		if (TEXTURE_FILTERING(_b)) {
			put_lines(_t, h_filter_mode);
			switch (config.texture.bilinearMode + config.texture.enableHalosRemoval * 2) {
			case BILINEAR_3POINT:
				/* 3 point texture filtering.
				 * Original author: ArthurCarvalho
				 * GLSL implementation: twinaphex, mupen64plus-libretro project. */
				put_lines(_t, h_filter_3point);
				break;
			case BILINEAR_STANDARD:
				put_lines(_t, h_filter_standard);
				break;
			case BILINEAR_3POINT_WITH_COLOR_BLEEDING:
				put_lines(_t, h_filter_3point_bleed);
				break;
			case BILINEAR_STANDARD_WITH_COLOR_BLEEDING_AND_PREMULTIPLIED_ALPHA:
				put_lines(_t, h_filter_standard_bleed);
				break;
			}
			put_lines(_t, h_read_tex);
		}
		if (YUV_CONVERSION(_b))
			put_lines(_t, h_yuv);
	} else {
		if (TEXTURE_FILTERING(_b))
			put_lines(_t, h_gles2_filter);
		if (YUV_CONVERSION(_b))
			put_lines(_t, h_gles2_yuv);
	}
}

static void put_main(gli_glsl_text *_t, const gli_gl_info *_info, bool _twoCycles)
{
	put_lines(_t, main_begin);
	if (!_info->isGLES2)
		put_lines(_t, _twoCycles ? main2_depth : main_depth);
	put_lines(_t, _twoCycles ? main2_vars : main_vars);
}

static void put_read_tex_copy(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (_info->isGLES2)
		put_lines(_t, read_tex_copy_gles2);
	else if (config.video.multisampling > 0)
		put_lines(_t, read_tex_copy_ms);
	else
		put_lines(_t, read_tex_copy);
}

static void put_read_tex0(gli_glsl_builder *_b, gli_glsl_text *_t)
{
	if (_b->info->isGLES2) {
		put_lines(_t, read_tex0_gles2);
		put_lines(_t, BILERP0(_b) ? read_tex0_gles2_bilerp : read_tex0_gles2_yuv);
	} else if (!BILERP0(_b))
		put_lines(_t, read_tex0_yuv);
	else
		put_lines(_t, config.video.multisampling > 0 ? read_tex0_ms : read_tex0);
}

static void put_read_tex1(gli_glsl_builder *_b, gli_glsl_text *_t)
{
	if (_b->info->isGLES2) {
		put_lines(_t, read_tex1_gles2);
		put_lines(_t, BILERP1(_b) ? read_tex1_gles2_bilerp : read_tex1_gles2_yuv);
	} else if (!BILERP1(_b))
		put_lines(_t, read_tex1_yuv);
	else
		put_lines(_t, config.video.multisampling > 0 ? read_tex1_ms : read_tex1);
}

static void put_call_n64_depth(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (config.frameBufferEmulation.N64DepthCompare == 0)
		return;
	put_lines(_t, n64_depth_begin);
	if (_info->imageTextures) {
		if (_info->fragment_interlock)
			put_lines(_t, n64_depth_interlock_arb);
		else if (_info->fragment_interlockNV)
			put_lines(_t, n64_depth_interlock_nv);
		else if (_info->fragment_ordering)
			put_lines(_t, n64_depth_ordering_intel);
	}
	put_lines(_t, n64_depth_call);
	if (_info->imageTextures) {
		if (_info->fragment_interlock)
			put_lines(_t, n64_depth_end_arb);
		else if (_info->fragment_interlockNV)
			put_lines(_t, n64_depth_end_nv);
	}
	put_lines(_t, n64_depth_discard);
}

static void put_noise(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (config.generalEmulation.enableNoise == 0)
		put_lines(_t, noise_dummy);
	else
		put_lines(_t, _info->isGLES2 ? noise_gles2 : noise);
}

static void put_write_depth(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (_info->isGLES2)
		return;
	if (config.generalEmulation.enableFragmentDepthWrite == 0 &&
		config.frameBufferEmulation.N64DepthCompare == 0)
		put_lines(_t, write_depth_dummy);
	/* No special case for RE2 here.
	 *
	 * Its branch converted the fragment depth into the N64 exponent/mantissa
	 * form through the ZLUT, to match a depth map that was uploaded in that
	 * form. TextureCache::_loadDepthTexture now decodes the map to linear as
	 * it loads, so both sides are already in the same space and the conversion
	 * is unnecessary. Dropping the branch also restores the uDepthSource /
	 * uPrimDepth handling the game was missing. */
	else if (_info->isGLESX && _info->noPerspective)
		put_lines(_t, write_depth_nopersp);
	else
		put_lines(_t, write_depth);
}

static void put_mipmap(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (_info->isGLES2)
		put_lines(_t, config.generalEmulation.enableLOD == 0 ? mipmap_gles2_fake : mipmap_gles2);
	else if (config.generalEmulation.enableLOD == 0)
		put_lines(_t, mipmap_fake);
	else {
		put_lines(_t, config.texture.bilinearMode == BILINEAR_3POINT ? mipmap_3point : mipmap_fetch);
		put_lines(_t, mipmap);
	}
}

static void put_readtex(gli_glsl_builder *_b, gli_glsl_text *_t)
{
	if (_b->info->isGLES2) {
		if (YUV_CONVERSION(_b))
			put_lines(_t, readtex_gles2_yuv);
		if (TEXTURE_FILTERING(_b)) {
			/* 3 point or bilinear filtering */
			put_lines(_t, config.texture.bilinearMode == BILINEAR_3POINT ? readtex_gles2_3point :
				readtex_gles2_bilinear);
			put_lines(_t, readtex_gles2);
		}
	} else if (config.video.multisampling > 0 && TEXTURE_FILTERING(_b))
		put_lines(_t, readtex_ms);
}

static void put_readtex_copy(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (_info->isGLES2)
		put_lines(_t, readtex_copy_gles2);
	else if (config.video.multisampling > 0)
		put_lines(_t, readtex_ms);
}

static void put_n64_depth_compare(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (config.frameBufferEmulation.N64DepthCompare == 0)
		return;
	put_lines(_t, depth_compare);
	if (_info->imageTextures)
		put_lines(_t, depth_compare_image_load);
	put_lines(_t, depth_compare_body);
	if (_info->imageTextures)
		put_lines(_t, depth_compare_image_store);
	else if (_info->ext_fetch)
		put_lines(_t, depth_compare_fetch);
	put_lines(_t, depth_compare_end);
}

static void put_n64_depth_render(gli_glsl_text *_t, const gli_gl_info *_info)
{
	if (config.frameBufferEmulation.N64DepthCompare == 0)
		return;
	put_lines(_t, depth_render);
	if (_info->imageTextures)
		put_lines(_t, depth_render_image_load);
	put_lines(_t, depth_render_body);
	if (_info->imageTextures)
		put_lines(_t, depth_render_image_store);
	else if (_info->ext_fetch)
		put_lines(_t, depth_render_fetch);
	put_lines(_t, depth_render_end);
}

/*---------------The combiner-------------*/

static const char *const s_colorInput[] = {
	"combined_color.rgb",
	"readtex0.rgb",
	"readtex1.rgb",
	"uPrimColor.rgb",
	"vec_color.rgb",
	"uEnvColor.rgb",
	"uCenterColor.rgb",
	"uScaleColor.rgb",
	"combined_color.a",
	"vec3(readtex0.a)",
	"vec3(readtex1.a)",
	"vec3(uPrimColor.a)",
	"vec3(vec_color.a)",
	"vec3(uEnvColor.a)",
	"vec3(lod_frac)",
	"vec3(uPrimLod)",
	"vec3(0.5 + 0.5*snoise())",
	"vec3(uK4)",
	"vec3(uK5)",
	"vec3(1.0)",
	"vec3(0.0)"
};

static const char *const s_alphaInput[] = {
	"combined_color.a",
	"readtex0.a",
	"readtex1.a",
	"uPrimColor.a",
	"vec_color.a",
	"uEnvColor.a",
	"uCenterColor.a",
	"uScaleColor.a",
	"combined_color.a",
	"readtex0.a",
	"readtex1.a",
	"uPrimColor.a",
	"vec_color.a",
	"uEnvColor.a",
	"lod_frac",
	"uPrimLod",
	"0.5 + 0.5*snoise()",
	"uK4",
	"uK5",
	"1.0",
	"0.0"
};

/* a stage's expression, with the inputs it reads added to @_inputs */
static u32 put_stage(gli_glsl_text *_t, const CombinerStage *_stage, const char *const *_in, u32 _inputs)
{
	bool bracketOpen = false;
	int i;
	for (i = 0; i < _stage->numOps; ++i) {
		const char *in = _in[_stage->op[i].param1];
		switch (_stage->op[i].op) {
		case LOAD:
			put(_t, "(");
			put(_t, in);
			put(_t, " ");
			bracketOpen = true;
			break;
		case SUB:
			put(_t, "- ");
			put(_t, in);
			if (bracketOpen) {
				put(_t, ")");
				bracketOpen = false;
			}
			break;
		case ADD:
			put(_t, "+ ");
			put(_t, in);
			if (bracketOpen) {
				put(_t, ")");
				bracketOpen = false;
			}
			break;
		case MUL:
			if (bracketOpen) {
				put(_t, ")*");
				bracketOpen = false;
			} else
				put(_t, "*");
			put(_t, in);
			break;
		case INTER:
			put(_t, "mix(");
			put(_t, _in[_stage->op[0].param2]);
			put(_t, ",");
			put(_t, _in[_stage->op[0].param1]);
			put(_t, ",");
			put(_t, _in[_stage->op[0].param3]);
			put(_t, ")");
			break;
		}
	}
	if (bracketOpen)
		put(_t, ")");
	put(_t, ";\n");
	return gli_inputs_add_stage(_inputs, _stage);
}

/* the combiner's part of the key's fragment shader, into _b->combiner;
 * the inputs it reads */
static u32 compile_combiner(gli_glsl_builder *_b, u64 _key, Combiner *_color, Combiner *_alpha)
{
	gli_glsl_text *t = &_b->combiner;
	const u32 cycleType = _b->cycleType;
	gDPCombine combine;
	u32 inputs = 0;
	combine.mux = _key;
	t->n = 0;

	if (cycleType != G_CYC_2CYCLE) {
		gli_stage_correct_first(&_alpha->stage[0]);
		gli_stage_correct_first(&_color->stage[0]);
	}
	put(t, "  alpha1 = ");
	inputs = put_stage(t, &_alpha->stage[0], s_alphaInput, inputs);
	/* Simulate N64 color sign-extend. */
	if (gli_combined_alpha_c(cycleType, &combine))
		put_lines(t, sign_extend_alpha_c);
	else if (gli_combined_alpha_abd(cycleType, &combine))
		put_lines(t, sign_extend_alpha_abd);

	if (cycleType < G_CYC_FILL)
		put_lines(t, alpha_test);

	put(t, "  color1 = ");
	inputs = put_stage(t, &_color->stage[0], s_colorInput, inputs);
	if (gli_combined_color_c(cycleType, &combine))
		put_lines(t, sign_extend_color_c);
	else if (gli_combined_color_abd(cycleType, &combine))
		put_lines(t, sign_extend_color_abd);

	if (cycleType == G_CYC_2CYCLE) {
		put(t, "  combined_color = vec4(color1, alpha1);\n");
		if (_alpha->numStages == 2) {
			put(t, "  alpha2 = ");
			gli_stage_correct_second(&_alpha->stage[1]);
			inputs = put_stage(t, &_alpha->stage[1], s_alphaInput, inputs);
		} else
			put(t, "  alpha2 = alpha1;\n");

		put(t, "  if (uCvgXAlpha != 0 && alpha2 < 0.125) discard;\n");

		if (_color->numStages == 2) {
			put(t, "  color2 = ");
			gli_stage_correct_second(&_color->stage[1]);
			inputs = put_stage(t, &_color->stage[1], s_colorInput, inputs);
		} else
			put(t, "  color2 = color1;\n");

		put(t, "  lowp vec4 cmbRes = vec4(color2, alpha2);\n");
	} else {
		if (cycleType < G_CYC_FILL)
			put(t, "  if (uCvgXAlpha != 0 && alpha1 < 0.125) discard;\n");
		put(t, "  lowp vec4 cmbRes = vec4(color1, alpha1);\n");
	}

	/* Simulate N64 color clamp. */
	if (cycleType <= G_CYC_2CYCLE)
		put_lines(t, clamp);
	else
		put(t, "  lowp vec4 clampedColor = clamp(cmbRes, 0.0, 1.0);\n");

	if (cycleType <= G_CYC_2CYCLE)
		put_call_dither(t, _b->info);

	if (config.generalEmulation.enableLegacyBlending == 0) {
		if (cycleType <= G_CYC_2CYCLE)
			put_blender1(t);
		if (cycleType == G_CYC_2CYCLE)
			put_blender2(t);
		put(t, "  fragColor = clampedColor;\n");
	} else {
		put(t, "  fragColor = clampedColor;\n");
		put_lines(t, legacy_blender);
	}
	return inputs;
}

/* the key's fragment shader, into _b->text, with _b->combiner */
static void fragment_source(gli_glsl_builder *_b, u32 _inputs, bool _lod, bool _textures, bool _hwl,
	const char *_uberDeclarations)
{
	gli_glsl_text *t = &_b->text;
	const gli_gl_info *info = _b->info;
	const u32 cycleType = _b->cycleType;
	t->n = 0;

	/* Write headers */
	put(t, _b->fragmentHeader.s);

	if (_textures) {
		put_tex_vars(t, info);

		if (cycleType == G_CYC_2CYCLE && config.generalEmulation.enableLegacyBlending == 0)
			put(t, "uniform lowp ivec4 uBlendMux2;\nuniform lowp int uForceBlendCycle2;\n");

		if (cycleType <= G_CYC_2CYCLE)
			put_h_dither(t, info);
		put_lines(t, h_noise);
		put_h_write_depth(t, info);
		put_h_depth_compare(t, info);
		put_h_read_ms_tex(_b, t);
		if (_lod)
			put_lines(t, h_mipmap);
		else if (cycleType < G_CYC_COPY)
			put_h_read_tex(_b, t);
		else
			put_lines(t, info->isGLES2 ? h_read_tex_copy_gles2 : h_read_tex_copy);
	} else {
		put_notex_vars(t, info);

		if (cycleType == G_CYC_2CYCLE && config.generalEmulation.enableLegacyBlending == 0)
			put(t, "uniform lowp ivec4 uBlendMux2;\nuniform lowp int uForceBlendCycle2;\n");

		if (cycleType <= G_CYC_2CYCLE)
			put_h_dither(t, info);
		put_lines(t, h_noise);
		put_h_write_depth(t, info);
		put_h_depth_compare(t, info);
	}

	if (_hwl)
		put_lines(t, h_calc_light);

	if (_uberDeclarations != NULL)
		put(t, _uberDeclarations);

	/* Write body */
	put_main(t, info, cycleType == G_CYC_2CYCLE);

	if (cycleType <= G_CYC_2CYCLE && config.generalEmulation.enableLegacyBlending == 0)
		put_lines(t, blend_mux);

	if (_textures) {
		if (_lod)
			put_lines(t, read_tex_mipmap);
		else if (cycleType < G_CYC_COPY) {
			if (gli_inputs_tile(_inputs, 0))
				put_read_tex0(_b, t);
			else
				put(t, "  lowp vec4 readtex0;\n");
			if (gli_inputs_tile(_inputs, 1))
				put_read_tex1(_b, t);
		} else
			put_read_tex_copy(t, info);
	}

	if (_hwl)
		put(t, "  calc_light(vNumLights, vShadeColor.rgb, input_color);\n");
	else
		put(t, "  input_color = vShadeColor.rgb;\n");

	put(t, "  vec_color = vec4(input_color, vShadeColor.a);\n");
	put_text(t, &_b->combiner);
	put(t, "\n");

	if (config.frameBufferEmulation.N64DepthCompare != 0)
		put_call_n64_depth(t, info);
	else if (config.generalEmulation.enableFragmentDepthWrite != 0)
		put_lines(t, render_target);

	/* End of Main() function */
	put(t, _b->fragmentEnd.s);

	/* Write other functions */
	if (_hwl)
		put_lines(t, calc_light);

	if (_textures) {
		if (_lod)
			put_mipmap(t, info);
		else if (cycleType < G_CYC_COPY)
			put_readtex(_b, t);
		else
			put_readtex_copy(t, info);
	}

	put_noise(t, info);

	if (cycleType <= G_CYC_2CYCLE && !info->isGLES2 && config.generalEmulation.enableNoise != 0)
		put_lines(t, dither);

	put_write_depth(t, info);
	put_n64_depth_compare(t, info);
	put_n64_depth_render(t, info);
}

/* Compiles and links _b->text, not asking how that went: asking is what
 * waits for a driver compiling on its own threads. */
static GLuint start_program(gli_glsl_builder *_b, bool _rect, bool _textures)
{
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	GLuint program;
	glShaderSource(fragmentShader, _b->text.n, _b->text.s, NULL);
	glCompileShader(fragmentShader);

	program = glCreateProgram();
	gli_glsl_locate_attributes(program, _rect, _textures);
	if (_rect)
		glAttachShader(program, _textures ? _b->vertexTexturedRect : _b->vertexRect);
	else
		glAttachShader(program, _textures ? _b->vertexTexturedTriangle : _b->vertexTriangle);
	glAttachShader(program, fragmentShader);
	if (gli_combiner_shader_cache_supported()) {
		if (IS_GL_FUNCTION_VALID(glProgramParameteri))
			glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	}
	glLinkProgram(program);
	glDeleteShader(fragmentShader);
	return program;
}

/* what the key draws with besides its combiner: a rect's draws do not
 * light */
static u32 key_inputs(u64 _key, u32 _inputs, bool *_hwl)
{
	*_hwl = !gli_combiner_key_is_rect(_key) && gli_isHWLightingAllowed() && gli_inputs_shade_color(_inputs);
	if (*_hwl)
		_inputs |= 1u << G_GCI_HW_LIGHT;
	return _inputs;
}

static gli_cprog *build_program(gli_glsl_builder *_b, Combiner *_color, Combiner *_alpha, u64 _key)
{
	u32 inputs;
	bool lod, textures, rect, hwl;
	GLuint program;
	_b->cycleType = gli_combiner_key_cycle_type(_key);
	_b->textureConvert = gli_combiner_key_bilerp(_key);

	inputs = compile_combiner(_b, _key, _color, _alpha);
	lod = gli_inputs_lod(inputs);
	textures = gli_inputs_texture(inputs);
	rect = gli_combiner_key_is_rect(_key);
	inputs = key_inputs(_key, inputs, &hwl);

	fragment_source(_b, inputs, lod, textures, hwl, NULL);
	program = start_program(_b, rect, textures);
	assert(gli_glsl_link_ok(program, 0));

	return gli_glsl_cprog_new(_key, program, _b->useProgram, inputs, _b->info);
}

/*---------------Ubershader-------------*/

/* One flat list of what the stages do, as put_stage writes them: in each
 * of the four stages four words of op * 256 + input, ended by -1. INTER
 * keeps its other two inputs in the next two words. */
static const char s_uberDeclarations[] =
	"uniform mediump int uCmbOps[16];			\n"
	"uniform mediump int uCmbFlags;				\n";

enum {
	UBER_ALPHA_C = 1,
	UBER_ALPHA_ABD = 2,
	UBER_COLOR_C = 4,
	UBER_COLOR_ABD = 8,
	UBER_TWO_STAGES = 16
};

/* the ubershader's stages: each reads its ops from uCmbOps, from its
 * word stage * 4 on */
static const char *const uber_alpha1[] = {
	"  { lowp float v = float(0.0);\n"
	"    for (int i = 0; i < 4; ++i) {\n"
	"      int o = uCmbOps[0 + i];\n"
	"      if (o < 0) break;\n"
	"      int p = o & 255; int k = o >> 8;\n"
	"      if (k == 0) v = ai[p];\n"
	"      else if (k == 1) v = v - ai[p];\n"
	"      else if (k == 2) v = v * ai[p];\n"
	"      else if (k == 3) v = v + ai[p];\n"
	"      else { v = mix(ai[uCmbOps[1]], ai[p], ai[uCmbOps[2]]); break; }\n"
	"    }\n"
	"    alpha1 = v; }\n",
	NULL
};

static const char *const uber_color1[] = {
	"  { lowp vec3 v = vec3(0.0);\n"
	"    for (int i = 0; i < 4; ++i) {\n"
	"      int o = uCmbOps[4 + i];\n"
	"      if (o < 0) break;\n"
	"      int p = o & 255; int k = o >> 8;\n"
	"      if (k == 0) v = ci[p];\n"
	"      else if (k == 1) v = v - ci[p];\n"
	"      else if (k == 2) v = v * ci[p];\n"
	"      else if (k == 3) v = v + ci[p];\n"
	"      else { v = mix(ci[uCmbOps[5]], ci[p], ci[uCmbOps[6]]); break; }\n"
	"    }\n"
	"    color1 = v; }\n",
	NULL
};

static const char *const uber_alpha2[] = {
	"  { lowp float v = float(0.0);\n"
	"    for (int i = 0; i < 4; ++i) {\n"
	"      int o = uCmbOps[8 + i];\n"
	"      if (o < 0) break;\n"
	"      int p = o & 255; int k = o >> 8;\n"
	"      if (k == 0) v = ai[p];\n"
	"      else if (k == 1) v = v - ai[p];\n"
	"      else if (k == 2) v = v * ai[p];\n"
	"      else if (k == 3) v = v + ai[p];\n"
	"      else { v = mix(ai[uCmbOps[9]], ai[p], ai[uCmbOps[10]]); break; }\n"
	"    }\n"
	"    alpha2 = v; }\n",
	NULL
};

static const char *const uber_color2[] = {
	"  { lowp vec3 v = vec3(0.0);\n"
	"    for (int i = 0; i < 4; ++i) {\n"
	"      int o = uCmbOps[12 + i];\n"
	"      if (o < 0) break;\n"
	"      int p = o & 255; int k = o >> 8;\n"
	"      if (k == 0) v = ci[p];\n"
	"      else if (k == 1) v = v - ci[p];\n"
	"      else if (k == 2) v = v * ci[p];\n"
	"      else if (k == 3) v = v + ci[p];\n"
	"      else { v = mix(ci[uCmbOps[13]], ci[p], ci[uCmbOps[14]]); break; }\n"
	"    }\n"
	"    color2 = v; }\n",
	NULL
};


/* What compile_combiner writes, with the stages and the sign extensions
 * chosen by uniforms instead of written in, into _b->combiner. */
static void uber_combiner(gli_glsl_builder *_b, bool _textured, bool _lod)
{
	gli_glsl_text *t = &_b->combiner;
	t->n = 0;
	put(t, "  lowp vec3 ci[21]; lowp float ai[21];\n"
		"  for (int i = 0; i < 21; ++i) { ci[i] = vec3(0.0); ai[i] = 0.0; }\n");
	if (_textured) {
		put(t, "  ci[1] = readtex0.rgb; ci[2] = readtex1.rgb; ci[9] = vec3(readtex0.a); ci[10] = vec3(readtex1.a);\n"
			"  ai[1] = readtex0.a; ai[2] = readtex1.a; ai[9] = readtex0.a; ai[10] = readtex1.a;\n");
		if (_lod)
			put(t, "  ci[14] = vec3(lod_frac); ai[14] = lod_frac;\n");
	}
	put(t, "  ci[3] = uPrimColor.rgb; ci[4] = vec_color.rgb; ci[5] = uEnvColor.rgb; ci[6] = uCenterColor.rgb; ci[7] = uScaleColor.rgb;\n");
	put(t, "  ci[11] = vec3(uPrimColor.a); ci[12] = vec3(vec_color.a); ci[13] = vec3(uEnvColor.a); ci[15] = vec3(uPrimLod);\n");
	put(t, "  ci[16] = vec3(0.5 + 0.5*snoise()); ci[17] = vec3(uK4); ci[18] = vec3(uK5); ci[19] = vec3(1.0);\n");
	put(t, "  ai[3] = uPrimColor.a; ai[4] = vec_color.a; ai[5] = uEnvColor.a; ai[6] = uCenterColor.a; ai[7] = uScaleColor.a;\n");
	put(t, "  ai[11] = uPrimColor.a; ai[12] = vec_color.a; ai[13] = uEnvColor.a; ai[15] = uPrimLod;\n");
	put(t, "  ai[16] = 0.5 + 0.5*snoise(); ai[17] = uK4; ai[18] = uK5; ai[19] = 1.0;\n");

	put_lines(t, uber_alpha1);
	put(t, "  if ((uCmbFlags & 1) != 0) {\n");
	put_lines(t, sign_extend_alpha_c);
	put(t, "  } else if ((uCmbFlags & 2) != 0) {\n");
	put_lines(t, sign_extend_alpha_abd);
	put(t, "  }\n");

	put_lines(t, alpha_test);

	put_lines(t, uber_color1);
	put(t, "  if ((uCmbFlags & 4) != 0) {\n");
	put_lines(t, sign_extend_color_c);
	put(t, "  } else if ((uCmbFlags & 8) != 0) {\n");
	put_lines(t, sign_extend_color_abd);
	put(t, "  }\n");

	if (_b->cycleType == G_CYC_2CYCLE) {
		put(t, "  combined_color = vec4(color1, alpha1);\n"
			"  ci[0] = color1; ci[8] = vec3(alpha1); ai[0] = alpha1; ai[8] = alpha1;\n"
			"  if ((uCmbFlags & 16) != 0) {\n");
		put_lines(t, uber_alpha2);
		put(t, "  } else alpha2 = alpha1;\n"
			"  if (uCvgXAlpha != 0 && alpha2 < 0.125) discard;\n"
			"  if ((uCmbFlags & 16) != 0) {\n");
		put_lines(t, uber_color2);
		put(t, "  } else color2 = color1;\n"
			"  lowp vec4 cmbRes = vec4(color2, alpha2);\n");
	} else {
		put(t, "  if (uCvgXAlpha != 0 && alpha1 < 0.125) discard;\n"
			"  lowp vec4 cmbRes = vec4(color1, alpha1);\n");
	}

	put_lines(t, clamp);
	put_call_dither(t, _b->info);

	if (config.generalEmulation.enableLegacyBlending == 0) {
		put_blender1(t);
		if (_b->cycleType == G_CYC_2CYCLE)
			put_blender2(t);
		put(t, "  fragColor = clampedColor;\n");
	} else {
		put(t, "  fragColor = clampedColor;\n");
		put_lines(t, legacy_blender);
	}
}

/* Draws with the ubershader, with this key's stages, while the driver
 * compiles the key's own program; from then on it is that program. */
typedef struct async_program
{
	gli_cprog base;
	u32 inputs;
	uber_variant *uber;
	GLint ops[16];
	GLint flags;
	GLuint pending;
	const gli_gl_info *info;
	gli_gl_cache1 *useProgram;
	gli_cprog *real;
	int delay;
} async_program;

#define ASYNC(_p) ((async_program *)(_p))
#define CASYNC(_p) ((const async_program *)(_p))

static void async_set_stages(async_program *_a)
{
	int i;
	if (_a->uber->owner == _a)
		return;
	_a->uber->owner = _a;
	for (i = 0; i < 16; ++i)
		glUniform1i(_a->uber->opsLoc[i], _a->ops[i]);
	glUniform1i(_a->uber->flagsLoc, _a->flags);
}

static void async_poll(async_program *_a, bool _wait)
{
	GLint done = GL_TRUE;
	GLint linked = GL_FALSE;
	GLuint program;
	if (_a->pending == 0)
		return;
	if (!_wait && _a->delay > 0) {
		--_a->delay;
		return;
	}
	if (!_wait)
		glGetProgramiv(_a->pending, GL_COMPLETION_STATUS_KHR, &done);
	if (done == GL_FALSE)
		return;
	program = _a->pending;
	_a->pending = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (linked == GL_FALSE) {
		/* stays on the ubershader, which draws the same */
		glDeleteProgram(program);
		return;
	}
	_a->real = gli_glsl_cprog_new(_a->base.key, program, _a->useProgram, _a->inputs, _a->info);
}

static void async_activate(void *_p)
{
	async_program *a = ASYNC(_p);
	async_poll(a, false);
	if (a->real != NULL) {
		a->real->ops->activate(a->real);
		return;
	}
	a->uber->program->ops->activate(a->uber->program);
	async_set_stages(a);
}

static void async_update(void *_p, bool _force)
{
	async_program *a = ASYNC(_p);
	async_poll(a, false);
	if (a->real != NULL) {
		a->real->ops->update(a->real, _force);
		return;
	}
	a->uber->program->ops->update(a->uber->program, _force);
	async_set_stages(a);
}

static void async_free(void *_p)
{
	async_program *a = ASYNC(_p);
	if (a->pending != 0) {
		gli_gl_use_program(a->useProgram, 0);
		glDeleteProgram(a->pending);
	}
	if (a->uber->owner == a)
		a->uber->owner = NULL;
	if (a->real != NULL)
		a->real->ops->free_program(a->real);
	free(a);
}

static bool async_uses_lod(const void *_p) { return gli_inputs_lod(CASYNC(_p)->inputs); }
static bool async_uses_texture(const void *_p) { return gli_inputs_texture(CASYNC(_p)->inputs); }
static bool async_uses_tile(const void *_p, u32 _t) { return gli_inputs_tile(CASYNC(_p)->inputs, _t); }
static bool async_uses_shade(const void *_p) { return gli_inputs_shade(CASYNC(_p)->inputs); }
static bool async_uses_hw_lighting(const void *_p) { return gli_inputs_hw_lighting(CASYNC(_p)->inputs); }

static const gli_cprog_ops s_asyncOps = {
	async_activate, async_update, async_uses_lod, async_free,
	async_uses_texture, async_uses_tile, async_uses_shade, async_uses_hw_lighting, NULL, NULL
};

static void start_uber(gli_glsl_builder *_b, uber_variant *_variant, u64 _key, bool _textured, bool _lod, bool _hwl)
{
	u32 inputs = 0;
	_b->cycleType = gli_combiner_key_cycle_type(_key);
	_b->textureConvert = gli_combiner_key_bilerp(_key);

	if (_textured) {
		inputs |= 1u << G_GCI_TEXEL0;
		inputs |= 1u << G_GCI_TEXEL1;
	}
	inputs |= 1u << G_GCI_SHADE;
	inputs |= 1u << G_GCI_NOISE;
	if (_lod)
		inputs |= 1u << G_GCI_LOD_FRACTION;
	if (_hwl)
		inputs |= 1u << G_GCI_HW_LIGHT;
	_variant->inputs = inputs;
	_variant->key = _key;
	uber_combiner(_b, _textured, _lod);
	fragment_source(_b, inputs, _lod, _textured, _hwl, s_uberDeclarations);
	_variant->handle = start_program(_b, gli_combiner_key_is_rect(_key), _textured);
}

/* Waits for the driver: only at load, where a compile may be waited for. */
static void finish_uber(gli_glsl_builder *_b, uber_variant *_variant)
{
	GLint linked = GL_FALSE;
	char name[32];
	int i;
	glGetProgramiv(_variant->handle, GL_LINK_STATUS, &linked);
	if (linked == GL_FALSE) {
		gli_glsl_link_ok(_variant->handle, 1);
		if (getenv("GLIDEN64_GL_STRICT") != NULL)
			abort();
		return;
	}
	for (i = 0; i < 16; ++i) {
		sprintf(name, "uCmbOps[%d]", i);
		_variant->opsLoc[i] = glGetUniformLocation(_variant->handle, name);
	}
	_variant->flagsLoc = glGetUniformLocation(_variant->handle, "uCmbFlags");
	_variant->program = gli_glsl_cprog_new(_variant->key, _variant->handle, _b->useProgram, _variant->inputs, _b->info);
	if (_variant->program == NULL)
		_variant->handle = 0; /* deleted with it */
}

#define UBER_COUNT (sizeof(((gli_glsl_builder *)0)->uber) / sizeof(uber_variant))
#define UBER_ALL(_b) (&(_b)->uber[0][0][0][0][0][0])

/* All of them, before the game needs one. */
static void build_ubers(gli_glsl_builder *_b)
{
	const u32 hwlVariants = config.generalEmulation.enableHWLighting != 0 ? 2U : 1U;
	u32 rect, cycle, textured, bilerp, lod, hwl;
	size_t i;
	for (rect = 0; rect < 2; ++rect)
		for (cycle = 0; cycle < 2; ++cycle)
			for (textured = 0; textured < 2; ++textured)
				for (bilerp = 0; bilerp < (textured != 0 ? 4U : 1U); ++bilerp)
					for (lod = 0; lod < (textured != 0 ? 2U : 1U); ++lod)
						for (hwl = 0; hwl < (rect == 0 ? hwlVariants : 1U); ++hwl) {
							gDPCombine combine;
							combine.mux = 0;
							combine.muxs0 = (rect | (cycle << 1) | (bilerp << 3)) << 24;
							start_uber(_b, &_b->uber[rect][cycle][bilerp][textured][lod][hwl],
								combine.mux, textured != 0, lod != 0, hwl != 0);
						}

	for (i = 0; i < UBER_COUNT; ++i)
		if (UBER_ALL(_b)[i].handle != 0)
			finish_uber(_b, &UBER_ALL(_b)[i]);
}

/* The variant of the ubershader _key draws with: built at load. */
static uber_variant *ready_uber(gli_glsl_builder *_b, u64 _key, bool _textured, bool _lod, bool _hwl)
{
	uber_variant *v = &_b->uber[gli_combiner_key_is_rect(_key) ? 1 : 0][gli_combiner_key_cycle_type(_key)]
		[_textured ? gli_combiner_key_bilerp(_key) : 0][_textured ? 1 : 0][_lod ? 1 : 0][_hwl ? 1 : 0];
	return v->program != NULL ? v : NULL;
}

/* Draws with the ubershader, built at load, and starts the key's own
 * program where the driver compiles on its own threads; NULL when the
 * ubershader cannot draw this key. */
static gli_cprog *build_async(gli_glsl_builder *_b, Combiner *_color, Combiner *_alpha, u64 _key)
{
	gDPCombine combine;
	u32 inputs;
	bool rect, textures, lod, hwl;
	uber_variant *uber;
	GLint ops[16];
	GLint flags = 0;
	GLuint pending = 0;
	async_program *a;

	if (_b->uberOff || _b->info->isGLES2 || gli_combiner_key_cycle_type(_key) > G_CYC_2CYCLE)
		return NULL;

	_b->cycleType = gli_combiner_key_cycle_type(_key);
	_b->textureConvert = gli_combiner_key_bilerp(_key);
	combine.mux = _key;

	inputs = compile_combiner(_b, _key, _color, _alpha);

	rect = gli_combiner_key_is_rect(_key);
	textures = gli_inputs_texture(inputs);
	lod = gli_inputs_lod(inputs);
	inputs = key_inputs(_key, inputs, &hwl);
	if (lod && !textures)
		return NULL;

	uber = ready_uber(_b, _key, textures, lod, hwl);
	if (uber == NULL)
		return NULL;

	gli_stage_encode(&_alpha->stage[0], ops);
	gli_stage_encode(&_color->stage[0], ops + 4);
	gli_stage_encode(&_alpha->stage[1], ops + 8);
	gli_stage_encode(&_color->stage[1], ops + 12);

	if (gli_combined_alpha_c(_b->cycleType, &combine))
		flags |= UBER_ALPHA_C;
	else if (gli_combined_alpha_abd(_b->cycleType, &combine))
		flags |= UBER_ALPHA_ABD;
	if (gli_combined_color_c(_b->cycleType, &combine))
		flags |= UBER_COLOR_C;
	else if (gli_combined_color_abd(_b->cycleType, &combine))
		flags |= UBER_COLOR_ABD;
	if (_alpha->numStages == 2 && _color->numStages == 2)
		flags |= UBER_TWO_STAGES;

	if (!_b->uberOnly) {
		fragment_source(_b, inputs, lod, textures, hwl, NULL);
		pending = start_program(_b, rect, textures);
	}

	a = (async_program *)calloc(1, sizeof(*a));
	if (a == NULL) {
		if (pending != 0)
			glDeleteProgram(pending);
		return NULL;
	}
	a->base.ops = &s_asyncOps;
	a->base.key = _key;
	a->inputs = inputs;
	a->uber = uber;
	memcpy(a->ops, ops, sizeof(ops));
	a->flags = flags;
	a->pending = pending;
	a->info = _b->info;
	a->useProgram = _b->useProgram;
	a->delay = _b->uberDelay;
	return &a->base;
}

/*---------------The builder-------------*/

static GLuint vertex_shader(gli_glsl_builder *_b, const char *const *_body)
{
	gli_glsl_text *t = &_b->text;
	GLuint shader;
	t->n = 0;
	put(t, _b->vertexHeader.s);
	put_lines(t, _body);
	put_vertex_end(t, _b->info);

	shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(shader, t->n, t->s, NULL);
	glCompileShader(shader);
	return shader;
}

gli_glsl_builder *gli_glsl_builder_new(const gli_gl_info *_info, gli_gl_cache1 *_useProgram)
{
	const char *uberEnv;
	gli_glsl_builder *b = (gli_glsl_builder *)calloc(1, sizeof(*b));
	if (b == NULL)
		return NULL;
	b->info = _info;
	b->useProgram = _useProgram;
	b->optionsBits = gli_combiner_options_bits();
	make_vertex_header(&b->vertexHeader, _info);
	make_fragment_header(&b->fragmentHeader, _info);
	header_lines(&b->fragmentEnd, _info->isGLES2 ? main_end_gles2 : main_end);

	b->vertexRect = vertex_shader(b, vs_rect);
	b->vertexTriangle = vertex_shader(b, vs_triangle);
	b->vertexTexturedRect = vertex_shader(b, vs_textured_rect);
	b->vertexTexturedTriangle = vertex_shader(b, vs_textured_triangle);
	/* Without the driver's own compile threads a key's program would stall the
	 * draw that wants it: those drivers stay on the ubershader until the next load. */
	b->uberOnly = !_info->parallelShaderCompile;
	/* For tools/ci: "off" compiles everything as ever, "delay" holds each program
	 * back for a while, as a driver compiling slowly would, anything else draws
	 * everything with the ubershader. */
	uberEnv = getenv("GLIDEN64_UBERSHADER");
	if (uberEnv != NULL && strcmp(uberEnv, "off") == 0)
		b->uberOff = true;
	else if (uberEnv != NULL && strcmp(uberEnv, "delay") == 0)
		b->uberDelay = 3;
	else if (uberEnv != NULL)
		b->uberOnly = true;

	if (!_info->isGLES2 && !b->uberOff)
		build_ubers(b);
	return b;
}

void gli_glsl_builder_delete(gli_glsl_builder *_b)
{
	size_t i;
	if (_b == NULL)
		return;
	for (i = 0; i < UBER_COUNT; ++i) {
		uber_variant *v = &UBER_ALL(_b)[i];
		if (v->program != NULL)
			v->program->ops->free_program(v->program);
		else if (v->handle != 0)
			glDeleteProgram(v->handle);
	}
	glDeleteShader(_b->vertexRect);
	glDeleteShader(_b->vertexTriangle);
	glDeleteShader(_b->vertexTexturedRect);
	glDeleteShader(_b->vertexTexturedTriangle);
	free(_b);
}

bool gli_glsl_builder_obsolete(const gli_glsl_builder *_b)
{
	return _b->optionsBits != gli_combiner_options_bits();
}

bool gli_glsl_builder_uber_only(const gli_glsl_builder *_b)
{
	return _b->uberOnly && !_b->uberOff && !_b->info->isGLES2;
}

const char *gli_glsl_builder_part(const gli_glsl_builder *_b, int _part)
{
	switch (_part) {
	case GLI_GLSL_VERTEX_HEADER: return _b->vertexHeader.s;
	case GLI_GLSL_FRAGMENT_HEADER: return _b->fragmentHeader.s;
	default: return _b->fragmentEnd.s;
	}
}

#define PROGRAMS(_p) ((gli_glsl_programs *)(_p))

gli_cprog *gli_glsl_ops_build(void *_p, Combiner *_color, Combiner *_alpha, u64 _key)
{
	return build_program(PROGRAMS(_p)->builder, _color, _alpha, _key);
}

gli_cprog *gli_glsl_ops_build_async(void *_p, Combiner *_color, Combiner *_alpha, u64 _key)
{
	return build_async(PROGRAMS(_p)->builder, _color, _alpha, _key);
}

const gli_cprog *gli_glsl_async_program(gli_cprog *_program)
{
	async_program *a = ASYNC(_program);
	async_poll(a, true);
	return a->real;
}
