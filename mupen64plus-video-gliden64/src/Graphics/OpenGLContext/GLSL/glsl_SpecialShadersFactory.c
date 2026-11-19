/* GLideN64's special GL shaders: the depth fog (shadow map), the texrect
 * drawer's draw and clear, the texrect copy, gamma correction and FXAA,
 * each made of the combiner builder's headers and its own lines, handed
 * to GL as they are, a line to a string. C89. */
#include <stdlib.h>
#include <assert.h>
#include <Types.h>
#include <Config.h>
#include <gDP.h>
#include <GBI.h>
#include <N64.h>
#include <FrameBuffer.h>
#include <PaletteTexture.h>
#include <gli_combiner.h>
#include <Graphics/gli_params.h>
#include <Graphics/OpenGLContext/opengl_CachedFunctions.h>
#include "glsl_Utils.h"
#include "glsl_FXAA.h"
#include "glsl_Programs.h"

/*---------------The shaders' lines-------------*/

static const char *const rect_nocolor_vs[] = {
	"IN highp vec4 aRectPosition;									\n",
	"void main()                                                    \n",
	"{                                                              \n",
	"  gl_Position = aRectPosition;									\n",
	"}                                                              \n",
	NULL
};

static const char *const textured_rect_vs[] = {
	"IN highp vec4 aRectPosition;	\n",
	"IN highp vec2 aTexCoord0;		\n",
	"OUT mediump vec2 vTexCoord0;	\n",
	"void main()					\n",
	"{								\n",
	"  gl_Position = aRectPosition;	\n",
	"  vTexCoord0 = aTexCoord0;		\n",
	"}								\n",
	NULL
};

/* the depth fog */
static const char *const shadow_map_uniforms[] = {
	"uniform lowp usampler2D uZlutImage;\n",
	"uniform lowp usampler2D uTlutImage;\n",
	"uniform sampler2D uDepthImage;		\n",
	"uniform lowp vec4 uFogColor;								\n",
	NULL
};
static const char *const shadow_map_depth_image[] = {
	"coherent layout(binding = 2, r32f) highp uniform restrict readonly image2D uDepthImageZ;		\n",
	NULL
};
static const char *const shadow_map_fetch_out[] = {
	"layout(location = 0) OUT lowp vec4 fragColor;	\n",
	"layout(location = 1) inout highp vec4 depthZ;	\n",
	NULL
};
static const char *const shadow_map_compare_out[] = {
	"OUT lowp vec4 fragColor;									\n",
	NULL
};
static const char *const shadow_map_out[] = {
	"OUT lowp vec4 fragColor;	\n",
	NULL
};
static const char *const shadow_map_get_alpha[] = {
	"lowp float get_alpha()										\n",
	"{															\n",
	NULL
};
static const char *const shadow_map_arm_depth[] = {
	"  highp float bufZ = gl_LastFragDepthARM;	\n",
	NULL
};
static const char *const shadow_map_texel_depth[] = {
	"  mediump ivec2 coord = ivec2(gl_FragCoord.xy);	\n",
	"  highp float bufZ = texelFetch(uDepthImage,coord, 0).r;	\n",
	NULL
};
static const char *const shadow_map_image_depth[] = {
	"  mediump ivec2 coord = ivec2(gl_FragCoord.xy);	\n",
	"  highp float bufZ = imageLoad(uDepthImageZ,coord).r;	\n",
	NULL
};
static const char *const shadow_map_fetch_depth[] = {
	"  highp float bufZ = depthZ.r;	\n",
	NULL
};
static const char *const shadow_map_main[] = {
	"  highp int iZ = bufZ > 0.999 ? 262143 : int(floor(bufZ * 262143.0));\n",
	"  mediump int y0 = clamp(iZ/512, 0, 511);					\n",
	"  mediump int x0 = iZ - 512*y0;							\n",
	"  highp uint iN64z = texelFetch(uZlutImage,ivec2(x0,y0), 0).r;		\n",
	"  highp float n64z = clamp(float(iN64z)/65532.0, 0.0, 1.0);\n",
	"  highp int index = min(255, int(n64z*255.0));				\n",
	"  highp uint iAlpha = texelFetch(uTlutImage,ivec2(index,0), 0).r;\n",
	"  return float(iAlpha>>8)/255.0;							\n",
	"}															\n",
	"void main()												\n",
	"{															\n",
	"  fragColor = vec4(uFogColor.rgb, get_alpha());			\n",
	"}															\n",
	NULL
};
static const char *const shadow_map_arm_extension[] = {
	"#extension GL_ARM_shader_framebuffer_fetch_depth_stencil : enable	\n",
	NULL
};

