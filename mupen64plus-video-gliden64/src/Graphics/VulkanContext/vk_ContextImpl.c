/* GLideN64's context on the C89 Vulkan renderer: the table gli_context.h
 * calls (gli_vk_context_ops), its parameters mapped to vk_renderer.h, and
 * the combiner and special programs as gli_cprog. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compat/strl.h>
#include <file/file_path.h>

#include <Config.h>
#include <gDP.h>
#include <gSP.h>
#include <N64.h>
#include <NoiseTexture.h>
#include <PaletteTexture.h>
#include <FrameBuffer.h>
#include <DisplayWindow.h>
#include <CombinerKey.h>
#include <CombinerKeyJournal.h>
#include <CombinerMux.h>
#include <PluginAPI.h>
#include <RSP.h>
#include <gli_combiner.h>
#include <gli_textures.h>
#include <gli_tex.h>
#include <Graphics/gli_context.h>
#include <Graphics/gli_params.h>
#include <Graphics/CombinerValues.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/GLSL/glsl_StdHash.h>

#include "vk_ColorBufferReader.h"
#include "vk_renderer.h"
#include "vk_cache.h"
#include "vk_device.h"
#include "vk_upscaler.h"
#include "vk_ContextImpl.h"

static vkr_renderer *g_r;

/* GL's numbers, which GLideN64's parameters carry */
enum {
	GLV_R8 = 0x8229, GLV_R16F = 0x822D, GLV_R32F = 0x822E, GLV_R32UI = 0x8236, GLV_R16UI = 0x8234,
	GLV_RGBA8 = 0x8058, GLV_RGB8 = 0x8051, GLV_RGBA4 = 0x8056, GLV_RGB5_A1 = 0x8057,
	GLV_DEPTH = 0x81A6
};

/* the context's own state */
typedef struct vk_context
{
	u32 clampMode;
	s32 unpack;
	u32 optionsBits;
} vk_context;

enum { VK_CLAMP_CLIPPING, VK_CLAMP_NO_NEAR_PLANE, VK_CLAMP_NONE };

#define CTX(_p) ((vk_context *)(_p))
#define CCTX(_p) ((const vk_context *)(_p))

static int to_format(u32 _internal)
{
	switch (_internal) {
	case GLV_RGBA8: return VKR_FMT_RGBA8;
	case GLV_RGB8: return VKR_FMT_RGB8;
	case GLV_RGBA4: return VKR_FMT_RGBA4;
	case GLV_RGB5_A1: return VKR_FMT_RGB5A1;
	case GLV_R8: return VKR_FMT_R8;
	case GLV_R16F: return VKR_FMT_R16F;
	case GLV_R32F: return VKR_FMT_R32F;
	case GLV_R32UI: return VKR_FMT_R32UI;
	case GLV_R16UI: return VKR_FMT_R16UI;
	case GLV_DEPTH: return VKR_FMT_DEPTH;
	}
	return VKR_FMT_NONE;
}

static u32 format_bytes(int _fmt)
{
	switch (_fmt) {
	case VKR_FMT_RGBA4: case VKR_FMT_RGB5A1: case VKR_FMT_R16F: case VKR_FMT_R16UI: return 2;
	case VKR_FMT_R8: return 1;
	}
	return 4;
}

static int to_unit(u32 _unit)
{
	static const int units[8] = { VKR_BIND_TEX0, VKR_BIND_TEX1, VKR_BIND_NOISE, VKR_BIND_DEPTH,
		VKR_BIND_ZLUT, VKR_BIND_TLUT, VKR_BIND_MS0, VKR_BIND_MS1 };
	return _unit < 8 ? units[_unit] : VKR_BIND_COUNT;
}

static int to_blend(u32 _b)
{
	switch (_b) {
	case GLI_BLEND_ZERO: return VKR_BLEND_ZERO;
	case GLI_BLEND_ONE: return VKR_BLEND_ONE;
	case GLI_BLEND_SRC_ALPHA: return VKR_BLEND_SRC_ALPHA;
	case GLI_BLEND_DST_ALPHA: return VKR_BLEND_DST_ALPHA;
	case GLI_BLEND_ONE_MINUS_SRC_ALPHA: return VKR_BLEND_INV_SRC_ALPHA;
	case GLI_BLEND_CONSTANT_ALPHA: return VKR_BLEND_CONST_ALPHA;
	case GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA: return VKR_BLEND_INV_CONST_ALPHA;
	case GLI_BLEND_SRC1_COLOR: return VKR_BLEND_SRC1_COLOR;
	case GLI_BLEND_ONE_MINUS_SRC1_COLOR: return VKR_BLEND_INV_SRC1_COLOR;
	case GLI_BLEND_SRC1_ALPHA: return VKR_BLEND_SRC1_ALPHA;
	case GLI_BLEND_ONE_MINUS_SRC1_ALPHA: return VKR_BLEND_INV_SRC1_ALPHA;
	}
	return VKR_BLEND_ONE;
}

static int to_wrap(u32 _w)
{
	if (_w == GLI_TP_WRAP_REPEAT) return VKR_WRAP_REPEAT;
	if (_w == GLI_TP_WRAP_MIRRORED_REPEAT) return VKR_WRAP_MIRROR;
	return VKR_WRAP_CLAMP;
}

static int to_topology(u32 _m)
{
	if (_m == GLI_DM_TRIANGLE_STRIP) return VKR_TOPO_STRIP;
	if (_m == GLI_DM_LINES) return VKR_TOPO_LINES;
	return VKR_TOPO_TRIANGLES;
}

/*---------------Combiner-------------*/

typedef struct vk_cprog
{
	gli_cprog base;   /* first: the program is its C face */
	vkr_program *p;
	u32 inputs;       /* CombinerStages.h's bits */
	unsigned groups;
	CombinerValues values;
	/* a key of the journal of before, built at load: the game's once it
	 * draws with it */
	unsigned char pending;
} vk_cprog;

#define CPROG(_p) ((vk_cprog *)(_p))
#define CCPROG(_p) ((const vk_cprog *)(_p))

/* as many as the renderer has programs: a miss allocates nothing; past
 * them (no game comes near) from the heap */
