#ifndef GLI_CONTEXT_H
#define GLI_CONTEXT_H

/* GLideN64's graphics context: the backend's functions (OpenGL or Vulkan)
 * and what it supports. C89.
 *
 * Each backend hands a table of its functions and the object they act on.
 * Handles and parameters are their 32-bit values; the parameter blocks and
 * GLideN64's C++ objects pass as pointers C does not look into. Graphics/
 * Context.h wraps the table for the C++ (gfxContext). */

#include <stddef.h>
#include <boolean.h>
#include "Types.h"
#include "gli_params.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Combiner;
struct gli_cprog;
struct gli_combiner_table;

/* a parameter left unset: the backend leaves that state as it is */
#define GLI_PARAM_INVALID 0xFFFFFFFFu

/* The texture blocks the backends take. Each is made from its _DEFAULT
 * (Graphics/Context.h's C++ blocks are these, made the same way); fields
 * hold handles and the numbers of gli_params.h. */
typedef struct gli_init_texture_params
{
	u32 handle;
	u32 textureUnitIndex;
	u32 target;
	u32 msaaLevel;
	u32 width;
	u32 height;
	u32 mipMapLevel;
	u32 mipMapLevels;
	u32 format;
	u32 internalFormat;
	u32 dataType;
	const void *data;
	/* The texture cache reuses the objects it evicts, so its storage
	 * has to stay respecifiable: immutable storage cannot be. */
	bool mutableStorage;
} gli_init_texture_params;
#define GLI_INIT_TEXTURE_PARAMS_DEFAULT { 0, 0, GLI_TT_TEXTURE_2D, 0, 0, 0, 0, 1, \
	GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, NULL, 0 }

typedef struct gli_update_texture_params
{
	u32 handle;
	u32 textureUnitIndex;
	u32 x;
	u32 y;
	u32 width;
	u32 height;
	u32 mipMapLevel;
	u32 format;
	u32 internalFormat;
	u32 dataType;
	const void *data;
} gli_update_texture_params;
#define GLI_UPDATE_TEXTURE_PARAMS_DEFAULT { 0, 0, 0, 0, 0, 0, 0, \
	GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, NULL }

typedef struct gli_tex_parameters
{
	u32 handle;
	u32 textureUnitIndex;
	u32 target;
	u32 magFilter;
	u32 minFilter;
	u32 wrapS;
	u32 wrapT;
	u32 maxMipmapLevel;
	f32 maxAnisotropy;  /* left as it is unless above 0 */
} gli_tex_parameters;
#define GLI_TEX_PARAMETERS_DEFAULT { 0, 0, GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, \
	GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0.0f }

typedef struct gli_bind_texture_params
{
	u32 texture;
	u32 textureUnitIndex;
	u32 target;
} gli_bind_texture_params;
#define GLI_BIND_TEXTURE_PARAMS_DEFAULT { 0, GLI_PARAM_INVALID, GLI_PARAM_INVALID }

typedef struct gli_bind_image_texture_params
{
	u32 imageUnit;
	u32 texture;
	u32 accessMode;
	u32 textureFormat;
} gli_bind_image_texture_params;
#define GLI_BIND_IMAGE_TEXTURE_PARAMS_DEFAULT { GLI_PARAM_INVALID, 0, GLI_PARAM_INVALID, GLI_PARAM_INVALID }

/* The frame buffer blocks */
typedef struct gli_init_renderbuffer_params
{
	u32 handle;
	u32 target;
	u32 format;
	u32 width;
	u32 height;
} gli_init_renderbuffer_params;
#define GLI_INIT_RENDERBUFFER_PARAMS_DEFAULT { 0, GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0, 0 }

typedef struct gli_fb_render_target
{
	u32 bufferHandle;
	u32 bufferTarget;
	u32 attachment;
	u32 textureTarget;
	u32 textureHandle;
} gli_fb_render_target;
#define GLI_FB_RENDER_TARGET_DEFAULT { 0, GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0 }

typedef struct gli_blit_params
{
	u32 readBuffer;
	u32 drawBuffer;
	s32 srcX0, srcY0, srcX1, srcY1;
	s32 dstX0, dstY0, dstX1, dstY1;
	u32 mask;
	u32 filter;
} gli_blit_params;
#define GLI_BLIT_PARAMS_DEFAULT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GLI_PARAM_INVALID, GLI_PARAM_INVALID }

/* The draw blocks; the program drawn with is a combiner's or a shader's,
 * by its C face */
struct SPVertex;
struct RectVertex;
struct gli_cprog;
typedef struct gli_draw_triangles_params
{
	u32 mode;
	u32 elementsType;
	u32 verticesCount;
	u32 elementsCount;
	bool flatColors;
	struct SPVertex *vertices;
	void *elements;
	const struct gli_cprog *combiner;
} gli_draw_triangles_params;
#define GLI_DRAW_TRIANGLES_PARAMS_DEFAULT { GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0, 0, 0, NULL, NULL, NULL }