/* the texrect drawer's filters, for GL ES 2 and the others */
static const char *const tex_3point_gles2[] = {
	"#if (__VERSION__ > 120)																						\n",
	"# define IN in																									\n",
	"# define OUT out																								\n",
	"#else																											\n",
	"# define IN varying																							\n",
	"# define OUT																									\n",
	"#ifndef GL_FRAGMENT_PRECISION_HIGH																				\n",
	"precision mediump float;																						\n",
	"# define highp mediump																							\n",
	"#endif																											\n",
	"#endif // __VERSION __																							\n",
	"lowp vec4 uTestColor = vec4(4.0/255.0, 2.0/255.0, 1.0/255.0, 0.0);												\n",
	"uniform lowp int uEnableAlphaTest;																				\n",
	"uniform mediump vec2 uTextureSize;																				\n",
	/* 3 point texture filtering. */
	/* Original author: ArthurCarvalho */
	/* GLSL implementation: twinaphex, mupen64plus-libretro project. */
	"#define TEX_OFFSET(off) texture2D(tex, texCoord - (off)/texSize)												\n",
	"lowp vec4 texFilter(in sampler2D tex, in mediump vec2 texCoord)												\n",
	"{																												\n",
	"  lowp vec4 c = texture2D(tex, texCoord);																		\n",
	"  if (c == uTestColor) discard;																				\n",
	"  if (uEnableAlphaTest != 0 && !(c.a > 0.0)) discard;															\n",
	"  mediump vec2 texSize = uTextureSize;																			\n",
	"																												\n",
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));													\n",
	"  offset -= step(1.0, offset.x + offset.y);																	\n",
	"  lowp vec4 zero = vec4(0.0);					 																\n",
	"  lowp vec4 c0 = TEX_OFFSET(offset);																			\n",
	"  c0 = c * vec4(equal(c0, uTestColor)) + c0 * vec4(notEqual(c0, uTestColor));									\n",
	"  lowp vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));										\n",
	"  c1 = c * vec4(equal(c1, uTestColor)) + c1 * vec4(notEqual(c1, uTestColor));									\n",
	"  lowp vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));										\n",
	"  c2 = c * vec4(equal(c2, uTestColor)) + c2 * vec4(notEqual(c2, uTestColor));									\n",
	"  return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);													\n",
	"}																												\n",
	"																												\n",
	NULL
};

static const char *const tex_3point[] = {
	/* 3 point texture filtering. */
	/* Original author: ArthurCarvalho */
	/* GLSL implementation: twinaphex, mupen64plus-libretro project. */
	"#define TEX_OFFSET(off, tex, texCoord, texSize) texture(tex, texCoord - (off)/texSize)							\n",
	"#define TEX_FILTER(name, tex, texCoord)																		\\\n",
	"{																												\\\n",
	"  lowp vec4 c = texture(tex, texCoord);		 																\\\n",
	"  if (c == uTestColor) discard;																				\\\n",
	"  if (uEnableAlphaTest == 1 && !(c.a > 0.0)) discard;															\\\n",
	"  mediump vec2 texSize = vec2(textureSize(tex,0));																\\\n",
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));													\\\n",
	"  offset -= step(1.0, offset.x + offset.y);																	\\\n",
	"  lowp vec4 zero = vec4(0.0);					 																\\\n",
	"  lowp vec4 c0 = TEX_OFFSET(offset, tex, texCoord, texSize);													\\\n",
	"  c0 = c * vec4(equal(c0, uTestColor)) + c0 * vec4(notEqual(c0, uTestColor));									\\\n",
	"  lowp vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord, texSize);				\\\n",
	"  c1 = c * vec4(equal(c1, uTestColor)) + c1 * vec4(notEqual(c1, uTestColor));									\\\n",
	"  lowp vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord, texSize);				\\\n",
	"  c2 = c * vec4(equal(c2, uTestColor)) + c2 * vec4(notEqual(c2, uTestColor));									\\\n",
	"  name = c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);													\\\n",
	"}																												\\\n",
	"																											    \n",
	NULL
};