#define VK_CPROG_POOL 4096
typedef union vk_cprog_cell
{
	union vk_cprog_cell *next;
	vk_cprog program;
} vk_cprog_cell;
static vk_cprog_cell s_cprogs[VK_CPROG_POOL];
static vk_cprog_cell *s_cprogFree;
static u32 s_cprogUsed;

static vk_cprog *cprog_take(void)
{
	vk_cprog_cell *c = s_cprogFree;
	if (c != NULL) {
		s_cprogFree = c->next;
		return &c->program;
	}
	if (s_cprogUsed < VK_CPROG_POOL)
		return &s_cprogs[s_cprogUsed++].program;
	return (vk_cprog *)malloc(sizeof(vk_cprog));
}

static void cprog_give(vk_cprog *_p)
{
	vk_cprog_cell *c = (vk_cprog_cell *)_p;
	if (c >= s_cprogs && c < s_cprogs + VK_CPROG_POOL) {
		c->next = s_cprogFree;
		s_cprogFree = c;
	} else
		free(_p);
}

/* The game's cache file (vk_cache.h): open from the renderer's start
 * until its keys are read, then appended to by the key journal; written
 * at the end where the pipeline cache changed or a key missed the
 * journal. */
static struct {
	char path[PLUGIN_PATH_SIZE + 64];
	vk_cache file;
	int dirty;
	/* the keys of before being built */
	int marking;
	combiner_journal_t *journal;
	/* the game's keys at the end, as records */
	unsigned char *records;
	size_t recordsSize;
	int haveRecords;
} s_cache;

/* a key of before drawn with: the game's, into its file */
static void met(vk_cprog *_c)
{
	_c->pending = 0;
	combiner_journal_push(s_cache.journal, _c->base.key, gli_combiner_key_hwl(_c->base.key) ? 1 : 0);
}

static void cprog_activate(void *_p)
{
	if (CPROG(_p)->pending)
		met(CPROG(_p));
	vkr_use(g_r, CPROG(_p)->p);
}

static void cprog_update(void *_p, bool _force)
{
	vk_cprog *c = CPROG(_p);
	/* forced at the load, which builds it */
	if (c->pending && !_force)
		met(c);
	vkr_use(g_r, c->p);
	updateCombinerValues(&c->values, c->groups);
	vkr_values(g_r, c->p, &c->values);
}

static bool cprog_uses_lod(const void *_p) { return gli_inputs_lod(CCPROG(_p)->inputs); }

static void cprog_free(void *_p)
{
	vkr_program_delete(g_r, CPROG(_p)->p);
	cprog_give(CPROG(_p));
}

static bool cprog_uses_texture(const void *_p) { return gli_inputs_texture(CCPROG(_p)->inputs); }
static bool cprog_uses_tile(const void *_p, u32 _t) { return gli_inputs_tile(CCPROG(_p)->inputs, _t); }
static bool cprog_uses_shade(const void *_p) { return gli_inputs_shade(CCPROG(_p)->inputs); }
static bool cprog_uses_hw_lighting(const void *_p) { return gli_inputs_hw_lighting(CCPROG(_p)->inputs); }

static const gli_cprog_ops cprog_ops = {
	cprog_activate, cprog_update, cprog_uses_lod, cprog_free,
	cprog_uses_texture, cprog_uses_tile, cprog_uses_shade, cprog_uses_hw_lighting, NULL, NULL
};

static gli_cprog *build_combiner(void *_impl, struct Combiner *_color, struct Combiner *_alpha, u64 _key)
{
	gDPCombine combine;
	const u32 cycle = gli_combiner_key_cycle_type(_key);
	const bool isRect = gli_combiner_key_is_rect(_key);
	const u32 bilerp = gli_combiner_key_bilerp(_key);
	vkr_combiner_desc desc;
	CombinerNeeds needs;
	vkr_program *p;
	vk_cprog *c;
	u32 inputs;
	bool hwLight;
	int flags = 0;
	int i;
	(void)_impl;

	combine.mux = _key;
	memset(&desc, 0, sizeof(desc));

	if (cycle != G_CYC_2CYCLE) {
		gli_stage_correct_first(&_alpha->stage[0]);
		gli_stage_correct_first(&_color->stage[0]);
	}
	inputs = gli_inputs_add_stage(gli_inputs_add_stage(0, &_alpha->stage[0]), &_color->stage[0]);

	if (gli_combined_alpha_c(cycle, &combine)) flags |= VKR_CF_ALPHA_C;
	else if (gli_combined_alpha_abd(cycle, &combine)) flags |= VKR_CF_ALPHA_ABD;
	if (gli_combined_color_c(cycle, &combine)) flags |= VKR_CF_COLOR_C;
	else if (gli_combined_color_abd(cycle, &combine)) flags |= VKR_CF_COLOR_ABD;

	gli_stage_encode(&_alpha->stage[0], &desc.ops[0]);
	gli_stage_encode(&_color->stage[0], &desc.ops[4]);
	for (i = 8; i < 16; ++i)
		desc.ops[i] = -1;

	if (cycle == G_CYC_2CYCLE && _alpha->numStages == 2) {
		gli_stage_correct_second(&_alpha->stage[1]);
		gli_stage_correct_second(&_color->stage[1]);
		inputs = gli_inputs_add_stage(gli_inputs_add_stage(inputs, &_alpha->stage[1]), &_color->stage[1]);
		gli_stage_encode(&_alpha->stage[1], &desc.ops[8]);
		gli_stage_encode(&_color->stage[1], &desc.ops[12]);
		flags |= VKR_CF_TWO_STAGES;
	}

	hwLight = !isRect && gli_isHWLightingAllowed() && gli_inputs_shade_color(inputs);
	if (hwLight)
		inputs |= 1u << G_GCI_HW_LIGHT;

	if (gli_inputs_texture(inputs)) flags |= VKR_CF_TEXTURED;
	if (gli_inputs_tile(inputs, 0)) flags |= VKR_CF_TILE0;
	if (gli_inputs_tile(inputs, 1)) flags |= VKR_CF_TILE1;
	if (gli_inputs_lod(inputs)) flags |= VKR_CF_LOD;
	if (hwLight) flags |= VKR_CF_HWLIGHT;
	if (bilerp & 2) flags |= VKR_CF_BILERP0;
	if (bilerp & 1) flags |= VKR_CF_BILERP1;
	desc.flags = flags | ((int)cycle << VKR_CF_CYCLE_SHIFT);

	memset(&needs, 0, sizeof(needs));
	needs.usesTexture = gli_inputs_texture(inputs);
	needs.usesLOD = gli_inputs_lod(inputs);
	needs.usesNoise = gli_inputs_noise(inputs);
	needs.usesTile0 = gli_inputs_tile(inputs, 0);
	needs.usesTile1 = gli_inputs_tile(inputs, 1);
	needs.usesHwLighting = gli_inputs_hw_lighting(inputs);
	needs.depthScale = 1;
	needs.cycleType = (int)cycle;
	needs.isRect = isRect;

	p = vkr_combiner_new(g_r, &desc, isRect ? 1 : 0);
	if (p == NULL)
		return NULL;
	c = cprog_take();
	if (c == NULL) {
		vkr_program_delete(g_r, p);
		return NULL;
	}
	memset(c, 0, sizeof(*c));
	c->base.ops = &cprog_ops;
	c->base.key = _key;
	c->p = p;
	c->inputs = inputs;
	c->groups = combinerValueGroups(&needs);
	c->pending = (unsigned char)(s_cache.marking != 0);
	return &c->base;
}