typedef struct gli_draw_rects_params
{
	u32 mode;
	bool texrect;
	u32 verticesCount;
	struct RectVertex *vertices;
	const struct gli_cprog *combiner;
} gli_draw_rects_params;
#define GLI_DRAW_RECTS_PARAMS_DEFAULT { GLI_PARAM_INVALID, 1, 0, NULL, NULL }

/* The formats of the textures a frame buffer is made of, by what each is
 * for: internal format, format, data type (gli_params.h numbers) and bytes
 * a texel. A backend sets what it has; the rest stay GLI_PARAM_INVALID (bytes 0). */
typedef struct gli_fb_formats
{
	u32 colorInternalFormat, colorFormat, colorType, colorFormatBytes;
	u32 monochromeInternalFormat, monochromeFormat, monochromeType, monochromeFormatBytes;
	u32 depthInternalFormat, depthFormat, depthType, depthFormatBytes;
	u32 depthImageInternalFormat, depthImageFormat, depthImageType, depthImageFormatBytes;
	u32 lutInternalFormat, lutFormat, lutType, lutFormatBytes;
	u32 noiseInternalFormat, noiseFormat, noiseType, noiseFormatBytes;
} gli_fb_formats;
#define GLI_FB_FORMAT_UNSET GLI_PARAM_INVALID, GLI_PARAM_INVALID, GLI_PARAM_INVALID, 0
#define GLI_FB_FORMATS_DEFAULT { GLI_FB_FORMAT_UNSET, GLI_FB_FORMAT_UNSET, GLI_FB_FORMAT_UNSET, \
	GLI_FB_FORMAT_UNSET, GLI_FB_FORMAT_UNSET, GLI_FB_FORMAT_UNSET }

/* Each backend's formats (Graphics/FramebufferFormats.c), over the unset ones */
#define GLI_FB_GL     0  /* desktop GL */
#define GLI_FB_GLES3  1  /* GLES 3 */
#define GLI_FB_GLES2  2  /* GLES 2 */
#define GLI_FB_ADRENO530   1u  /* GLES 3 on Adreno 530: float color */
#define GLI_FB_OES_DEPTH   2u  /* GLES 2 GL_OES_depth_texture */
#define GLI_FB_OES_RGBA8   4u  /* GLES 2 GL_OES_rgb8_rgba8 */
void gli_fb_formats_gl(gli_fb_formats *_formats, u32 _api, u32 _flags);
void gli_fb_formats_vk(gli_fb_formats *_formats);

/* what a backend may support, in the order isSupported is asked */
enum {
	GLI_FEATURE_MULTISAMPLING,
	GLI_FEATURE_BLIT_FRAMEBUFFER,
	GLI_FEATURE_WEAK_BLIT_FRAMEBUFFER,
	GLI_FEATURE_DEPTH_FRAMEBUFFER_TEXTURES,
	GLI_FEATURE_SHADER_PROGRAM_BINARY,
	GLI_FEATURE_IMAGE_TEXTURES,
	GLI_FEATURE_INTEGER_TEXTURES,
	GLI_FEATURE_CLIP_CONTROL,
	GLI_FEATURE_FRAMEBUFFER_FETCH,
	GLI_FEATURE_TEXTURE_BARRIER,
	GLI_FEATURE_DUAL_SOURCE_BLENDING,
	GLI_FEATURE_COUNT
};