static const char *const tex_bilinear_gles2[] = {
	"#if (__VERSION__ > 120)																						\n",
	"# define IN in																									\n",
	"# define OUT out																								\n",
	"#else																											\n",
	"# define IN varying																							\n",
	"# define OUT																									\n",
	"#ifndef GL_FRAGMENT_PRECISION_HIGH																				\n",
	"precision mediump float;																						\n",
	"# define highp mediump																							\n",
	"#endif																											\n",
	"#endif // __VERSION __																							\n",
	"lowp vec4 uTestColor = vec4(4.0/255.0, 2.0/255.0, 1.0/255.0, 0.0);												\n",
	"uniform lowp int uEnableAlphaTest;																				\n",
	"uniform mediump vec2 uTextureSize;																				\n",
	"#define TEX_OFFSET(off) texture2D(tex, texCoord - (off)/texSize)												\n",
	"lowp vec4 texFilter(in sampler2D tex, in mediump vec2 texCoord)												\n",
	"{																												\n",
	"  lowp vec4 c = texture2D(tex, texCoord);																		\n",
	"  if (c == uTestColor) discard;																				\n",
	"  if (uEnableAlphaTest != 0 && !(c.a > 0.0)) discard;															\n",
	"  mediump vec2 texSize = uTextureSize;																			\n",
	"																												\n",
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));													\n",
	"  offset -= step(1.0, offset.x + offset.y);																	\n",
	"  lowp vec4 zero = vec4(0.0);																					\n",
	"																												\n",
	"  lowp vec4 p0q0 = TEX_OFFSET(offset);																			\n",
	"  p0q0 = c * vec4(equal(p0q0, uTestColor)) + p0q0 * vec4(notEqual(p0q0, uTestColor));							\n",
	"  lowp vec4 p1q0 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));										\n",
	"  p1q0 = c * vec4(equal(p1q0, uTestColor)) + p1q0 * vec4(notEqual(p1q0, uTestColor));							\n",
	"																												\n",
	"  lowp vec4 p0q1 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));				                        \n",
	"  p0q1 = c * vec4(equal(p0q1, uTestColor)) + p0q1 * vec4(notEqual(p0q1, uTestColor));							\n",
	"  lowp vec4 p1q1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y - sign(offset.y)));						\n",
	"  p1q1 = c * vec4(equal(p1q1, uTestColor)) + p1q1 * vec4(notEqual(p1q1, uTestColor));							\n",
	"																												\n",
	"  mediump vec2 interpolationFactor = abs(offset);																\n",
	"  lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x ); // Interpolates top row in X direction.		\n",
	"  lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x ); // Interpolates bottom row in X direction.	\n",
	"  return mix( pInterp_q0, pInterp_q1, interpolationFactor.y ); // Interpolate in Y direction.					\n",
	"}																												\n",
	NULL
};