/*---------------Special shaders-------------*/

enum { VK_SPECIAL_PLAIN, VK_SPECIAL_SHADOW_MAP, VK_SPECIAL_FXAA, VK_SPECIAL_TEXRECT_DRAW };

typedef struct vk_special
{
	gli_cprog base;   /* first: the program is its C face */
	vkr_program *p;
	int kind;
	u16 width, height; /* FXAA: the shown buffer's, kept when it is gone */
	int alpha;         /* the texrect drawer's alpha test */
} vk_special;

#define SPECIAL(_p) ((vk_special *)(_p))

static void special_activate(void *_p)
{
	vk_special *s = SPECIAL(_p);
	vkr_use(g_r, s->p);
	switch (s->kind) {
	case VK_SPECIAL_SHADOW_MAP:
		gDP.changed |= CHANGED_COMBINE;
		vkr_blend(g_r, VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA);
		vkr_special_params(g_r, s->p, gDP.fogColor.r, gDP.fogColor.g, gDP.fogColor.b, gDP.fogColor.a);
		gli_palette_update();
		break;
	case VK_SPECIAL_FXAA:
		{
			const FrameBuffer *pBuffer;
			gDP.changed |= CHANGED_COMBINE;
			pBuffer = gli_fb_find(*REG.VI_ORIGIN);
			if (pBuffer != NULL && pBuffer->fb.m_pTexture != NULL) {
				s->width = pBuffer->fb.m_pTexture->realWidth;
				s->height = pBuffer->fb.m_pTexture->realHeight;
			}
			vkr_special_params(g_r, s->p, (f32)s->width, (f32)s->height, 0.f, 0.f);
		}
		break;
	case VK_SPECIAL_TEXRECT_DRAW:
		{
			const f32 depth = gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : 0.0f;
			vkr_special_params(g_r, s->p, (f32)s->alpha, depth, 0.f, 0.f);
			gDP.changed |= CHANGED_COMBINE;
		}
		break;
	default:
		gDP.changed |= CHANGED_COMBINE;
		break;
	}
}

static void special_update(void *_p, bool _force) { (void)_p; (void)_force; }
static bool special_uses_lod(const void *_p) { (void)_p; return 0; }

static void special_free(void *_p)
{
	vkr_program_delete(g_r, SPECIAL(_p)->p);
	free(_p);
}

static bool special_uses_texture(const void *_p) { (void)_p; return 1; }
static bool special_uses_tile(const void *_p, u32 _t) { (void)_p; return _t == 0; }
static bool special_uses_shade(const void *_p) { (void)_p; return 0; }
static bool special_uses_hw_lighting(const void *_p) { (void)_p; return 0; }
static void special_texture_size(void *_p, u32 _w, u32 _h) { (void)_p; (void)_w; (void)_h; }

static void special_alpha_test(void *_p, int _enable)
{
	SPECIAL(_p)->alpha = _enable;
	gDP.changed |= CHANGED_COMBINE;
}

static const gli_cprog_ops special_ops = {
	special_activate, special_update, special_uses_lod, special_free,
	special_uses_texture, special_uses_tile, special_uses_shade, special_uses_hw_lighting, NULL, NULL
};

static const gli_cprog_ops texrect_draw_ops = {
	special_activate, special_update, special_uses_lod, special_free,
	special_uses_texture, special_uses_tile, special_uses_shade, special_uses_hw_lighting,
	special_texture_size, special_alpha_test
};

/* made at load, with the context */
static gli_cprog *special_new(int _kind, int _vs, int _fs)
{
	vk_special *s = (vk_special *)calloc(1, sizeof(*s));
	if (s == NULL)
		return NULL;
	s->base.ops = _kind == VK_SPECIAL_TEXRECT_DRAW ? &texrect_draw_ops : &special_ops;
	s->p = vkr_special_new(g_r, _vs, _fs);
	s->kind = _kind;
	return &s->base;
}

static gli_cprog *depth_fog_shader(void *_impl)
{
	(void)_impl;
	return special_new(VK_SPECIAL_SHADOW_MAP, VKR_VS_SPECIAL, VKR_FS_SHADOW_MAP);
}

static gli_cprog *texrect_draw_shader(void *_impl)
{
	(void)_impl;
	return special_new(VK_SPECIAL_TEXRECT_DRAW, VKR_VS_SPECIAL, VKR_FS_TEXRECT_DRAW);
}

static gli_cprog *texrect_clear_shader(void *_impl)
{
	(void)_impl;
	return special_new(VK_SPECIAL_PLAIN, VKR_VS_SPECIAL, VKR_FS_TEXRECT_CLEAR);
}

static gli_cprog *texrect_copy_shader(void *_impl)
{
	(void)_impl;
	return special_new(VK_SPECIAL_PLAIN, VKR_VS_SPECIAL, VKR_FS_TEXRECT_COPY);
}