typedef struct gli_context_ops
{
	void (*free_impl)(void *_impl);
	void (*init)(void *_impl);
	void (*destroy)(void *_impl);
	void (*setClampMode)(void *_impl, u32 _mode);
	u32 (*getClampMode)(void *_impl);
	void (*enable)(void *_impl, u32 _parameter, bool _enable);
	u32 (*isEnabled)(void *_impl, u32 _parameter);
	void (*cullFace)(void *_impl, u32 _mode);
	void (*enableDepthWrite)(void *_impl, bool _enable);
	void (*setDepthCompare)(void *_impl, u32 _mode);
	void (*setViewport)(void *_impl, s32 _x, s32 _y, s32 _width, s32 _height);
	void (*setScissor)(void *_impl, s32 _x, s32 _y, s32 _width, s32 _height);
	void (*setBlending)(void *_impl, u32 _sfactor, u32 _dfactor);
	void (*setBlendingSeparate)(void *_impl, u32 _sfactorcolor, u32 _dfactorcolor, u32 _sfactoralpha, u32 _dfactoralpha);
	void (*setBlendColor)(void *_impl, f32 _red, f32 _green, f32 _blue, f32 _alpha);
	void (*clearColorBuffer)(void *_impl, f32 _red, f32 _green, f32 _blue, f32 _alpha);
	void (*clearDepthBuffer)(void *_impl);
	void (*setPolygonOffset)(void *_impl, f32 _factor, f32 _units);
	/* textures */
	u32 (*createTexture)(void *_impl, u32 _target);
	void (*deleteTexture)(void *_impl, u32 _name);
	void (*init2DTexture)(void *_impl, const gli_init_texture_params *_params);
	void (*update2DTexture)(void *_impl, const gli_update_texture_params *_params);
	void *(*lendTextureData)(void *_impl, size_t _bytes);
	void (*setTextureParameters)(void *_impl, const gli_tex_parameters *_params);
	void (*bindTexture)(void *_impl, const gli_bind_texture_params *_params);
	void (*setTextureUnpackAlignment)(void *_impl, s32 _param);
	s32 (*getTextureUnpackAlignment)(const void *_impl);
	s32 (*getMaxTextureSize)(const void *_impl);
	f32 (*getMaxAnisotropy)(const void *_impl);
	void (*bindImageTexture)(void *_impl, const gli_bind_image_texture_params *_params);
	u32 (*convertInternalTextureFormat)(const void *_impl, u32 _format);
	void (*textureBarrier)(void *_impl);
	/* frame buffers: the backend sets the formats it has in @_formats */
	void (*getFramebufferTextureFormats)(void *_impl, gli_fb_formats *_formats);
	u32 (*createFramebuffer)(void *_impl);
	void (*deleteFramebuffer)(void *_impl, u32 _name);
	void (*bindFramebuffer)(void *_impl, u32 _target, u32 _name);
	void (*addFrameBufferRenderTarget)(void *_impl, const gli_fb_render_target *_params);
	u32 (*createRenderbuffer)(void *_impl);
	void (*initRenderbuffer)(void *_impl, const gli_init_renderbuffer_params *_params);
	bool (*blitFramebuffers)(void *_impl, const gli_blit_params *_params);
	void (*setDrawBuffers)(void *_impl, u32 _num);
	void *(*createPixelReadBuffer)(void *_impl, size_t _sizeInBytes);
	void *(*createColorBufferReader)(void *_impl, void *_texture);
	/* shaders */
	bool (*isCombinerProgramBuilderObsolete)(void *_impl);
	bool (*isCombinerUberOnly)(void *_impl);
	void (*resetCombinerProgramBuilder)(void *_impl);
	/* a program for @_key; the async one NULL where it cannot defer */
	struct gli_cprog *(*createCombinerProgram)(void *_impl, struct Combiner *_color, struct Combiner *_alpha, u64 _key);
	struct gli_cprog *(*createCombinerProgramAsync)(void *_impl, struct Combiner *_color, struct Combiner *_alpha, u64 _key);
	bool (*saveShadersStorage)(void *_impl, const struct gli_combiner_table *_combiners);
	bool (*loadShadersStorage)(void *_impl, struct gli_combiner_table *_combiners);
	/* into @_path, @_size bytes at most */
	void (*getCombinerJournalPath)(void *_impl, char *_path, size_t _size, u32 *_version);
	struct gli_cprog *(*createDepthFogShader)(void *_impl);
	struct gli_cprog *(*createTexrectDrawerDrawShader)(void *_impl);
	struct gli_cprog *(*createTexrectDrawerClearShader)(void *_impl);
	struct gli_cprog *(*createTexrectCopyShader)(void *_impl);
	struct gli_cprog *(*createGammaCorrectionShader)(void *_impl);
	struct gli_cprog *(*createFXAAShader)(void *_impl);
	void (*resetShaderProgram)(void *_impl);
	/* drawing */
	void (*drawTriangles)(void *_impl, const gli_draw_triangles_params *_params);
	void (*drawRects)(void *_impl, const gli_draw_rects_params *_params);
	void (*drawLine)(void *_impl, f32 _width, void *_vertices);
	f32 (*getMaxLineWidth)(void *_impl);
	s32 (*getMaxMSAALevel)(void *_impl);
	bool (*isSupported)(const void *_impl, u32 _feature);
	bool (*isError)(const void *_impl);
	bool (*isFramebufferError)(const void *_impl);
} gli_context_ops;

typedef struct gli_context
{
	const gli_context_ops *ops; /* NULL until gli_context_init */
	void *impl;
	gli_fb_formats formats;     /* the frame buffer texture formats */
	/* what the backend supports */
	bool multisampling;
	bool blitFramebuffer;
	bool weakBlitFramebuffer;
	bool depthFramebufferTextures;
	bool shaderProgramBinary;
	bool imageTextures;
	bool integerTextures;
	/* the targets to bind a frame buffer to draw and to read: on GLES2,
	 * which has one, both GLI_BT_FRAMEBUFFER */
	u32 drawFramebuffer;
	u32 readFramebuffer;
	bool clipControl;
	bool framebufferFetch;
	bool textureBarrier;
	bool dualSourceBlending;
} gli_context;

extern gli_context gli_ctx;

/* the backend made (Vulkan when it is on, else OpenGL) and started */
void gli_context_init(void);
/* the backend stopped and freed */
void gli_context_destroy(void);

/* each backend's table and object (opengl_ContextImpl.cpp, vk_ContextImpl.cpp) */
const gli_context_ops *gli_gl_context_ops(void);
void *gli_gl_context_new(void);
const gli_context_ops *gli_vk_context_ops(void);
void *gli_vk_context_new(void);

#ifdef __cplusplus
}
#endif

#endif