static const char *const tex_bilinear[] = {
	"#define TEX_OFFSET(off, tex, texCoord, texSize) texture(tex, texCoord - (off)/texSize)							\n",
	"#define TEX_FILTER(name, tex, texCoord)																		\\\n",
	"{																												\\\n",
	"  lowp vec4 c = texture(tex, texCoord);																		\\\n",
	"  if (c == uTestColor) discard;																				\\\n",
	"  if (uEnableAlphaTest == 1 && !(c.a > 0.0)) discard;															\\\n",
	"  mediump vec2 texSize = vec2(textureSize(tex,0));																\\\n",
	"  mediump vec2 offset = fract(texCoord*texSize - vec2(0.5));													\\\n",
	"  offset -= step(1.0, offset.x + offset.y);																	\\\n",
	"  lowp vec4 zero = vec4(0.0);																					\\\n",
	"																												\\\n",
	"  lowp vec4 p0q0 = TEX_OFFSET(offset, tex, texCoord, texSize);													\\\n",
	"  p0q0 = c * vec4(equal(p0q0, uTestColor)) + p0q0 * vec4(notEqual(p0q0, uTestColor));							\\\n",
	"  lowp vec4 p1q0 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y), tex, texCoord, texSize);				\\\n",
	"  p1q0 = c * vec4(equal(p1q0, uTestColor)) + p1q0 * vec4(notEqual(p1q0, uTestColor));							\\\n",
	"																												\\\n",
	"  lowp vec4 p0q1 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)), tex, texCoord, texSize);				\\\n",
	"  p0q1 = c * vec4(equal(p0q1, uTestColor)) + p0q1 * vec4(notEqual(p0q1, uTestColor));							\\\n",
	"  lowp vec4 p1q1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y - sign(offset.y)), tex, texCoord, texSize);	\\\n",
	"  p1q1 = c * vec4(equal(p1q1, uTestColor)) + p1q1 * vec4(notEqual(p1q1, uTestColor));							\\\n",
	"																												\\\n",
	"  mediump vec2 interpolationFactor = abs(offset);																\\\n",
	"  lowp vec4 pInterp_q0 = mix( p0q0, p1q0, interpolationFactor.x ); 											\\\n", /* Interpolates top row in X direction. */
	"  lowp vec4 pInterp_q1 = mix( p0q1, p1q1, interpolationFactor.x ); 											\\\n", /* Interpolates bottom row in X direction. */
	"  name = mix( pInterp_q0, pInterp_q1, interpolationFactor.y ); 												\\\n", /* Interpolate in Y direction. */
	"}																												\\\n",
	"																												\n",
	NULL
};

static const char *const draw_gles2[] = {
	"uniform sampler2D uTex0;													\n",
	"IN mediump vec2 vTexCoord0;												\n",
	"OUT lowp vec4 fragColor;													\n",
	"void main()																\n",
	"{																			\n",
	"  fragColor = texFilter(uTex0, vTexCoord0);								\n",
	"  gl_FragColor = fragColor;												\n",
	"}																			\n",
	NULL
};

static const char *const draw[] = {
	"uniform sampler2D uTex0;													\n",
	"uniform lowp int uEnableAlphaTest;											\n",
	"uniform highp float uPrimDepth;											\n",
	"lowp vec4 uTestColor = vec4(4.0/255.0, 2.0/255.0, 1.0/255.0, 0.0);			\n",
	"in mediump vec2 vTexCoord0;												\n",
	"out lowp vec4 fragColor;													\n",
	"void main()																\n",
	"{																			\n",
	"  TEX_FILTER(fragColor, uTex0, vTexCoord0);								\n",
	NULL
};
static const char *const draw_depth[] = {
	"  gl_FragDepth = uPrimDepth;											\n",
	NULL
};
static const char *const draw_end[] = {
	"}																			\n",
	NULL
};

static const char *const clear_gles2[] = {
	"lowp vec4 uTestColor = vec4(4.0/255.0, 2.0/255.0, 1.0/255.0, 0.0);	\n",
	"void main()														\n",
	"{																	\n",
	"  gl_FragColor = uTestColor;										\n",
	"}																	\n",
	NULL
};

static const char *const clear[] = {
	"lowp vec4 uTestColor = vec4(4.0/255.0, 2.0/255.0, 1.0/255.0, 0.0);	\n",
	"out lowp vec4 fragColor;													\n",
	"void main()																\n",
	"{																			\n",
	"  fragColor = uTestColor;													\n",
	"}																			\n",
	NULL
};

static const char *const texrect_copy[] = {
	"IN mediump vec2 vTexCoord0;							\n",
	"uniform sampler2D uTex0;								\n",
	"OUT lowp vec4 fragColor;								\n",
	"														\n",
	"void main()											\n",
	"{														\n",
	"	fragColor = texture2D(uTex0, vTexCoord0);			\n",
	NULL
};