static gli_cprog *gamma_correction_shader(void *_impl)
{
	gli_cprog *s;
	(void)_impl;
	s = special_new(VK_SPECIAL_PLAIN, VKR_VS_SPECIAL, VKR_FS_GAMMA);
	if (s != NULL) {
		const f32 level = (config.gammaCorrection.force != 0) ? config.gammaCorrection.level : 2.0f;
		vkr_special_params(g_r, SPECIAL(s)->p, level, 0.f, 0.f, 0.f);
	}
	return s;
}

static gli_cprog *fxaa_shader(void *_impl)
{
	(void)_impl;
	return special_new(VK_SPECIAL_FXAA, VKR_VS_FXAA, VKR_FS_FXAA);
}

/*---------------Context-------------*/

static void window_size(void)
{
	vkr_set_window(g_r, g_window.screenWidth, g_window.screenHeight);
}

/* every game's keys of before, the pipeline cache beside them */
#define OLD_JOURNAL "/GLideN64.vulkan.keys.journal"
#define OLD_JOURNAL_VERSION 1u

/* GLideN64.<name>.Vulkan.cache in the shaders folder, named as GL's is */
static void cache_path(char *_path, size_t _size)
{
	char name[48];
	size_t len;

	gli_api_user_cache_path(_path, _size);
	len = strlen(_path);
	strlcat(_path, "/shaders", _size);
	/* the folder made, or the file beside it */
	if (!path_mkdir(_path))
		_path[len] = '\0';
	sprintf(name, "/GLideN64.%x.Vulkan.cache", (unsigned)gli_std_hash_string(RSP.romname));
	strlcat(_path, name, _size);
}

static void create(vk_context *_c, const void *_pipelines, size_t _size)
{
	vkr_shader_config cfg;

	_c->optionsBits = gli_combiner_options_bits();
	memset(&cfg, 0, sizeof(cfg));
	cfg.legacy_blending = config.generalEmulation.enableLegacyBlending != 0;
	cfg.noise = config.generalEmulation.enableNoise != 0;
	cfg.fragment_depth_write = config.generalEmulation.enableFragmentDepthWrite != 0;
	cfg.n64_depth_compare = config.frameBufferEmulation.N64DepthCompare != 0;
	cfg.enable_lod = config.generalEmulation.enableLOD != 0;
	cfg.filter_mode = (int)(config.texture.bilinearMode + config.texture.enableHalosRemoval * 2);
	cfg.three_point = config.texture.bilinearMode == BILINEAR_3POINT;
	cfg.msaa = (int)config.video.multisampling;
	cfg.noise_width = NOISE_TEX_WIDTH;
	cfg.noise_height = NOISE_TEX_HEIGHT;
	cfg.interlock = cfg.n64_depth_compare && gliden64_vulkan_has_interlock();
	g_r = vkr_create_lent(gliden64_vulkan_interface(), &cfg, _pipelines, _size);
	if (g_r != NULL) {
		window_size();
		/* its pipelines at load, with the rest; a device that cannot
		 * make them draws as without one */
		if (gliden64_upscaler_offered() && !vkr_upscaler_init(g_r))
			gliden64_upscaler_set_interface(NULL);
	}
}

static void free_impl(void *_p)
{
	free(_p);
}

/* The game's file opened, its pipeline cache lent to the renderer. A
 * game without one is to be written; the pipeline cache of before goes,
 * read by nothing now. */
static void init(void *_p)
{
	memset(&s_cache, 0, sizeof(s_cache));
	cache_path(s_cache.path, sizeof(s_cache.path));
	vk_cache_open(&s_cache.file, s_cache.path);
	if (!s_cache.file.found) {
		char old[PLUGIN_PATH_SIZE + 64];
		s_cache.dirty = 1;
		gli_api_user_cache_path(old, sizeof(old));
		strlcat(old, OLD_JOURNAL ".pipelines", sizeof(old));
		if (filestream_exists(old))
			filestream_delete(old);
	}
	create(CTX(_p), s_cache.file.pipelines, s_cache.file.pipelines_size);
}

/* The renderer gone, the file written where the pipeline cache changed,
 * a key missed it or it was not there: a session that met nothing new
 * writes nothing. Its keys from the end, or from the file still open
 * where the storage was off. */
static void destroy(void *_p)
{
	size_t size = 0;
	int changed = 0;
	void *pipelines = vkr_destroy_taking_cache(g_r, &size, &changed);
	(void)_p;
	g_r = NULL;
	if (changed)
		s_cache.dirty = 1;
	if (s_cache.dirty && s_cache.haveRecords)
		vk_cache_write(s_cache.path, pipelines, size, s_cache.records, s_cache.recordsSize);
	else if (s_cache.dirty && s_cache.file.found) {
		/* read once, the file let go before it is written */
		unsigned char *records = (unsigned char *)malloc(s_cache.file.records_size + 1);
		const size_t n = s_cache.file.records_size;
		if (records != NULL)
			memcpy(records, s_cache.file.records, n);
		vk_cache_close(&s_cache.file, NULL);
		if (records != NULL)
			vk_cache_write(s_cache.path, pipelines, size, records, n);
		free(records);
	} else if (s_cache.dirty && config.generalEmulation.enableShadersStorage == 0)
		/* none there, and no keys to keep */
		vk_cache_write(s_cache.path, pipelines, size, NULL, 0);
	vk_cache_close(&s_cache.file, NULL);
	free(pipelines);
	free(s_cache.records);
	memset(&s_cache, 0, sizeof(s_cache));
}

static void set_clamp_mode(void *_p, u32 _mode)
{
	CTX(_p)->clampMode = _mode;
	switch (_mode) {
	case VK_CLAMP_CLIPPING: vkr_set_clamp(g_r, 0, 0); break;
	case VK_CLAMP_NO_NEAR_PLANE: vkr_set_clamp(g_r, 1, 1); break;
	case VK_CLAMP_NONE: vkr_set_clamp(g_r, 1, 0); break;
	}
}

static u32 get_clamp_mode(void *_p)
{
	return CTX(_p)->clampMode;
}

static int to_cap(u32 _p)
{
	switch (_p) {
	case GLI_EN_BLEND: return VKR_CAP_BLEND;
	case GLI_EN_CULL_FACE: return VKR_CAP_CULL;
	case GLI_EN_DEPTH_TEST: return VKR_CAP_DEPTH_TEST;
	case GLI_EN_POLYGON_OFFSET_FILL: return VKR_CAP_POLY_OFFSET;
	case GLI_EN_SCISSOR_TEST: return VKR_CAP_SCISSOR;
	}
	return -1;
}

static void enable(void *_p, u32 _parameter, bool _enable)
{
	const int cap = to_cap(_parameter);
	(void)_p;
	if (cap >= 0)
		vkr_enable(g_r, cap, _enable);
}

static u32 is_enabled(void *_p, u32 _parameter)
{
	const int cap = to_cap(_parameter);
	(void)_p;
	return cap >= 0 ? (u32)vkr_is_enabled(g_r, cap) : 0;
}

static void cull_face(void *_p, u32 _mode)
{
	(void)_p;
	vkr_cull(g_r, _mode == GLI_CULLMODE_FRONT ? VKR_CULL_FRONT : (_mode == GLI_CULLMODE_BACK ? VKR_CULL_BACK : VKR_CULL_BOTH));
}

static void enable_depth_write(void *_p, bool _enable) { (void)_p; vkr_depth_write(g_r, _enable); }

static void set_depth_compare(void *_p, u32 _mode)
{
	(void)_p;
	vkr_depth_compare(g_r, _mode == GLI_COMPARE_LESS ? VKR_CMP_LESS : (_mode == GLI_COMPARE_ALWAYS ? VKR_CMP_ALWAYS : VKR_CMP_LEQUAL));
}

static void set_viewport(void *_p, s32 _x, s32 _y, s32 _w, s32 _h) { (void)_p; vkr_viewport(g_r, _x, _y, _w, _h); }
static void set_scissor(void *_p, s32 _x, s32 _y, s32 _w, s32 _h) { (void)_p; vkr_scissor(g_r, _x, _y, _w, _h); }

static void set_blending(void *_p, u32 _s, u32 _d)
{
	const int s = to_blend(_s), d = to_blend(_d);
	(void)_p;
	vkr_blend(g_r, s, d, s, d);
}

static void set_blending_separate(void *_p, u32 _s, u32 _d, u32 _sa, u32 _da)
{
	(void)_p;
	vkr_blend(g_r, to_blend(_s), to_blend(_d), to_blend(_sa), to_blend(_da));
}

static void set_blend_color(void *_p, f32 _r, f32 _g, f32 _b, f32 _a) { (void)_p; vkr_blend_color(g_r, _r, _g, _b, _a); }
static void clear_color_buffer(void *_p, f32 _r, f32 _g, f32 _b, f32 _a) { (void)_p; vkr_clear_color(g_r, _r, _g, _b, _a); }
static void clear_depth_buffer(void *_p) { (void)_p; vkr_clear_depth(g_r); }
static void set_polygon_offset(void *_p, f32 _f, f32 _u) { (void)_p; vkr_polygon_offset(g_r, _f, _u); }

static void bind_unit(u32 _unit, u32 _h)
{
	const int u = to_unit(_unit);
	if (u < VKR_BIND_COUNT)
		vkr_bind(g_r, u, _h);
}

static u32 create_texture(void *_p, u32 _target) { (void)_p; (void)_target; return vkr_texture_new(g_r); }
static void delete_texture(void *_p, u32 _name) { (void)_p; vkr_texture_delete(g_r, _name); }

static void init_2d_texture(void *_p, const gli_init_texture_params *_params)
{
	const int fmt = to_format(_params->internalFormat);
	const u32 samples = _params->msaaLevel > 0 ? _params->msaaLevel : 1;
	(void)_p;
	vkr_texture_init(g_r, _params->handle, fmt, samples, _params->width, _params->height, _params->mipMapLevel,
		_params->mipMapLevels, _params->data,
		_params->data != NULL ? (size_t)_params->width * _params->height * format_bytes(fmt) : 0);
	if (_params->textureUnitIndex != GLI_PARAM_INVALID)
		bind_unit(_params->textureUnitIndex, _params->handle);
}

static void update_2d_texture(void *_p, const gli_update_texture_params *_params)
{
	const int fmt = to_format(_params->internalFormat);
	(void)_p;
	vkr_texture_update(g_r, _params->handle, _params->mipMapLevel, _params->x, _params->y, _params->width,
		_params->height, _params->data, (size_t)_params->width * _params->height * format_bytes(fmt));
}

static void *lend_texture_data(void *_p, size_t _bytes) { (void)_p; return vkr_texture_lend(g_r, _bytes); }

static void set_texture_parameters(void *_p, const gli_tex_parameters *_params)
{
	const int mag = _params->magFilter != GLI_PARAM_INVALID ? (_params->magFilter == GLI_TP_FILTER_LINEAR ? 1 : 0) : VKR_KEEP;
	const int wrapS = _params->wrapS != GLI_PARAM_INVALID ? to_wrap(_params->wrapS) : VKR_KEEP;
	const int wrapT = _params->wrapT != GLI_PARAM_INVALID ? to_wrap(_params->wrapT) : VKR_KEEP;
	const f32 maxLod = _params->maxMipmapLevel != GLI_PARAM_INVALID ? (f32)(s32)_params->maxMipmapLevel : -1.f;
	const f32 aniso = _params->maxAnisotropy > 0.0f ? _params->maxAnisotropy : -1.f;
	int min = VKR_KEEP;
	(void)_p;
	if (_params->minFilter != GLI_PARAM_INVALID)
		min = (_params->minFilter == GLI_TP_FILTER_LINEAR || _params->minFilter == GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST) ? 1 : 0;
	vkr_texture_params(g_r, _params->handle, mag, min, VKR_KEEP, wrapS, wrapT, maxLod, aniso);
	if (_params->textureUnitIndex != GLI_PARAM_INVALID)
		bind_unit(_params->textureUnitIndex, _params->handle);
}