static const char *const gamma_correction[] = {
	"IN mediump vec2 vTexCoord0;													\n",
	"uniform sampler2D uTex0;													\n",
	"uniform lowp float uGammaCorrectionLevel;									\n",
	"OUT lowp vec4 fragColor;													\n",
	"void main()																\n",
	"{																			\n",
	"    fragColor = texture2D(uTex0, vTexCoord0);								\n",
	"    fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / uGammaCorrectionLevel));	\n",
	NULL
};

/*---------------A shader's strings-------------*/

/* the most strings a shader is made of: FXAA's fragment shader's lines and
 * its header and end */
#define GLI_GLSL_PARTS_MAX 96

typedef struct gli_glsl_parts
{
	const GLchar *s[GLI_GLSL_PARTS_MAX];
	GLsizei n;
} gli_glsl_parts;

static void add(gli_glsl_parts *_parts, const char *_s)
{
	if (_parts->n < GLI_GLSL_PARTS_MAX)
		_parts->s[_parts->n++] = _s;
}

static void add_lines(gli_glsl_parts *_parts, const char *const *_lines)
{
	for (; *_lines != NULL; ++_lines)
		add(_parts, *_lines);
}

/*---------------The shaders-------------*/

struct gli_glsl_special
{
	const gli_gl_info *info;
	struct gli_gl_cache1 *useProgram;
	/* the builder's, borrowed */
	const char *vertexHeader;
	const char *fragmentHeader;
	const char *fragmentEnd;
};

enum
{
	GLI_GLSL_PLAIN,
	GLI_GLSL_SHADOW_MAP,
	GLI_GLSL_TEXRECT_DRAW,
	GLI_GLSL_FXAA
};

/* the uniforms' locations */
enum
{
	LOC_FOG = 0, LOC_ZLUT, LOC_TLUT, LOC_DEPTH_IMAGE,              /* the depth fog */
	LOC_TEXTURE_SIZE = 0, LOC_ALPHA_TEST, LOC_PRIM_DEPTH,          /* the texrect drawer */
	LOC_COUNT = 4
};

typedef struct gli_glsl_shader
{
	gli_cprog base;   /* first: the shader is its C face */
	GLuint program;
	struct gli_gl_cache1 *useProgram;
	int kind;
	GLint loc[LOC_COUNT];
	GLfloat depth;    /* the texrect drawer's last sent */
	u16 width, height; /* FXAA's last sent */
} gli_glsl_shader;

#define SHADER(_p) ((gli_glsl_shader *)(_p))

static void shader_activate(void *_p)
{
	gli_glsl_shader *s = SHADER(_p);
	gli_gl_use_program(s->useProgram, s->program);
	switch (s->kind) {
	case GLI_GLSL_SHADOW_MAP:
		gDP.changed |= CHANGED_COMBINE;
		glUniform4fv(s->loc[LOC_FOG], 1, &gDP.fogColor.r);
		glUniform1i(s->loc[LOC_ZLUT], GLI_TU_ZLUTTex);
		glUniform1i(s->loc[LOC_TLUT], GLI_TU_PaletteTex);
		glUniform1i(s->loc[LOC_DEPTH_IMAGE], 0);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gli_palette_update();
		break;
	case GLI_GLSL_TEXRECT_DRAW:
		if (s->loc[LOC_PRIM_DEPTH] >= 0) {
			const GLfloat depth = gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : 0.0f;
			if (depth != s->depth) {
				s->depth = depth;
				glUniform1f(s->loc[LOC_PRIM_DEPTH], s->depth);
			}
		}
		gDP.changed |= CHANGED_COMBINE;
		break;
	case GLI_GLSL_FXAA:
		{
			const struct FrameBuffer *pBuffer;
			gDP.changed |= CHANGED_COMBINE;
			pBuffer = gli_fb_find(*REG.VI_ORIGIN);
			if (pBuffer != NULL && pBuffer->fb.m_pTexture != NULL &&
				(s->width != pBuffer->fb.m_pTexture->realWidth || s->height != pBuffer->fb.m_pTexture->realHeight)) {
				s->width = pBuffer->fb.m_pTexture->realWidth;
				s->height = pBuffer->fb.m_pTexture->realHeight;
				glUniform2f(s->loc[LOC_TEXTURE_SIZE], (GLfloat)s->width, (GLfloat)s->height);
			}
		}
		break;
	default:
		gDP.changed |= CHANGED_COMBINE;
		break;
	}
}