static void bind_texture(void *_p, const gli_bind_texture_params *_params)
{
	(void)_p;
	bind_unit(_params->textureUnitIndex, _params->texture);
}

static void set_texture_unpack_alignment(void *_p, s32 _param) { CTX(_p)->unpack = _param; }
static s32 get_texture_unpack_alignment(const void *_p) { return CCTX(_p)->unpack; }
static s32 get_max_texture_size(const void *_p) { (void)_p; return vkr_get_caps(g_r)->max_texture_size; }
static f32 get_max_anisotropy(const void *_p) { (void)_p; return vkr_get_caps(g_r)->max_anisotropy; }

static void bind_image_texture(void *_p, const gli_bind_image_texture_params *_params)
{
	(void)_p;
	vkr_bind_image(g_r, _params->imageUnit == GLI_IU_DepthZ ? VKR_BIND_DEPTH_Z : VKR_BIND_DEPTH_DZ, _params->texture);
}

static u32 convert_internal_texture_format(const void *_p, u32 _format) { (void)_p; return _format; }
static void texture_barrier(void *_p) { (void)_p; }
static void get_framebuffer_texture_formats(void *_p, gli_fb_formats *_formats) { (void)_p; gli_fb_formats_vk(_formats); }

static u32 create_framebuffer(void *_p) { (void)_p; return vkr_fbo_new(g_r); }
static void delete_framebuffer(void *_p, u32 _name) { (void)_p; vkr_fbo_delete(g_r, _name); }

static void bind_framebuffer(void *_p, u32 _target, u32 _name)
{
	(void)_p;
	if (_name == 0)
		window_size();
	if (_target == GLI_BT_READ_FRAMEBUFFER) vkr_fbo_bind(g_r, 1, 0, _name);
	else if (_target == GLI_BT_DRAW_FRAMEBUFFER) vkr_fbo_bind(g_r, 0, 1, _name);
	else vkr_fbo_bind(g_r, 1, 1, _name);
}

static void add_render_target(void *_p, const gli_fb_render_target *_params)
{
	/* attaching binds, as it does in GL: a new buffer is drawn to without another bind */
	bind_framebuffer(_p, _params->bufferTarget, _params->bufferHandle);
	if (_params->attachment == GLI_BA_COLOR_ATTACHMENT0)
		vkr_fbo_color(g_r, _params->bufferHandle, _params->textureHandle);
	else if (_params->attachment == GLI_BA_DEPTH_ATTACHMENT)
		vkr_fbo_depth(g_r, _params->bufferHandle, _params->textureHandle);
}

static u32 create_renderbuffer(void *_p) { (void)_p; return vkr_texture_new(g_r); }
static void delete_renderbuffer(void *_p, u32 _name) { (void)_p; vkr_texture_delete(g_r, _name); }

static void init_renderbuffer(void *_p, const gli_init_renderbuffer_params *_params)
{
	(void)_p;
	vkr_texture_init(g_r, _params->handle, to_format(_params->format), 1, _params->width, _params->height, 0, 1, NULL, 0);
}

static bool blit_framebuffers(void *_p, const gli_blit_params *_params)
{
	const int color = (_params->mask & GLI_BM_COLOR_BUFFER) != 0;
	const int depth = (_params->mask & GLI_BM_DEPTH_BUFFER) != 0;
	(void)_p;
	window_size();
	return vkr_blit(g_r, _params->readBuffer, _params->drawBuffer, _params->srcX0, _params->srcY0, _params->srcX1,
		_params->srcY1, _params->dstX0, _params->dstY0, _params->dstX1, _params->dstY1, color, depth,
		_params->filter == GLI_TP_FILTER_LINEAR ? 1 : 0) != 0;
}

static void set_draw_buffers(void *_p, u32 _num) { (void)_p; (void)_num; }
static struct gli_prb *create_pixel_read_buffer(void *_p, size_t _bytes) { (void)_p; return gli_vk_prb_new(g_r, _bytes); }

static void *create_color_buffer_reader(void *_p, void *_texture)
{
	(void)_p;
	return gli_vk_cbr_new(g_r, (struct gli_tex *)_texture);
}

static bool builder_obsolete(void *_p)
{
	return CTX(_p)->optionsBits != gli_combiner_options_bits();
}

static void reset_builder(void *_p)
{
	size_t size = 0;
	int changed = 0;
	void *pipelines;
	if (!builder_obsolete(_p))
		return;
	/* the pipeline cache handed to the new renderer */
	pipelines = vkr_destroy_taking_cache(g_r, &size, &changed);
	if (changed)
		s_cache.dirty = 1;
	create(CTX(_p), pipelines, size);
	free(pipelines);
}

static bool load_shaders_storage(void *_p, struct gli_combiner_table *_combiners) { (void)_p; (void)_combiners; return 0; }

/* The game's keys built, the file let go and appended to. A game without
 * its file builds the keys of every game in the journal of before, as it
 * did, and keeps those it draws with; its file is written now, for the
 * journal to append to. */
static struct combiner_journal *open_combiner_journal(void *_p,
	void (*_known)(void *_ctx, uint64_t _key, unsigned _hwl), void *_ctx)
{
	(void)_p;
	if (s_cache.file.found)
		vk_cache_keys(&s_cache.file, _known, _ctx);
	else {
		char old[PLUGIN_PATH_SIZE + 64];
		gli_api_user_cache_path(old, sizeof(old));
		strlcat(old, OLD_JOURNAL, sizeof(old));
		s_cache.marking = 1;
		combiner_journal_read(old, OLD_JOURNAL_VERSION, _known, _ctx);
		s_cache.marking = 0;
	}
	if (s_cache.file.found)
		vk_cache_close(&s_cache.file, s_cache.path);
	else if (!vk_cache_write(s_cache.path, NULL, 0, NULL, 0))
		return NULL;
	s_cache.journal = combiner_journal_append(s_cache.path);
	return s_cache.journal;
}

/* The journal stopped; the game's keys as records, for a file written at
 * the end: those built for it, not those of before it never drew with */