static void shader_update(void *_p, bool _force) { (void)_p; (void)_force; }
static bool shader_uses_lod(const void *_p) { (void)_p; return 0; }

static void shader_free(void *_p)
{
	gli_glsl_shader *s = SHADER(_p);
	gli_gl_use_program(s->useProgram, 0);
	glDeleteProgram(s->program);
	free(s);
}

static bool shader_uses_texture(const void *_p) { (void)_p; return 1; }
static bool shader_uses_tile(const void *_p, u32 _t) { (void)_p; return _t == 0; }
static bool shader_uses_shade(const void *_p) { (void)_p; return 0; }
static bool shader_uses_hw_lighting(const void *_p) { (void)_p; return 0; }

static void shader_texture_size(void *_p, u32 _width, u32 _height)
{
	gli_glsl_shader *s = SHADER(_p);
	if (s->loc[LOC_TEXTURE_SIZE] < 0)
		return;
	gli_gl_use_program(s->useProgram, s->program);
	glUniform2f(s->loc[LOC_TEXTURE_SIZE], (GLfloat)_width, (GLfloat)_height);
	gDP.changed |= CHANGED_COMBINE;
}

static void shader_alpha_test(void *_p, int _enable)
{
	gli_glsl_shader *s = SHADER(_p);
	gli_gl_use_program(s->useProgram, s->program);
	glUniform1i(s->loc[LOC_ALPHA_TEST], _enable);
	gDP.changed |= CHANGED_COMBINE;
}

static const gli_cprog_ops shader_ops = {
	shader_activate, shader_update, shader_uses_lod, shader_free,
	shader_uses_texture, shader_uses_tile, shader_uses_shade, shader_uses_hw_lighting, NULL, NULL
};

static const gli_cprog_ops texrect_draw_ops = {
	shader_activate, shader_update, shader_uses_lod, shader_free,
	shader_uses_texture, shader_uses_tile, shader_uses_shade, shader_uses_hw_lighting,
	shader_texture_size, shader_alpha_test
};

/* the vertex shader: the header and @_body */
static void vertex(gli_glsl_parts *_parts, const gli_glsl_special *_special, const char *const *_body)
{
	_parts->n = 0;
	add(_parts, _special->vertexHeader);
	add_lines(_parts, _body);
}

/* a shader of @_kind from its strings, used to find its uniforms with
 * @_uniforms; NULL when there is no room */
static gli_glsl_shader *shader_new(const gli_glsl_special *_special, int _kind,
	const gli_glsl_parts *_vertex, const gli_glsl_parts *_fragment, const char *const *_uniforms)
{
	gli_glsl_shader *s = (gli_glsl_shader *)calloc(1, sizeof(*s));
	int i;
	if (s == NULL)
		return NULL;
	s->base.ops = _kind == GLI_GLSL_TEXRECT_DRAW ? &texrect_draw_ops : &shader_ops;
	s->useProgram = _special->useProgram;
	s->kind = _kind;
	s->program = gli_glsl_rect_program(_vertex->s, _vertex->n, _fragment->s, _fragment->n);
	if (_uniforms != NULL) {
		gli_gl_use_program(s->useProgram, s->program);
		for (i = 0; _uniforms[i] != NULL; ++i)
			s->loc[i] = glGetUniformLocation(s->program, _uniforms[i]);
	}
	return s;
}

/* the texture unit 0 for uTex0, the program left used */
static void sampler0(const gli_glsl_shader *_s)
{
	const GLint loc = glGetUniformLocation(_s->program, "uTex0");
	glUniform1i(loc, 0);
}

static gli_cprog *shadow_map_shader(const gli_glsl_special *_special)
{
	static const char *const uniforms[] = { "uFogColor", "uZlutImage", "uTlutImage", "uDepthImage", NULL };
	const gli_gl_info *info = _special->info;
	const int compare = config.frameBufferEmulation.N64DepthCompare != 0;
	gli_glsl_parts vs, fs;
	gli_glsl_shader *s;
	if (info->isGLES2)
		return NULL;
	vertex(&vs, _special, rect_nocolor_vs);
	fs.n = 0;
	add(&fs, _special->fragmentHeader);
	if (!compare && info->fetch_depth)
		add_lines(&fs, shadow_map_arm_extension);
	add_lines(&fs, shadow_map_uniforms);
	if (compare) {
		if (info->imageTextures)
			add_lines(&fs, shadow_map_depth_image);
		add_lines(&fs, info->ext_fetch ? shadow_map_fetch_out : shadow_map_compare_out);
	} else
		add_lines(&fs, shadow_map_out);
	add_lines(&fs, shadow_map_get_alpha);
	if (!compare)
		add_lines(&fs, info->fetch_depth ? shadow_map_arm_depth : shadow_map_texel_depth);
	/* GLInfo makes sure of one of these with the depth compare */
	else if (info->imageTextures)
		add_lines(&fs, shadow_map_image_depth);
	else if (info->ext_fetch)
		add_lines(&fs, shadow_map_fetch_depth);
	add_lines(&fs, shadow_map_main);
	s = shader_new(_special, GLI_GLSL_SHADOW_MAP, &vs, &fs, uniforms);
	if (s == NULL)
		return NULL;
	gli_gl_use_program(s->useProgram, 0);
	return &s->base;
}

static gli_cprog *texrect_draw_shader(const gli_glsl_special *_special)
{
	const gli_gl_info *info = _special->info;
	gli_glsl_parts vs, fs;
	gli_glsl_shader *s;
	vertex(&vs, _special, textured_rect_vs);
	fs.n = 0;
	add(&fs, _special->fragmentHeader);
	if (config.texture.bilinearMode == BILINEAR_STANDARD)
		add_lines(&fs, info->isGLES2 ? tex_bilinear_gles2 : tex_bilinear);
	else
		add_lines(&fs, info->isGLES2 ? tex_3point_gles2 : tex_3point);
	if (info->isGLES2)
		add_lines(&fs, draw_gles2);
	else {
		add_lines(&fs, draw);
		if (config.generalEmulation.enableFragmentDepthWrite != 0 &&
			config.frameBufferEmulation.N64DepthCompare == 0)
			add_lines(&fs, draw_depth);
		add_lines(&fs, draw_end);
	}
	s = shader_new(_special, GLI_GLSL_TEXRECT_DRAW, &vs, &fs, NULL);
	if (s == NULL)
		return NULL;
	gli_gl_use_program(s->useProgram, s->program);
	{
		const GLint loc = glGetUniformLocation(s->program, "uTex0");
		assert(loc >= 0);
		glUniform1i(loc, 0);
	}
	s->loc[LOC_TEXTURE_SIZE] = glGetUniformLocation(s->program, "uTextureSize");
	s->loc[LOC_ALPHA_TEST] = glGetUniformLocation(s->program, "uEnableAlphaTest");
	s->loc[LOC_PRIM_DEPTH] = glGetUniformLocation(s->program, "uPrimDepth");
	gli_gl_use_program(s->useProgram, 0);
	return &s->base;
}

static gli_cprog *texrect_clear_shader(const gli_glsl_special *_special)
{
	gli_glsl_parts vs, fs;
	gli_glsl_shader *s;
	vertex(&vs, _special, textured_rect_vs);
	fs.n = 0;
	add(&fs, _special->fragmentHeader);
	add_lines(&fs, _special->info->isGLES2 ? clear_gles2 : clear);
	s = shader_new(_special, GLI_GLSL_PLAIN, &vs, &fs, NULL);
	return s != NULL ? &s->base : NULL;
}