static bool close_shaders_storage(void *_p, const struct gli_combiner_table *_combiners,
	struct combiner_journal *_journal)
{
	int whole;
	RFILE *f = combiner_journal_detach(_journal, &whole);
	u32 i;
	(void)_p;
	if (f != NULL)
		filestream_close(f);
	s_cache.journal = NULL;
	if (!whole)
		s_cache.dirty = 1;
	free(s_cache.records);
	s_cache.records = NULL;
	s_cache.recordsSize = 0;
	s_cache.haveRecords = 0;
	if (_combiners == NULL)
		return 0;
	s_cache.records = (unsigned char *)malloc((size_t)_combiners->count * COMBINER_JOURNAL_RECORD_SIZE + 1);
	if (s_cache.records == NULL)
		return 0;
	for (i = 0; i < _combiners->count; ++i) {
		const gli_cprog *p = _combiners->slots[_combiners->order[i]].program;
		const u64 key = p->key;
		const u32 cycle = gli_combiner_key_cycle_type(key);
		unsigned char *r = s_cache.records + s_cache.recordsSize;
		/* copy and fill's are built at every start */
		if (p->ops != &cprog_ops || CCPROG(p)->pending || cycle == G_CYC_COPY || cycle == G_CYC_FILL)
			continue;
		r[0] = COMBINER_JOURNAL_RECORD_KEY;
		r[1] = (unsigned char)(gli_combiner_key_hwl(key) ? 1 : 0);
		memcpy(r + 2, &key, sizeof(key));
		s_cache.recordsSize += COMBINER_JOURNAL_RECORD_SIZE;
	}
	s_cache.haveRecords = 1;
	return 1;
}

static void reset_shader_program(void *_p) { (void)_p; vkr_use(g_r, NULL); }

/* For a temporal upscaler: where what is drawn now goes, when that is
 * the N64's color image */
static bool upscaler_view(gliden64_upscaler_view *_view, unsigned *_state)
{
	const FrameBuffer *pDrawn;
	const gli_fb *pCurrent;
	f32 scaleX, scaleY;
	if (!gliden64_upscaler_active())
		return 0;
	pDrawn = gli_fb_drawn();
	if (pDrawn == NULL || vkr_draw_fbo(g_r) != pDrawn->fb.m_FBO)
		return 0;
	pCurrent = &pDrawn->fb;
	gliden64_upscaler_color_image(pCurrent->m_startAddress, pCurrent->m_width, (int)pCurrent->m_size - 1);
	gli_fb_coords_scales(pDrawn, &scaleX, &scaleY);
	vkr_get_viewport(g_r, _view->viewport);
	_view->screen_scale[0] = 2.0f * scaleX;
	_view->screen_scale[1] = -2.0f * scaleY;
	_view->scale = pCurrent->m_scale;
	*_state = vkr_draw_state(g_r, &_view->cull);
	return 1;
}

/* Kept for the motion vectors, and moved by the frame's jitter */
static void upscaler_triangles(const vkr_tri_vertex *_v, u32 _count, const u16 *_elements, u32 _elementsCount, bool _strip)
{
	gliden64_upscaler_view view;
	unsigned state = 0;
	unsigned flags = 0;
	uint64_t signature;
	const struct gli_tex *pTexture;
	const gli_cprog *current;
	float jitter[2];
	if (!upscaler_view(&view, &state))
		return;
	/* the N64's own depth compare runs in the shader */
	if (config.frameBufferEmulation.N64DepthCompare != 0)
		state = (state & 4) | (gDP.otherMode.depthCompare != 0 ? 1 : 0) | (gDP.otherMode.depthUpdate != 0 ? 2 : 0);
	if (state & 1)
		flags |= RDP_UPSCALER_DEPTH_TEST;
	if (state & 2)
		flags |= RDP_UPSCALER_DEPTH_WRITE;
	else if (state & 4)
		flags |= RDP_UPSCALER_TRANSLUCENT;
	/* triangles drawn alike: the same combiner, modes and texture */
	signature = rdp_upscaler_hash_init();
	signature = rdp_upscaler_hash(signature, (u32)gDP.combine.mux);
	signature = rdp_upscaler_hash(signature, (u32)(gDP.combine.mux >> 32));
	signature = rdp_upscaler_hash(signature, (u32)gDP.otherMode._u64);
	signature = rdp_upscaler_hash(signature, (u32)(gDP.otherMode._u64 >> 32));
	pTexture = gli_texture_current[0];
	current = gli_cmb.current;
	if (pTexture != NULL && current != NULL && current->ops->usesTile(current, 0)) {
		signature = rdp_upscaler_hash(signature, (u32)pTexture->crc);
		signature = rdp_upscaler_hash(signature, (u32)(pTexture->crc >> 32));
	}
	gliden64_upscaler_triangles(_v, _count, _elements, _elementsCount, _strip, &view, signature, flags, jitter);
	vkr_jitter(g_r, jitter[0], jitter[1]);
}

static void draw_triangles(void *_p, const gli_draw_triangles_params *_params)
{
	/* converted straight into the ring the GPU reads */
	vkr_tri_vertex *dst = vkr_tri_reserve(g_r, _params->verticesCount);
	const f32 lights = gli_isHWLightingAllowed() ? (f32)_params->vertices[0].HWLight : 0.f;
	const u32 elements = _params->elements != NULL ? _params->elementsCount : 0;
	int topology;
	u32 i;
	(void)_p;
	for (i = 0; i < _params->verticesCount; ++i) {
		const SPVertex *src = &_params->vertices[i];
		vkr_tri_vertex *d = &dst[i];
		d->x = src->x; d->y = src->y; d->z = src->z; d->w = src->w;
		if (_params->flatColors) {
			d->r = src->flat_r; d->g = src->flat_g; d->b = src->flat_b; d->a = src->flat_a;
		} else {
			d->r = src->r; d->g = src->g; d->b = src->b; d->a = src->a;
		}
		d->s = src->s; d->t = src->t;
		d->modify = src->modify;
		d->num_lights = lights;
	}
	if (_params->elements != NULL) {
		uint16_t *idx = vkr_index_reserve(g_r, _params->elementsCount);
		memcpy(idx, _params->elements, _params->elementsCount * sizeof(uint16_t));
	}
	topology = to_topology(_params->mode);
	upscaler_triangles(dst, _params->verticesCount, (const u16 *)_params->elements, elements, topology == VKR_TOPO_STRIP);
	vkr_draw_tris(g_r, topology, _params->verticesCount, elements);
	vkr_jitter(g_r, 0.0f, 0.0f);
}

static void draw_rects(void *_p, const gli_draw_rects_params *_params)
{
	vkr_rect_vertex *dst = vkr_rect_reserve(g_r, _params->verticesCount);
	gliden64_upscaler_view view;
	unsigned state = 0;
	(void)_p;
	memcpy(dst, _params->vertices, _params->verticesCount * sizeof(RectVertex));
	if (!_params->texrect) {
		/* GL reads the texture coordinates of such a rect as 0 */
		u32 i;
		for (i = 0; i < _params->verticesCount; ++i)
			dst[i].s0 = dst[i].t0 = dst[i].s1 = dst[i].t1 = 0.f;
	}
	/* (a rectangle stands still: over what moves under it) */
	if (upscaler_view(&view, &state))
		gliden64_upscaler_rect(dst, _params->verticesCount, &view);
	vkr_draw_rects(g_r, to_topology(_params->mode), _params->verticesCount);
}

static void draw_line(void *_p, f32 _width, void *_vertices)
{
	const SPVertex *v = (const SPVertex *)_vertices;
	vkr_tri_vertex *dst = vkr_tri_reserve(g_r, 2);
	u32 i;
	(void)_p;
	for (i = 0; i < 2; ++i) {
		dst[i].x = v[i].x; dst[i].y = v[i].y; dst[i].z = v[i].z; dst[i].w = v[i].w;
		dst[i].r = v[i].r; dst[i].g = v[i].g; dst[i].b = v[i].b; dst[i].a = v[i].a;
		dst[i].s = v[i].s; dst[i].t = v[i].t;
		dst[i].modify = v[i].modify;
		dst[i].num_lights = 0.f;
	}
	vkr_draw_line(g_r, 2, _width);
}

static f32 get_max_line_width(void *_p) { (void)_p; return vkr_get_caps(g_r)->max_line_width; }
static s32 get_max_msaa_level(void *_p) { (void)_p; return vkr_get_caps(g_r)->max_samples; }

static bool is_supported(const void *_p, u32 _feature)
{
	(void)_p;
	switch (_feature) {
	case GLI_FEATURE_MULTISAMPLING:
	case GLI_FEATURE_BLIT_FRAMEBUFFER:
	case GLI_FEATURE_DEPTH_FRAMEBUFFER_TEXTURES:
	case GLI_FEATURE_INTEGER_TEXTURES:
	case GLI_FEATURE_CLIP_CONTROL:
		return 1;
	case GLI_FEATURE_IMAGE_TEXTURES:
		return vkr_get_caps(g_r)->storage_images != 0;
	}
	return 0;
}

static bool is_error(const void *_p) { (void)_p; return 0; }
static bool is_framebuffer_error(const void *_p) { (void)_p; return 0; }

/* in gli_context_ops's order */
static const gli_context_ops vk_ops = {
	free_impl, init, destroy, set_clamp_mode, get_clamp_mode, enable, is_enabled, cull_face,
	enable_depth_write, set_depth_compare, set_viewport, set_scissor, set_blending, set_blending_separate,
	set_blend_color, clear_color_buffer, clear_depth_buffer, set_polygon_offset,
	create_texture, delete_texture, init_2d_texture, update_2d_texture, lend_texture_data,
	set_texture_parameters, bind_texture, set_texture_unpack_alignment, get_texture_unpack_alignment,
	get_max_texture_size, get_max_anisotropy, bind_image_texture, convert_internal_texture_format, texture_barrier,
	get_framebuffer_texture_formats, create_framebuffer, delete_framebuffer, bind_framebuffer,
	add_render_target, create_renderbuffer, delete_renderbuffer, init_renderbuffer, blit_framebuffers, set_draw_buffers,
	create_pixel_read_buffer, create_color_buffer_reader,
	builder_obsolete, reset_builder,
	build_combiner, build_combiner, load_shaders_storage,
	open_combiner_journal, close_shaders_storage, depth_fog_shader, texrect_draw_shader,
	texrect_clear_shader, texrect_copy_shader, gamma_correction_shader,
	fxaa_shader,
	reset_shader_program,
	draw_triangles, draw_rects, draw_line, get_max_line_width, get_max_msaa_level, is_supported, is_error,
	is_framebuffer_error
};

const gli_context_ops *gli_vk_context_ops(void)
{
	return &vk_ops;
}

void *gli_vk_context_new(void)
{
	vk_context *c = (vk_context *)calloc(1, sizeof(*c));
	if (c != NULL) {
		c->clampMode = VK_CLAMP_CLIPPING;
		c->unpack = 1;
	}
	return c;
}

/* What framebuffer 0 holds goes to the frontend, as this frame's image. */
void gliden64_vulkan_present(void)
{
	vkr_upscaler_frame frame;
	if (g_r == NULL)
		return;
	if (gliden64_upscaler_frame(&frame))
		vkr_present_upscaled(g_r, &frame);
	else
		vkr_present(g_r);
}

void gliden64_vulkan_presented_size(unsigned *_width, unsigned *_height)
{
	*_width = *_height = 0;
	if (g_r != NULL)
		vkr_presented_size(g_r, _width, _height);
}

/* The buffer the VI shows, and where the window has it */
void gliden64_vulkan_shown(unsigned _address, unsigned _width, unsigned _size,
	unsigned _height, float _scale, const int *_src, const int *_dst)
{
	float map[4];
	if (!gliden64_upscaler_active() || _src[2] == _src[0] || _src[3] == _src[1])
		return;
	map[0] = (float)(_dst[2] - _dst[0]) / (float)(_src[2] - _src[0]);
	map[1] = (float)(_dst[3] - _dst[1]) / (float)(_src[3] - _src[1]);
	map[2] = (float)_dst[0] - (float)_src[0] * map[0];
	map[3] = (float)_dst[1] - (float)_src[1] * map[1];
	gliden64_upscaler_shown(_address, _width, (int)_size - 1, _width, _height, _scale, map);
}