/* the fragment shader: the header, @_body and the end */
static void fragment_ended(gli_glsl_parts *_parts, const gli_glsl_special *_special, const char *const *_body)
{
	_parts->n = 0;
	add(_parts, _special->fragmentHeader);
	add_lines(_parts, _body);
	add(_parts, _special->fragmentEnd);
}

static gli_cprog *texrect_copy_shader(const gli_glsl_special *_special)
{
	gli_glsl_parts vs, fs;
	gli_glsl_shader *s;
	vertex(&vs, _special, textured_rect_vs);
	fragment_ended(&fs, _special, texrect_copy);
	s = shader_new(_special, GLI_GLSL_PLAIN, &vs, &fs, NULL);
	if (s == NULL)
		return NULL;
	gli_gl_use_program(s->useProgram, s->program);
	sampler0(s);
	gli_gl_use_program(s->useProgram, 0);
	return &s->base;
}

static gli_cprog *gamma_correction_shader(const gli_glsl_special *_special)
{
	gli_glsl_parts vs, fs;
	gli_glsl_shader *s;
	vertex(&vs, _special, textured_rect_vs);
	fragment_ended(&fs, _special, gamma_correction);
	s = shader_new(_special, GLI_GLSL_PLAIN, &vs, &fs, NULL);
	if (s == NULL)
		return NULL;
	gli_gl_use_program(s->useProgram, s->program);
	sampler0(s);
	{
		const GLint levelLoc = glGetUniformLocation(s->program, "uGammaCorrectionLevel");
		const f32 gammaLevel = (config.gammaCorrection.force != 0) ? config.gammaCorrection.level : 2.0f;
		assert(levelLoc >= 0);
		glUniform1f(levelLoc, gammaLevel);
	}
	gli_gl_use_program(s->useProgram, 0);
	return &s->base;
}

static gli_cprog *fxaa_shader(const gli_glsl_special *_special)
{
	static const char *const uniforms[] = { "uTextureSize", NULL };
	gli_glsl_parts vs, fs;
	gli_glsl_shader *s;
	vertex(&vs, _special, gli_glsl_fxaa_vertex());
	fragment_ended(&fs, _special, gli_glsl_fxaa_fragment());
	s = shader_new(_special, GLI_GLSL_FXAA, &vs, &fs, uniforms);
	if (s == NULL)
		return NULL;
	gli_gl_use_program(s->useProgram, 0);
	return &s->base;
}

/*---------------Their C face (glsl_Programs.h)-------------*/

gli_glsl_special *gli_glsl_special_new(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	const gli_glsl_builder *_builder)
{
	gli_glsl_special *special = (gli_glsl_special *)malloc(sizeof(*special));
	if (special == NULL)
		return NULL;
	special->info = _info;
	special->useProgram = _useProgram;
	special->vertexHeader = gli_glsl_builder_part(_builder, GLI_GLSL_VERTEX_HEADER);
	special->fragmentHeader = gli_glsl_builder_part(_builder, GLI_GLSL_FRAGMENT_HEADER);
	special->fragmentEnd = gli_glsl_builder_part(_builder, GLI_GLSL_FRAGMENT_END);
	return special;
}

void gli_glsl_special_delete(gli_glsl_special *_special)
{
	free(_special);
}

#define SPECIAL(_p) ((const gli_glsl_programs *)(_p))->special

gli_cprog *gli_glsl_ops_depth_fog_shader(void *_p) { return shadow_map_shader(SPECIAL(_p)); }
gli_cprog *gli_glsl_ops_texrect_draw_shader(void *_p) { return texrect_draw_shader(SPECIAL(_p)); }
gli_cprog *gli_glsl_ops_texrect_clear_shader(void *_p) { return texrect_clear_shader(SPECIAL(_p)); }
gli_cprog *gli_glsl_ops_texrect_copy_shader(void *_p) { return texrect_copy_shader(SPECIAL(_p)); }
gli_cprog *gli_glsl_ops_gamma_correction_shader(void *_p) { return gamma_correction_shader(SPECIAL(_p)); }
gli_cprog *gli_glsl_ops_fxaa_shader(void *_p) { return fxaa_shader(SPECIAL(_p)); }
