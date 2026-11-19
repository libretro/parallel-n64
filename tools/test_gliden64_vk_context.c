/* test_gliden64_vk_context -- GLideN64's Vulkan context (vk_ContextImpl.c)
 * and the combiner stage helpers (CombinerStages.c), on a stub renderer.
 *
 * Made, started with the shader config from the options and the pipeline
 * cache beside the keys journal, the window sized, the upscaler started or
 * dropped; stopped. Each state call mapped to the renderer: clamp modes,
 * capabilities, culling, depth, viewport, scissor, blending, clears,
 * polygon offset. Textures and render buffers made, filled with their
 * byte counts, updated, lent, given parameters, bound to their units;
 * image units; frame buffers bound by target, sizing the window first for
 * the window, attached, blitted. Combiners built from their stages with
 * their flags, inputs and value groups, from a pool, drawn with and freed;
 * the special shaders and their parameters. Triangles, rects and lines
 * written into the ring, the upscaler told what is drawn. The present and
 * the shown buffer's map.
 *
 *   ./test_gliden64_vk_context
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <Config.h>
#include <gDP.h>
#include <gSP.h>
#include <N64.h>
#include <GBI.h>
#include <FrameBuffer.h>
#include <DisplayWindow.h>
#include <CombinerKey.h>
#include <CombinerMux.h>
#include <PluginAPI.h>
#include <PaletteTexture.h>
#include <NoiseTexture.h>
#include <gli_combiner.h>
#include <gli_textures.h>
#include <gli_tex.h>
#include <Graphics/gli_context.h>
#include <Graphics/gli_params.h>
#include <Graphics/CombinerValues.h>
#include <Graphics/CombinerStages.h>
#include "vk_ColorBufferReader.h"
#include "vk_renderer.h"
#include "vk_device.h"
#include "vk_upscaler.h"
#include "vk_ContextImpl.h"

static int failures;
static char log_[16384];

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		if (getenv("VK_TEST_LOG") != NULL)
			printf("  log: %s\n", log_);
		failures++;
	}
}

static void say(const char *_fmt, ...)
{
	char b[256];
	va_list ap;
	va_start(ap, _fmt);
	vsprintf(b, _fmt, ap);
	va_end(ap);
	if (strlen(log_) + strlen(b) + 2 < sizeof(log_)) {
		strcat(log_, b);
		strcat(log_, " ");
	}
}
static void clear_log(void) { log_[0] = '\0'; }
static int has(const char *_s) { return strstr(log_, _s) != NULL; }
static int count_of(const char *_s)
{
	int n = 0;
	const char *p = log_;
	while ((p = strstr(p, _s)) != NULL) {
		n++;
		p += strlen(_s);
	}
	return n;
}

Config gliden64Config;
gDPInfo gDP;
N64Regs REG;
gli_window g_window;
gli_combiner_info gli_cmb;
struct gli_tex *gli_texture_current[2];
static u32 viOrigin;

/* ---- the renderer ---- */

static int renderer_obj;
static vkr_renderer *const RENDERER = (vkr_renderer *)&renderer_obj;
static int createFails;
static vkr_shader_config createdCfg;
static char createdCache[600];
static const void *createdIface;
vkr_renderer *vkr_create(const struct retro_hw_render_interface_vulkan *_vk, const vkr_shader_config *_cfg, const char *_cache)
{
	createdIface = _vk;
	createdCfg = *_cfg;
	strcpy(createdCache, _cache != NULL ? _cache : "(null)");
	say("create");
	return createFails ? NULL : RENDERER;
}
void vkr_destroy(vkr_renderer *_r) { say("destroy:%d", _r == RENDERER); }
static vkr_caps caps = { 8192, 16.0f, 8, 4.0f, 1 };
const vkr_caps *vkr_get_caps(const vkr_renderer *_r) { (void)_r; return &caps; }
void vkr_set_window(vkr_renderer *_r, unsigned _w, unsigned _h) { (void)_r; say("window:%u:%u", _w, _h); }
void vkr_present(vkr_renderer *_r) { (void)_r; say("present"); }
void vkr_presented_size(const vkr_renderer *_r, unsigned *_w, unsigned *_h) { (void)_r; *_w = 11; *_h = 22; }
static int upscalerInitOk = 1;
int vkr_upscaler_init(vkr_renderer *_r) { (void)_r; say("upscaler-init"); return upscalerInitOk; }
static vkr_upscaler_frame presentedFrame;
void vkr_present_upscaled(vkr_renderer *_r, const vkr_upscaler_frame *_u) { (void)_r; presentedFrame = *_u; say("present-upscaled"); }
void vkr_jitter(vkr_renderer *_r, float _x, float _y) { (void)_r; say("jitter:%g:%g", _x, _y); }
void vkr_get_viewport(const vkr_renderer *_r, float *_v) { (void)_r; _v[0] = 1; _v[1] = 2; _v[2] = 3; _v[3] = 4; }
static unsigned drawFbo;
unsigned vkr_draw_fbo(const vkr_renderer *_r) { (void)_r; return drawFbo; }
static unsigned drawState;
unsigned vkr_draw_state(const vkr_renderer *_r, int *_cull) { (void)_r; *_cull = 2; return drawState; }
void vkr_enable(vkr_renderer *_r, int _cap, int _on) { (void)_r; say("enable:%d:%d", _cap, _on); }
int vkr_is_enabled(const vkr_renderer *_r, int _cap) { (void)_r; return _cap + 100; }
void vkr_set_clamp(vkr_renderer *_r, int _dc, int _clip) { (void)_r; say("clamp:%d:%d", _dc, _clip); }
void vkr_cull(vkr_renderer *_r, int _m) { (void)_r; say("cull:%d", _m); }
void vkr_depth_write(vkr_renderer *_r, int _on) { (void)_r; say("depthwrite:%d", _on); }
void vkr_depth_compare(vkr_renderer *_r, int _c) { (void)_r; say("depthcmp:%d", _c); }
void vkr_viewport(vkr_renderer *_r, int _x, int _y, int _w, int _h) { (void)_r; say("viewport:%d:%d:%d:%d", _x, _y, _w, _h); }
void vkr_scissor(vkr_renderer *_r, int _x, int _y, int _w, int _h) { (void)_r; say("scissor:%d:%d:%d:%d", _x, _y, _w, _h); }
void vkr_blend(vkr_renderer *_r, int _s, int _d, int _sa, int _da) { (void)_r; say("blend:%d:%d:%d:%d", _s, _d, _sa, _da); }
void vkr_blend_color(vkr_renderer *_r, float _cr, float _g, float _b, float _a) { (void)_r; say("blendcolor:%g:%g:%g:%g", _cr, _g, _b, _a); }
void vkr_polygon_offset(vkr_renderer *_r, float _f, float _u) { (void)_r; say("polyoffset:%g:%g", _f, _u); }
void vkr_clear_color(vkr_renderer *_r, float _cr, float _g, float _b, float _a) { (void)_r; say("clearcolor:%g:%g:%g:%g", _cr, _g, _b, _a); }
void vkr_clear_depth(vkr_renderer *_r) { (void)_r; say("cleardepth"); }
unsigned vkr_texture_new(vkr_renderer *_r) { (void)_r; say("texnew"); return 41; }
void vkr_texture_delete(vkr_renderer *_r, unsigned _h) { (void)_r; say("texdel:%u", _h); }
void vkr_texture_init(vkr_renderer *_r, unsigned _h, int _fmt, unsigned _samples, unsigned _w, unsigned _hgt,
	unsigned _level, unsigned _levels, const void *_data, size_t _bytes)
{
	(void)_r;
	say("texinit:%u:%d:%u:%u:%u:%u:%u:%d:%u", _h, _fmt, _samples, _w, _hgt, _level, _levels, _data != NULL, (unsigned)_bytes);
}
static char lent[64];
void *vkr_texture_lend(vkr_renderer *_r, size_t _bytes) { (void)_r; say("lend:%u", (unsigned)_bytes); return lent; }
void vkr_texture_update(vkr_renderer *_r, unsigned _h, unsigned _level, unsigned _x, unsigned _y, unsigned _w,
	unsigned _hgt, const void *_data, size_t _bytes)
{
	(void)_r;
	say("texupdate:%u:%u:%u:%u:%u:%u:%d:%u", _h, _level, _x, _y, _w, _hgt, _data != NULL, (unsigned)_bytes);
}
void vkr_texture_params(vkr_renderer *_r, unsigned _h, int _mag, int _min, int _mip, int _ws, int _wt, float _lod, float _aniso)
{
	(void)_r;
	say("texparams:%u:%d:%d:%d:%d:%d:%g:%g", _h, _mag, _min, _mip, _ws, _wt, _lod, _aniso);
}
void vkr_bind(vkr_renderer *_r, unsigned _unit, unsigned _h) { (void)_r; say("bind:%u:%u", _unit, _h); }
void vkr_bind_image(vkr_renderer *_r, unsigned _unit, unsigned _h) { (void)_r; say("bindimage:%u:%u", _unit, _h); }
unsigned vkr_fbo_new(vkr_renderer *_r) { (void)_r; say("fbonew"); return 51; }
void vkr_fbo_delete(vkr_renderer *_r, unsigned _h) { (void)_r; say("fbodel:%u", _h); }
void vkr_fbo_bind(vkr_renderer *_r, int _read, int _draw, unsigned _h) { (void)_r; say("fbobind:%d:%d:%u", _read, _draw, _h); }
void vkr_fbo_color(vkr_renderer *_r, unsigned _fbo, unsigned _tex) { (void)_r; say("fbocolor:%u:%u", _fbo, _tex); }
void vkr_fbo_depth(vkr_renderer *_r, unsigned _fbo, unsigned _tex) { (void)_r; say("fbodepth:%u:%u", _fbo, _tex); }
static int blitResult = 1;
int vkr_blit(vkr_renderer *_r, unsigned _read, unsigned _draw, int _sx0, int _sy0, int _sx1, int _sy1,
	int _dx0, int _dy0, int _dx1, int _dy1, int _color, int _depth, int _linear)
{
	(void)_r;
	say("blit:%u:%u:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d", _read, _draw, _sx0, _sy0, _sx1, _sy1, _dx0, _dy0, _dx1, _dy1,
		_color, _depth, _linear);
	return blitResult;
}

/* programs: a handle each, numbered */
static int programs[64];
static int programCount;
static int combinerFails;
static vkr_combiner_desc lastDesc;
static int lastRect;
vkr_program *vkr_combiner_new(vkr_renderer *_r, const vkr_combiner_desc *_d, int _rect)
{
	(void)_r;
	lastDesc = *_d;
	lastRect = _rect;
	say("combinernew");
	if (combinerFails)
		return NULL;
	programs[programCount & 63] = programCount;
	return (vkr_program *)&programs[programCount++ & 63];
}
vkr_program *vkr_special_new(vkr_renderer *_r, int _vs, int _fs)
{
	(void)_r;
	say("specialnew:%d:%d", _vs, _fs);
	programs[programCount & 63] = programCount;
	return (vkr_program *)&programs[programCount++ & 63];
}
static int pnum(const vkr_program *_p) { return _p != NULL ? *(const int *)_p : -1; }
void vkr_program_delete(vkr_renderer *_r, vkr_program *_p) { (void)_r; say("progdel:%d", pnum(_p)); }
void vkr_use(vkr_renderer *_r, vkr_program *_p) { (void)_r; say("use:%d", pnum(_p)); }
static const CombinerValues *valuesSent;
void vkr_values(vkr_renderer *_r, vkr_program *_p, const CombinerValues *_v) { (void)_r; valuesSent = _v; say("values:%d", pnum(_p)); }
void vkr_special_params(vkr_renderer *_r, vkr_program *_p, float _a, float _b, float _c, float _d)
{
	(void)_r;
	say("sparams:%d:%g:%g:%g:%g", pnum(_p), _a, _b, _c, _d);
}

static vkr_tri_vertex triRing[16];
static vkr_rect_vertex rectRing[8];
static uint16_t indexRing[16];
vkr_tri_vertex *vkr_tri_reserve(vkr_renderer *_r, unsigned _n) { (void)_r; say("trireserve:%u", _n); memset(triRing, 0xee, sizeof(triRing)); return triRing; }
vkr_rect_vertex *vkr_rect_reserve(vkr_renderer *_r, unsigned _n) { (void)_r; say("rectreserve:%u", _n); return rectRing; }
uint16_t *vkr_index_reserve(vkr_renderer *_r, unsigned _n) { (void)_r; say("indexreserve:%u", _n); return indexRing; }
void vkr_draw_tris(vkr_renderer *_r, int _t, unsigned _v, unsigned _i) { (void)_r; say("drawtris:%d:%u:%u", _t, _v, _i); }
void vkr_draw_rects(vkr_renderer *_r, int _t, unsigned _v) { (void)_r; say("drawrects:%d:%u", _t, _v); }
void vkr_draw_line(vkr_renderer *_r, unsigned _v, float _w) { (void)_r; say("drawline:%u:%g", _v, _w); }

/* ---- the rest ---- */

static int ifaceObj;
const struct retro_hw_render_interface_vulkan *gliden64_vulkan_interface(void) { return (const struct retro_hw_render_interface_vulkan *)&ifaceObj; }
static int interlock;
int gliden64_vulkan_has_interlock(void) { return interlock; }
static int upscalerActive, upscalerOffered;
bool gliden64_upscaler_active(void) { return upscalerActive; }
bool gliden64_upscaler_offered(void) { return upscalerOffered; }
void gliden64_upscaler_set_interface(const struct retro_hw_render_upscaler_interface *_i) { say("upscaler-iface:%d", _i != NULL); }
void gliden64_upscaler_color_image(uint32_t _a, uint32_t _s, int _l) { say("colorimage:%u:%u:%d", _a, _s, _l); }
static gliden64_upscaler_view lastView;
static uint64_t lastSignature;
static unsigned lastFlags;
static const vkr_tri_vertex *upTris;
static const uint16_t *upIndices;
static unsigned upCounts[2];
static int upStrip;
void gliden64_upscaler_triangles(const vkr_tri_vertex *_v, unsigned _n, const uint16_t *_i, unsigned _ni, bool _strip,
	const gliden64_upscaler_view *_view, uint64_t _sig, unsigned _flags, float *_jitter)
{
	upTris = _v; upCounts[0] = _n; upIndices = _i; upCounts[1] = _ni; upStrip = _strip;
	lastView = *_view;
	lastSignature = _sig;
	lastFlags = _flags;
	_jitter[0] = 0.25f;
	_jitter[1] = -0.5f;
	say("uptris");
}
static const vkr_rect_vertex *upRect;
void gliden64_upscaler_rect(const vkr_rect_vertex *_v, unsigned _n, const gliden64_upscaler_view *_view)
{
	upRect = _v;
	lastView = *_view;
	say("uprect:%u", _n);
}
static float shownMap[4];
static unsigned shownArgs[6];
static float shownScale;
void gliden64_upscaler_shown(uint32_t _a, uint32_t _s, int _l, unsigned _w, unsigned _h, float _scale, const float *_map)
{
	shownArgs[0] = _a; shownArgs[1] = _s; shownArgs[2] = (unsigned)_l; shownArgs[3] = _w; shownArgs[4] = _h;
	shownScale = _scale;
	memcpy(shownMap, _map, sizeof(shownMap));
	say("shown");
}
static int upscaledFrame;
bool gliden64_upscaler_frame(vkr_upscaler_frame *_f) { memset(_f, 0x5a, sizeof(*_f)); return upscaledFrame; }
uint64_t rdp_upscaler_hash_init(void) { return 7; }
uint64_t rdp_upscaler_hash(uint64_t _h, uint32_t _w) { return _h * 31 + _w; }

static int prbObj, cbrObj;
static size_t prbBytes;
static const struct gli_tex *cbrTex;
gli_prb *gli_vk_prb_new(vkr_renderer *_r, size_t _bytes) { (void)_r; prbBytes = _bytes; return (gli_prb *)&prbObj; }
gli_cbr *gli_vk_cbr_new(vkr_renderer *_r, struct gli_tex *_t) { (void)_r; cbrTex = _t; return (gli_cbr *)&cbrObj; }
void gli_fb_formats_vk(gli_fb_formats *_f) { _f->colorFormatBytes = 77; say("formats"); }
static u32 optionsBits = 5;
u32 gli_combiner_options_bits(void) { return optionsBits; }
static int hwLighting;
bool gli_isHWLightingAllowed(void) { return hwLighting; }
void gli_palette_update(void) { say("palette"); }
void gli_api_user_cache_path(char *_p, size_t _size) { strncpy(_p, "/cache", _size); }

static unsigned groupsAsked;
static CombinerNeeds needsAsked;
unsigned combinerValueGroups(const CombinerNeeds *_n) { needsAsked = *_n; return groupsAsked; }
static unsigned groupsUpdated;
void updateCombinerValues(CombinerValues *_v, unsigned _g) { groupsUpdated = _g; _v->fogColor[0] = 0.5f; say("update-values:%u", _g); }

u32 gli_combiner_key_cycle_type(u64 _key) { return (u32)(_key >> 60) & 3; }
u32 gli_combiner_key_bilerp(u64 _key) { return (u32)(_key >> 56) & 3; }
bool gli_combiner_key_is_rect(u64 _key) { return ((_key >> 58) & 1) != 0; }
#define KEY(_cycle, _rect, _bilerp, _mux) (((u64)(_cycle) << 60) | ((u64)(_rect) << 58) | ((u64)(_bilerp) << 56) | (_mux))

static FrameBuffer drawnBuffer;
static FrameBuffer *drawn, *found;
FrameBuffer *gli_fb_drawn(void) { return drawn; }
FrameBuffer *gli_fb_find(u32 _address) { say("find:%u", _address); return found; }
void gli_fb_coords_scales(const FrameBuffer *_b, f32 *_x, f32 *_y) { (void)_b; *_x = 1.0f / 320.0f; *_y = 1.0f / 240.0f; }

/* ---- the program the context is asked about ---- */

static const gli_context_ops *ops;
static void *ctx;

static void start(void)
{
	clear_log();
	ops = gli_vk_context_ops();
	ctx = gli_vk_context_new();
	ops->init(ctx);
}

static void stop(void)
{
	ops->destroy(ctx);
	ops->free_impl(ctx);
}

static void test_create(void)
{
	memset(&config, 0, sizeof(config));
	config.generalEmulation.enableLegacyBlending = 1;
	config.generalEmulation.enableNoise = 0;
	config.generalEmulation.enableFragmentDepthWrite = 1;
	config.frameBufferEmulation.N64DepthCompare = 1;
	config.generalEmulation.enableLOD = 1;
	config.texture.bilinearMode = BILINEAR_3POINT;
	config.texture.enableHalosRemoval = 1;
	config.video.multisampling = 4;
	interlock = 1;
	g_window.screenWidth = 800;
	g_window.screenHeight = 600;
	upscalerOffered = 0;
	start();
	check(createdIface == (const void *)&ifaceObj, "made on the frontend's Vulkan");
	check(createdCfg.legacy_blending == 1 && createdCfg.noise == 0 && createdCfg.fragment_depth_write == 1
		&& createdCfg.n64_depth_compare == 1 && createdCfg.enable_lod == 1, "the shader config from the options");
	check(createdCfg.filter_mode == (int)(BILINEAR_3POINT + 2) && createdCfg.three_point == 1 && createdCfg.msaa == 4,
		"its filter and multisampling");
	check(createdCfg.noise_width == NOISE_TEX_WIDTH && createdCfg.noise_height == NOISE_TEX_HEIGHT && createdCfg.interlock == 1,
		"the noise texture's size; interlock with the N64 depth compare");
	check(strcmp(createdCache, "/cache/GLideN64.vulkan.keys.journal.pipelines") == 0, "the pipeline cache beside the journal");
	check(has("create window:800:600") && !has("upscaler-init"), "the window sized; no upscaler not offered");
	check(ops->getClampMode(ctx) == 0 && ops->getTextureUnpackAlignment(ctx) == 1, "clipping on, unpacked a byte at a time");
	stop();
	interlock = 0;
	config.texture.bilinearMode = BILINEAR_3POINT == 0 ? 1 : 0;
	config.texture.enableHalosRemoval = 0;
	config.frameBufferEmulation.N64DepthCompare = 0;
	start();
	check(createdCfg.interlock == 0 && createdCfg.three_point == 0 && createdCfg.filter_mode == (int)config.texture.bilinearMode
		&& createdCfg.n64_depth_compare == 0,
		"no interlock without the device's");
	stop();
	config.frameBufferEmulation.N64DepthCompare = 1;
	start();
	check(createdCfg.interlock == 0 && createdCfg.n64_depth_compare == 1, "no interlock without the device's, with the compare");
	stop();
	config.frameBufferEmulation.N64DepthCompare = 0;
	interlock = 1;
	start();
	check(createdCfg.interlock == 0, "no interlock without the N64 depth compare");
	stop();
	interlock = 0;

	upscalerOffered = 1;
	upscalerInitOk = 1;
	start();
	check(has("upscaler-init") && !has("upscaler-iface"), "the upscaler started");
	stop();
	upscalerInitOk = 0;
	start();
	check(has("upscaler-init upscaler-iface:0"), "a device that cannot: the upscaler dropped");
	stop();
	upscalerOffered = 0;
	upscalerInitOk = 1;

	createFails = 1;
	upscalerOffered = 1;
	start();
	check(!has("window") && !has("upscaler"), "no device: nothing more");
	createFails = 0;
	upscalerOffered = 0;
	clear_log();
	ops->destroy(ctx);
	check(has("destroy:0"), "stopped with none");
	ops->free_impl(ctx);

	start();
	clear_log();
	ops->destroy(ctx);
	check(has("destroy:1"), "stopped");
	clear_log();
	gliden64_vulkan_present();
	check(!has("present"), "nothing presented after");
	ops->free_impl(ctx);
}

static void test_state(void)
{
	start();
	clear_log();
	ops->setClampMode(ctx, 0);
	ops->setClampMode(ctx, 1);
	ops->setClampMode(ctx, 2);
	check(has("clamp:0:0 clamp:1:1 clamp:1:0"), "the clamp modes");
	check(ops->getClampMode(ctx) == 2, "the clamp mode kept");
	clear_log();
	ops->enable(ctx, GLI_EN_BLEND, 1);
	ops->enable(ctx, GLI_EN_CULL_FACE, 0);
	ops->enable(ctx, GLI_EN_DEPTH_TEST, 1);
	ops->enable(ctx, GLI_EN_POLYGON_OFFSET_FILL, 1);
	ops->enable(ctx, GLI_EN_SCISSOR_TEST, 0);
	ops->enable(ctx, GLI_EN_DITHER, 1);
	{
		char want[160];
		sprintf(want, "enable:%d:1 enable:%d:0 enable:%d:1 enable:%d:1 enable:%d:0 ", VKR_CAP_BLEND, VKR_CAP_CULL,
			VKR_CAP_DEPTH_TEST, VKR_CAP_POLY_OFFSET, VKR_CAP_SCISSOR);
		check(strcmp(log_, want) == 0, "capabilities, an unknown one left");
	}
	check(ops->isEnabled(ctx, GLI_EN_BLEND) == (u32)VKR_CAP_BLEND + 100 && ops->isEnabled(ctx, GLI_EN_CULL_FACE) == (u32)VKR_CAP_CULL + 100
		&& ops->isEnabled(ctx, GLI_EN_DEPTH_TEST) == (u32)VKR_CAP_DEPTH_TEST + 100
		&& ops->isEnabled(ctx, GLI_EN_POLYGON_OFFSET_FILL) == (u32)VKR_CAP_POLY_OFFSET + 100
		&& ops->isEnabled(ctx, GLI_EN_SCISSOR_TEST) == (u32)VKR_CAP_SCISSOR + 100 && ops->isEnabled(ctx, GLI_EN_DITHER) == 0,
		"asked of the renderer");
	clear_log();
	ops->cullFace(ctx, GLI_CULLMODE_FRONT);
	ops->cullFace(ctx, GLI_CULLMODE_BACK);
	ops->cullFace(ctx, GLI_CULLMODE_FRONT_AND_BACK);
	ops->enableDepthWrite(ctx, 1);
	ops->enableDepthWrite(ctx, 0);
	ops->setDepthCompare(ctx, GLI_COMPARE_LESS);
	ops->setDepthCompare(ctx, GLI_COMPARE_ALWAYS);
	ops->setDepthCompare(ctx, GLI_COMPARE_LEQUAL);
	{
		char want[200];
		sprintf(want, "cull:%d cull:%d cull:%d depthwrite:1 depthwrite:0 depthcmp:%d depthcmp:%d depthcmp:%d ", VKR_CULL_FRONT,
			VKR_CULL_BACK, VKR_CULL_BOTH, VKR_CMP_LESS, VKR_CMP_ALWAYS, VKR_CMP_LEQUAL);
		check(strcmp(log_, want) == 0, "culling and depth");
	}
	clear_log();
	ops->setViewport(ctx, 1, 2, 3, 4);
	ops->setScissor(ctx, 5, 6, 7, 8);
	ops->setBlendColor(ctx, 0.25f, 0.5f, 0.75f, 1.0f);
	ops->clearColorBuffer(ctx, 0.5f, 0.25f, 0.125f, 1.0f);
	ops->clearDepthBuffer(ctx);
	ops->setPolygonOffset(ctx, -1.5f, -2.0f);
	check(strcmp(log_, "viewport:1:2:3:4 scissor:5:6:7:8 blendcolor:0.25:0.5:0.75:1 clearcolor:0.5:0.25:0.125:1 cleardepth polyoffset:-1.5:-2 ") == 0,
		"viewport, scissor, colours, offset");
	{
		static const u32 gl[12] = { GLI_BLEND_ZERO, GLI_BLEND_ONE, GLI_BLEND_SRC_ALPHA, GLI_BLEND_DST_ALPHA, GLI_BLEND_ONE_MINUS_SRC_ALPHA,
			GLI_BLEND_CONSTANT_ALPHA, GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA, GLI_BLEND_SRC1_COLOR, GLI_BLEND_ONE_MINUS_SRC1_COLOR,
			GLI_BLEND_SRC1_ALPHA, GLI_BLEND_ONE_MINUS_SRC1_ALPHA, 0x9999 };
		static const int vk[12] = { VKR_BLEND_ZERO, VKR_BLEND_ONE, VKR_BLEND_SRC_ALPHA, VKR_BLEND_DST_ALPHA, VKR_BLEND_INV_SRC_ALPHA,
			VKR_BLEND_CONST_ALPHA, VKR_BLEND_INV_CONST_ALPHA, VKR_BLEND_SRC1_COLOR, VKR_BLEND_INV_SRC1_COLOR,
			VKR_BLEND_SRC1_ALPHA, VKR_BLEND_INV_SRC1_ALPHA, VKR_BLEND_ONE };
		int i;
		for (i = 0; i < 12; i++) {
			char want[64];
			clear_log();
			ops->setBlending(ctx, gl[i], GLI_BLEND_ZERO);
			sprintf(want, "blend:%d:%d:%d:%d ", vk[i], VKR_BLEND_ZERO, vk[i], VKR_BLEND_ZERO);
			check(strcmp(log_, want) == 0, "each blend factor, colour and alpha alike");
			clear_log();
			ops->setBlendingSeparate(ctx, GLI_BLEND_ONE, gl[i], GLI_BLEND_SRC_ALPHA, gl[11 - i]);
			sprintf(want, "blend:%d:%d:%d:%d ", VKR_BLEND_ONE, vk[i], VKR_BLEND_SRC_ALPHA, vk[11 - i]);
			check(strcmp(log_, want) == 0, "each blend factor, separate");
		}
	}
	stop();
}

static void test_textures(void)
{
	gli_init_texture_params ip = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_update_texture_params up;
	gli_tex_parameters tp = GLI_TEX_PARAMETERS_DEFAULT;
	gli_bind_texture_params bp;
	gli_bind_image_texture_params bip;
	gli_init_renderbuffer_params rp;
	char data[4];
	start();
	clear_log();
	check(ops->createTexture(ctx, GLI_TT_TEXTURE_2D) == 41 && has("texnew"), "a texture made");
	ops->deleteTexture(ctx, 9);
	check(has("texdel:9"), "deleted");
	{
		static const u32 gl[11] = { GLI_ICF_RGBA8, GLI_ICF_RGB8, GLI_ICF_RGBA4, GLI_ICF_RGB5_A1, GLI_ICF_R8, GLI_ICF_R16F,
			GLI_ICF_R32F, 0x8236, GLI_ICF_R16UI, GLI_ICF_DEPTH, 0x1234 };
		static const int vk[11] = { VKR_FMT_RGBA8, VKR_FMT_RGB8, VKR_FMT_RGBA4, VKR_FMT_RGB5A1, VKR_FMT_R8, VKR_FMT_R16F,
			VKR_FMT_R32F, VKR_FMT_R32UI, VKR_FMT_R16UI, VKR_FMT_DEPTH, VKR_FMT_NONE };
		static const unsigned bytes[11] = { 4, 4, 2, 2, 1, 2, 4, 4, 2, 4, 4 };
		int i;
		for (i = 0; i < 11; i++) {
			char want[96];
			clear_log();
			ip.handle = 12;
			ip.internalFormat = gl[i];
			ip.width = 3;
			ip.height = 5;
			ip.mipMapLevel = 1;
			ip.mipMapLevels = 2;
			ip.msaaLevel = 0;
			ip.data = data;
			ip.textureUnitIndex = GLI_PARAM_INVALID;
			ops->init2DTexture(ctx, &ip);
			sprintf(want, "texinit:12:%d:1:3:5:1:2:1:%u ", vk[i], 15 * bytes[i]);
			check(strcmp(log_, want) == 0, "each format with its bytes");
			clear_log();
			memset(&up, 0, sizeof(up));
			up.handle = 13; up.internalFormat = gl[i]; up.mipMapLevel = 2; up.x = 1; up.y = 2; up.width = 7; up.height = 3; up.data = data;
			ops->update2DTexture(ctx, &up);
			sprintf(want, "texupdate:13:2:1:2:7:3:1:%u ", 21 * bytes[i]);
			check(strcmp(log_, want) == 0, "each format's update with its bytes");
		}
	}
	clear_log();
	ip.data = NULL;
	ip.msaaLevel = 4;
	ip.internalFormat = GLI_ICF_RGBA8;
	ip.textureUnitIndex = GLI_TU_TEX1;
	ops->init2DTexture(ctx, &ip);
	{
		char want[96];
		sprintf(want, "texinit:12:%d:4:3:5:1:2:0:0 bind:%d:12 ", VKR_FMT_RGBA8, VKR_BIND_TEX1);
		check(strcmp(log_, want) == 0, "no data: no bytes; multisampled; bound to its unit");
	}
	clear_log();
	check(ops->lendTextureData(ctx, 99) == lent && has("lend:99"), "lent from the ring");
	{
		static const u32 units[9] = { GLI_TU_TEX0, GLI_TU_TEX1, GLI_TU_NoiseTex, GLI_TU_DepthTex, GLI_TU_ZLUTTex, GLI_TU_PaletteTex,
			GLI_TU_MSTEX0, GLI_TU_MSTEX1, 8 };
		static const int binds[8] = { VKR_BIND_TEX0, VKR_BIND_TEX1, VKR_BIND_NOISE, VKR_BIND_DEPTH, VKR_BIND_ZLUT, VKR_BIND_TLUT,
			VKR_BIND_MS0, VKR_BIND_MS1 };
		int i;
		for (i = 0; i < 9; i++) {
			char want[32];
			clear_log();
			bp.textureUnitIndex = units[i];
			bp.texture = 30 + i;
			bp.target = GLI_TT_TEXTURE_2D;
			ops->bindTexture(ctx, &bp);
			if (i < 8) {
				sprintf(want, "bind:%d:%u ", binds[i], 30 + i);
				check(strcmp(log_, want) == 0, "each unit");
			} else
				check(log_[0] == '\0', "no unit past them");
		}
	}
	clear_log();
	tp.handle = 14;
	ops->setTextureParameters(ctx, &tp);
	{
		char want[96];
		sprintf(want, "texparams:14:%d:%d:%d:%d:%d:-1:-1 bind:%d:14 ", VKR_KEEP, VKR_KEEP, VKR_KEEP, VKR_KEEP, VKR_KEEP, VKR_BIND_TEX0);
		check(strcmp(log_, want) == 0, "nothing set: all kept; its unit, 0");
		clear_log();
		tp.magFilter = GLI_TP_FILTER_LINEAR;
		tp.minFilter = GLI_TP_FILTER_NEAREST;
		tp.wrapS = GLI_TP_WRAP_REPEAT;
		tp.wrapT = GLI_TP_WRAP_MIRRORED_REPEAT;
		tp.maxMipmapLevel = 3;
		tp.maxAnisotropy = 8.0f;
		tp.textureUnitIndex = GLI_TU_TEX0;
		ops->setTextureParameters(ctx, &tp);
		sprintf(want, "texparams:14:1:0:%d:%d:%d:3:8 bind:%d:14 ", VKR_KEEP, VKR_WRAP_REPEAT, VKR_WRAP_MIRROR, VKR_BIND_TEX0);
		check(strcmp(log_, want) == 0, "filters, wraps, levels, anisotropy, its unit");
		clear_log();
		tp.magFilter = GLI_TP_FILTER_NEAREST;
		tp.minFilter = GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST;
		tp.wrapS = GLI_TP_WRAP_CLAMP_TO_EDGE;
		tp.wrapT = GLI_TP_WRAP_CLAMP_TO_EDGE;
		tp.textureUnitIndex = GLI_PARAM_INVALID;
		ops->setTextureParameters(ctx, &tp);
		sprintf(want, "texparams:14:0:1:%d:%d:%d:3:8 ", VKR_KEEP, VKR_WRAP_CLAMP, VKR_WRAP_CLAMP);
		check(strcmp(log_, want) == 0, "nearest, linear mipmapped, clamped");
		clear_log();
		tp.minFilter = GLI_TP_FILTER_LINEAR;
		tp.maxAnisotropy = 0.0f;
		ops->setTextureParameters(ctx, &tp);
		sprintf(want, "texparams:14:0:1:%d:%d:%d:3:-1 ", VKR_KEEP, VKR_WRAP_CLAMP, VKR_WRAP_CLAMP);
		check(strcmp(log_, want) == 0, "linear; no anisotropy");
		clear_log();
		tp.minFilter = GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST;
		ops->setTextureParameters(ctx, &tp);
		check(strstr(log_, "texparams:14:0:0:") != NULL, "nearest mipmapped");
	}
	ops->setTextureUnpackAlignment(ctx, 4);
	check(ops->getTextureUnpackAlignment(ctx) == 4, "the unpack alignment kept");
	check(ops->getMaxTextureSize(ctx) == 8192 && ops->getMaxAnisotropy(ctx) == 16.0f && ops->getMaxLineWidth(ctx) == 4.0f
		&& ops->getMaxMSAALevel(ctx) == 8, "the device's limits");
	clear_log();
	bip.imageUnit = GLI_IU_DepthZ;
	bip.texture = 61;
	ops->bindImageTexture(ctx, &bip);
	bip.imageUnit = GLI_IU_DepthDeltaZ;
	bip.texture = 62;
	ops->bindImageTexture(ctx, &bip);
	{
		char want[64];
		sprintf(want, "bindimage:%d:61 bindimage:%d:62 ", VKR_BIND_DEPTH_Z, VKR_BIND_DEPTH_DZ);
		check(strcmp(log_, want) == 0, "the depth images");
	}
	check(ops->convertInternalTextureFormat(ctx, 0x8058) == 0x8058 && ops->convertInternalTextureFormat(ctx, 0x8229) == 0x8229,
		"formats as they are");
	clear_log();
	ops->textureBarrier(ctx);
	ops->setDrawBuffers(ctx, 3);
	check(log_[0] == '\0', "no barrier or draw buffers");
	{
		gli_fb_formats f;
		memset(&f, 0, sizeof(f));
		ops->getFramebufferTextureFormats(ctx, &f);
		check(f.colorFormatBytes == 77, "the Vulkan formats");
	}
	clear_log();
	check(ops->createRenderbuffer(ctx) == 41, "a render buffer a texture");
	ops->deleteRenderbuffer(ctx, 8);
	rp.handle = 15; rp.target = GLI_TT_RENDERBUFFER; rp.format = GLI_ICF_DEPTH; rp.width = 64; rp.height = 32;
	ops->initRenderbuffer(ctx, &rp);
	{
		char want[96];
		sprintf(want, "texnew texdel:8 texinit:15:%d:1:64:32:0:1:0:0 ", VKR_FMT_DEPTH);
		check(strcmp(log_, want) == 0, "made, deleted, given its size");
	}
	check(ops->createPixelReadBuffer(ctx, 123) == (gli_prb *)&prbObj && prbBytes == 123, "a pixel read buffer");
	{
		struct gli_tex t;
		check(ops->createColorBufferReader(ctx, &t) == (void *)&cbrObj && cbrTex == &t, "a colour buffer reader of the texture");
	}
	stop();
}

static void test_framebuffers(void)
{
	gli_fb_render_target rt = GLI_FB_RENDER_TARGET_DEFAULT;
	gli_blit_params bl = GLI_BLIT_PARAMS_DEFAULT;
	start();
	g_window.screenWidth = 640;
	g_window.screenHeight = 480;
	clear_log();
	check(ops->createFramebuffer(ctx) == 51, "a frame buffer made");
	ops->deleteFramebuffer(ctx, 52);
	ops->bindFramebuffer(ctx, GLI_BT_READ_FRAMEBUFFER, 3);
	ops->bindFramebuffer(ctx, GLI_BT_DRAW_FRAMEBUFFER, 4);
	ops->bindFramebuffer(ctx, GLI_BT_FRAMEBUFFER, 5);
	check(strcmp(log_, "fbonew fbodel:52 fbobind:1:0:3 fbobind:0:1:4 fbobind:1:1:5 ") == 0, "bound by target");
	clear_log();
	ops->bindFramebuffer(ctx, GLI_BT_DRAW_FRAMEBUFFER, 0);
	check(strcmp(log_, "window:640:480 fbobind:0:1:0 ") == 0, "the window sized before it is bound");
	clear_log();
	rt.bufferHandle = 7; rt.bufferTarget = GLI_BT_DRAW_FRAMEBUFFER; rt.attachment = GLI_BA_COLOR_ATTACHMENT0;
	rt.textureTarget = GLI_TT_TEXTURE_2D; rt.textureHandle = 8;
	ops->addFrameBufferRenderTarget(ctx, &rt);
	rt.attachment = GLI_BA_DEPTH_ATTACHMENT; rt.bufferTarget = GLI_BT_READ_FRAMEBUFFER; rt.textureHandle = 9;
	ops->addFrameBufferRenderTarget(ctx, &rt);
	rt.attachment = GLI_BA_COLOR_ATTACHMENT1;
	ops->addFrameBufferRenderTarget(ctx, &rt);
	check(strcmp(log_, "fbobind:0:1:7 fbocolor:7:8 fbobind:1:0:7 fbodepth:7:9 fbobind:1:0:7 ") == 0,
		"attached after binding, colour or depth");
	clear_log();
	bl.readBuffer = 1; bl.drawBuffer = 2; bl.srcX0 = 3; bl.srcY0 = 4; bl.srcX1 = 5; bl.srcY1 = 6;
	bl.dstX0 = 7; bl.dstY0 = 8; bl.dstX1 = 9; bl.dstY1 = 10; bl.mask = GLI_BM_COLOR_BUFFER; bl.filter = GLI_TP_FILTER_LINEAR;
	blitResult = 1;
	check(ops->blitFramebuffers(ctx, &bl), "blitted");
	bl.mask = GLI_BM_DEPTH_BUFFER; bl.filter = GLI_TP_FILTER_NEAREST;
	blitResult = 0;
	check(!ops->blitFramebuffers(ctx, &bl), "the blit's result");
	bl.mask = GLI_BM_DEPTH_BUFFER | GLI_BM_COLOR_BUFFER;
	ops->blitFramebuffers(ctx, &bl);
	blitResult = 1;
	check(strcmp(log_, "window:640:480 blit:1:2:3:4:5:6:7:8:9:10:1:0:1 window:640:480 blit:1:2:3:4:5:6:7:8:9:10:0:1:0 "
		"window:640:480 blit:1:2:3:4:5:6:7:8:9:10:1:1:0 ") == 0, "the window sized; colour, depth, linear");
	check(ops->isSupported(ctx, GLI_FEATURE_MULTISAMPLING) && ops->isSupported(ctx, GLI_FEATURE_BLIT_FRAMEBUFFER)
		&& ops->isSupported(ctx, GLI_FEATURE_DEPTH_FRAMEBUFFER_TEXTURES) && ops->isSupported(ctx, GLI_FEATURE_INTEGER_TEXTURES)
		&& ops->isSupported(ctx, GLI_FEATURE_CLIP_CONTROL), "what it has");
	check(!ops->isSupported(ctx, GLI_FEATURE_WEAK_BLIT_FRAMEBUFFER) && !ops->isSupported(ctx, GLI_FEATURE_SHADER_PROGRAM_BINARY)
		&& !ops->isSupported(ctx, GLI_FEATURE_FRAMEBUFFER_FETCH) && !ops->isSupported(ctx, GLI_FEATURE_TEXTURE_BARRIER)
		&& !ops->isSupported(ctx, GLI_FEATURE_DUAL_SOURCE_BLENDING), "what it does not");
	caps.storage_images = 1;
	check(ops->isSupported(ctx, GLI_FEATURE_IMAGE_TEXTURES), "image textures with storage images");
	caps.storage_images = 0;
	check(!ops->isSupported(ctx, GLI_FEATURE_IMAGE_TEXTURES), "not without");
	caps.storage_images = 1;
	check(!ops->isError(ctx) && !ops->isFramebufferError(ctx), "no errors");
	stop();
}

/* a stage of @n ops */
static void stage(CombinerStage *_s, int _n, int _op0, int _p1, int _p2, int _p3, int _op1, int _q1)
{
	memset(_s, 0, sizeof(*_s));
	_s->numOps = _n;
	_s->op[0].op = _op0; _s->op[0].param1 = _p1; _s->op[0].param2 = _p2; _s->op[0].param3 = _p3;
	_s->op[1].op = _op1; _s->op[1].param1 = _q1;
}

static void test_stages(void)
{
	CombinerStage st;
	int enc[4];
	gDPCombine c;
	stage(&st, 2, LOAD, G_GCI_TEXEL1, G_GCI_TEXEL1_ALPHA, G_GCI_TEXEL0, MUL, G_GCI_TEXEL1_ALPHA);
	gli_stage_correct_first(&st);
	check(st.op[0].param1 == G_GCI_TEXEL0 && st.op[0].param2 == G_GCI_TEXEL0_ALPHA && st.op[0].param3 == G_GCI_TEXEL0
		&& st.op[1].param1 == G_GCI_TEXEL0_ALPHA, "the first cycle reads texel 0");
	st.op[2].param1 = G_GCI_TEXEL1;
	gli_stage_correct_first(&st);
	check(st.op[2].param1 == G_GCI_TEXEL1, "only its ops");
	stage(&st, 1, INTER, G_GCI_SHADE, G_GCI_SHADE, G_GCI_TEXEL1, 0, 0);
	gli_stage_correct_first(&st);
	check(st.op[0].param3 == G_GCI_TEXEL0, "the third input too");
	stage(&st, 2, LOAD, G_GCI_TEXEL0, G_GCI_TEXEL1, G_GCI_TEXEL0_ALPHA, MUL, G_GCI_TEXEL1_ALPHA);
	st.op[0].param3 = G_GCI_TEXEL1_ALPHA;
	st.op[1].param2 = G_GCI_SHADE;
	gli_stage_correct_second(&st);
	check(st.op[0].param1 == G_GCI_TEXEL1 && st.op[0].param2 == G_GCI_TEXEL0 && st.op[0].param3 == G_GCI_TEXEL0_ALPHA
		&& st.op[1].param1 == G_GCI_TEXEL0_ALPHA && st.op[1].param2 == G_GCI_SHADE, "the second cycle's texels swapped");
	stage(&st, 1, LOAD, G_GCI_TEXEL0_ALPHA, 0, 0, 0, 0);
	gli_stage_correct_second(&st);
	check(st.op[0].param1 == G_GCI_TEXEL1_ALPHA, "texel 0's alpha for texel 1's");
	stage(&st, 1, LOAD, G_GCI_TEXEL1, 0, 0, 0, 0);
	st.op[1].param1 = G_GCI_TEXEL1;
	gli_stage_correct_second(&st);
	check(st.op[1].param1 == G_GCI_TEXEL1, "only its ops, second");

	stage(&st, 3, LOAD, G_GCI_SHADE, 0, 0, MUL, G_GCI_PRIMITIVE);
	st.op[2].op = ADD; st.op[2].param1 = G_GCI_ENVIRONMENT;
	gli_stage_encode(&st, enc);
	check(enc[0] == ((LOAD << 8) | G_GCI_SHADE) && enc[1] == ((MUL << 8) | G_GCI_PRIMITIVE) && enc[2] == ((ADD << 8) | G_GCI_ENVIRONMENT)
		&& enc[3] == -1, "ops encoded, -1 after");
	stage(&st, 6, LOAD, 1, 0, 0, MUL, 2);
	st.op[2].op = ADD; st.op[2].param1 = 3; st.op[3].op = SUB; st.op[3].param1 = 4; st.op[4].op = ADD; st.op[4].param1 = 5;
	gli_stage_encode(&st, enc);
	check(enc[3] == ((SUB << 8) | 4), "four at most");
	stage(&st, 2, LOAD, G_GCI_SHADE, 0, 0, INTER, G_GCI_TEXEL0);
	st.op[1].param2 = G_GCI_TEXEL1; st.op[1].param3 = G_GCI_LOD_FRACTION;
	gli_stage_encode(&st, enc);
	check(enc[0] == ((INTER << 8) | G_GCI_TEXEL0) && enc[1] == G_GCI_TEXEL1 && enc[2] == G_GCI_LOD_FRACTION && enc[3] == -1,
		"INTER keeps its inputs, alone");
	stage(&st, 0, 0, 0, 0, 0, 0, 0);
	gli_stage_encode(&st, enc);
	check(enc[0] == -1 && enc[1] == -1 && enc[2] == -1 && enc[3] == -1, "none");

	{
		u32 in;
		stage(&st, 2, LOAD, G_GCI_SHADE, G_GCI_NOISE, G_GCI_NOISE, MUL, G_GCI_TEXEL0);
		in = gli_inputs_add_stage(0x100u, &st);
		check(in == (0x100u | (1u << G_GCI_SHADE) | (1u << G_GCI_TEXEL0)), "an op's first input");
		stage(&st, 1, INTER, G_GCI_TEXEL1, G_GCI_LOD_FRACTION, G_GCI_SHADE_ALPHA, 0, 0);
		in = gli_inputs_add_stage(0, &st);
		check(in == ((1u << G_GCI_TEXEL1) | (1u << G_GCI_LOD_FRACTION) | (1u << G_GCI_SHADE_ALPHA)), "INTER's three");
		check(gli_inputs_tile(1u << G_GCI_TEXEL0, 0) && gli_inputs_tile(1u << G_GCI_TEXEL0_ALPHA, 0) && !gli_inputs_tile(1u << G_GCI_TEXEL1, 0)
			&& gli_inputs_tile(1u << G_GCI_TEXEL1, 1) && gli_inputs_tile(1u << G_GCI_TEXEL1_ALPHA, 1) && !gli_inputs_tile(1u << G_GCI_TEXEL0, 1),
			"each tile's");
		check(gli_inputs_texture(1u << G_GCI_TEXEL0) && gli_inputs_texture(1u << G_GCI_TEXEL0_ALPHA) && gli_inputs_texture(1u << G_GCI_TEXEL1)
			&& gli_inputs_texture(1u << G_GCI_TEXEL1_ALPHA) && gli_inputs_texture(1u << G_GCI_LOD_FRACTION) && !gli_inputs_texture(1u << G_GCI_SHADE),
			"a texture's, the LOD's");
		check(gli_inputs_lod(1u << G_GCI_LOD_FRACTION) && !gli_inputs_lod(1u << G_GCI_TEXEL0), "the LOD");
		check(gli_inputs_noise(1u << G_GCI_NOISE) && !gli_inputs_noise(1u << G_GCI_SHADE), "noise");
		check(gli_inputs_shade(1u << G_GCI_SHADE) && gli_inputs_shade(1u << G_GCI_SHADE_ALPHA) && !gli_inputs_shade(1u << G_GCI_TEXEL0),
			"shade");
		check(gli_inputs_shade_color(1u << G_GCI_SHADE) && !gli_inputs_shade_color(1u << G_GCI_SHADE_ALPHA), "shade colour");
		check(gli_inputs_hw_lighting(1u << G_GCI_HW_LIGHT) && !gli_inputs_hw_lighting(1u << G_GCI_SHADE), "hardware lighting");
	}

	memset(&c, 0, sizeof(c));
	c.mRGB1 = G_CCMUX_COMBINED; c.mA1 = G_ACMUX_COMBINED;
	check(gli_combined_color_c(G_CYC_2CYCLE, &c) && !gli_combined_color_c(G_CYC_1CYCLE, &c), "colour C, two cycles only");
	check(gli_combined_alpha_c(G_CYC_2CYCLE, &c) && !gli_combined_alpha_c(G_CYC_1CYCLE, &c), "alpha C, two cycles only");
	c.mRGB1 = 1; c.mA1 = 1;
	check(!gli_combined_color_c(G_CYC_2CYCLE, &c) && !gli_combined_alpha_c(G_CYC_2CYCLE, &c), "not another C");
	c.aRGB1 = G_CCMUX_COMBINED; c.saRGB1 = 1; c.sbRGB1 = 1;
	c.aA1 = G_ACMUX_COMBINED; c.saA1 = 1; c.sbA1 = 1;
	check(gli_combined_color_abd(G_CYC_2CYCLE, &c) && !gli_combined_color_abd(G_CYC_1CYCLE, &c), "colour D");
	check(gli_combined_alpha_abd(G_CYC_2CYCLE, &c) && !gli_combined_alpha_abd(G_CYC_1CYCLE, &c), "alpha D");
	c.aRGB1 = 1; c.aA1 = 1;
	check(!gli_combined_color_abd(G_CYC_2CYCLE, &c) && !gli_combined_alpha_abd(G_CYC_2CYCLE, &c), "none of A, B, D");
	c.saRGB1 = G_CCMUX_COMBINED; c.saA1 = G_ACMUX_COMBINED;
	check(gli_combined_color_abd(G_CYC_2CYCLE, &c) && gli_combined_alpha_abd(G_CYC_2CYCLE, &c), "A, with C");
	c.mRGB1 = G_CCMUX_0; c.mA1 = G_ACMUX_0;
	check(!gli_combined_color_abd(G_CYC_2CYCLE, &c) && !gli_combined_alpha_abd(G_CYC_2CYCLE, &c), "not with C zero");
	c.saRGB1 = 1; c.saA1 = 1; c.sbRGB1 = G_CCMUX_COMBINED; c.sbA1 = G_ACMUX_COMBINED; c.mRGB1 = 1; c.mA1 = 1;
	check(gli_combined_color_abd(G_CYC_2CYCLE, &c) && gli_combined_alpha_abd(G_CYC_2CYCLE, &c), "B");
	c.mRGB1 = G_CCMUX_0;
	check(!gli_combined_color_abd(G_CYC_2CYCLE, &c) && gli_combined_alpha_abd(G_CYC_2CYCLE, &c), "colour and alpha apart");
	c.mRGB1 = 1; c.mA1 = G_ACMUX_0;
	check(gli_combined_color_abd(G_CYC_2CYCLE, &c) && !gli_combined_alpha_abd(G_CYC_2CYCLE, &c), "alpha and colour apart");
}

static void test_combiners(void)
{
	Combiner color, alpha;
	gli_cprog *p, *q;
	int i;
	start();
	hwLighting = 0;
	/* one cycle: texel 1 read as texel 0, no second stage */
	memset(&color, 0, sizeof(color));
	memset(&alpha, 0, sizeof(alpha));
	color.numStages = 1;
	alpha.numStages = 2;
	stage(&color.stage[0], 2, LOAD, G_GCI_TEXEL1, 0, 0, MUL, G_GCI_SHADE);
	stage(&alpha.stage[0], 1, LOAD, G_GCI_TEXEL1_ALPHA, 0, 0, 0, 0);
	stage(&alpha.stage[1], 1, LOAD, G_GCI_NOISE, 0, 0, 0, 0);
	stage(&color.stage[1], 1, LOAD, G_GCI_NOISE, 0, 0, 0, 0);
	groupsAsked = 0x33;
	clear_log();
	p = ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_1CYCLE, 0, 2, 0x123));
	check(p != NULL && p->key == KEY(G_CYC_1CYCLE, 0, 2, 0x123) && has("combinernew") && lastRect == 0, "built, its key kept");
	check(lastDesc.ops[0] == ((LOAD << 8) | G_GCI_TEXEL0_ALPHA) && lastDesc.ops[1] == -1
		&& lastDesc.ops[4] == ((LOAD << 8) | G_GCI_TEXEL0) && lastDesc.ops[5] == ((MUL << 8) | G_GCI_SHADE) && lastDesc.ops[6] == -1,
		"alpha then colour, the first cycle's texels");
	for (i = 8; i < 16; i++)
		check(lastDesc.ops[i] == -1, "no second stage");
	check(lastDesc.flags == (VKR_CF_TEXTURED | VKR_CF_TILE0 | VKR_CF_BILERP0 | (G_CYC_1CYCLE << VKR_CF_CYCLE_SHIFT)),
		"its flags: textured, tile 0, bilerp 0, its cycle");
	check(p->ops->usesTexture(p) && p->ops->usesTile(p, 0) && !p->ops->usesTile(p, 1) && p->ops->usesShade(p) && !p->ops->usesLOD(p)
		&& !p->ops->usesHwLighting(p), "what it reads");
	check(needsAsked.usesTexture == 1 && needsAsked.usesLOD == 0 && needsAsked.usesNoise == 0 && needsAsked.usesTile0 == 1
		&& needsAsked.usesTile1 == 0 && needsAsked.usesHwLighting == 0 && needsAsked.depthScale == 1
		&& needsAsked.cycleType == G_CYC_1CYCLE && needsAsked.isRect == 0, "its value groups asked for");
	check(p->ops->setTextureSize == NULL && p->ops->setEnableAlphaTest == NULL, "not the texrect drawer's");
	clear_log();
	p->ops->activate(p);
	check(strcmp(log_, "use:0 ") == 0, "activated");
	clear_log();
	valuesSent = NULL;
	p->ops->update(p, 1);
	check(strcmp(log_, "use:0 update-values:51 values:0 ") == 0 && valuesSent != NULL && valuesSent->fogColor[0] == 0.5f,
		"updated: its groups' values sent from its own");

	/* two cycles: the second's texels swapped, its flags */
	memset(&color, 0, sizeof(color));
	memset(&alpha, 0, sizeof(alpha));
	color.numStages = 2;
	alpha.numStages = 2;
	stage(&color.stage[0], 1, LOAD, G_GCI_PRIMITIVE, 0, 0, 0, 0);
	stage(&alpha.stage[0], 1, LOAD, G_GCI_SHADE_ALPHA, 0, 0, 0, 0);
	stage(&color.stage[1], 1, LOAD, G_GCI_TEXEL0, 0, 0, 0, 0);
	stage(&alpha.stage[1], 1, INTER, G_GCI_LOD_FRACTION, G_GCI_NOISE, G_GCI_TEXEL1_ALPHA, 0, 0);
	{
		gDPCombine cm;
		u64 mux;
		memset(&cm, 0xff, sizeof(cm));
		cm.mRGB1 = G_CCMUX_COMBINED;
		cm.aA1 = G_ACMUX_COMBINED;
		cm.mA1 = 1;
		mux = cm.mux & 0x00ffffffffffffffull;
		q = ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_2CYCLE, 0, 1, mux));
	}
	check(q != NULL && lastDesc.ops[4] == ((LOAD << 8) | G_GCI_PRIMITIVE) && lastDesc.ops[12] == ((LOAD << 8) | G_GCI_TEXEL1)
		&& lastDesc.ops[8] == ((INTER << 8) | G_GCI_LOD_FRACTION) && lastDesc.ops[9] == G_GCI_NOISE && lastDesc.ops[10] == G_GCI_TEXEL0_ALPHA,
		"two cycles: the first's texels kept, the second's swapped");
	check(lastDesc.flags == (VKR_CF_ALPHA_ABD | VKR_CF_COLOR_C | VKR_CF_TWO_STAGES | VKR_CF_TEXTURED | VKR_CF_TILE0 | VKR_CF_TILE1 | VKR_CF_LOD
		| VKR_CF_BILERP1 | (G_CYC_2CYCLE << VKR_CF_CYCLE_SHIFT)), "its flags");
	check(needsAsked.usesNoise == 1 && needsAsked.usesLOD == 1 && needsAsked.usesTile1 == 1 && needsAsked.usesTile0 == 1
		&& needsAsked.cycleType == G_CYC_2CYCLE && q->ops->usesLOD(q) && q->ops->usesShade(q) && q->ops->usesTile(q, 1),
		"what it reads");
	{
		gDPCombine cm;
		memset(&cm, 0xff, sizeof(cm));
		cm.mA1 = G_ACMUX_COMBINED;
		cm.aA1 = G_ACMUX_COMBINED;
		cm.aRGB1 = G_CCMUX_COMBINED;
		cm.mRGB1 = 1;
		ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_2CYCLE, 0, 0, cm.mux & 0x00ffffffffffffffull));
		check((lastDesc.flags & (VKR_CF_ALPHA_C | VKR_CF_ALPHA_ABD | VKR_CF_COLOR_C | VKR_CF_COLOR_ABD)) == (VKR_CF_ALPHA_C | VKR_CF_COLOR_ABD),
			"alpha C, not D as well; colour D");
		memset(&cm, 0xff, sizeof(cm));
		cm.aRGB1 = G_CCMUX_COMBINED;
		cm.mRGB1 = 1;
		ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_2CYCLE, 0, 0, cm.mux & 0x00ffffffffffffffull));
		check((lastDesc.flags & (VKR_CF_ALPHA_C | VKR_CF_ALPHA_ABD | VKR_CF_COLOR_C | VKR_CF_COLOR_ABD)) == VKR_CF_COLOR_ABD,
			"colour D alone");
	}
	/* the alpha stage's inputs */
	{
		Combiner c2, a2;
		gli_cprog *n;
		memset(&c2, 0, sizeof(c2));
		memset(&a2, 0, sizeof(a2));
		c2.numStages = a2.numStages = 1;
		stage(&c2.stage[0], 1, LOAD, G_GCI_PRIMITIVE, 0, 0, 0, 0);
		stage(&a2.stage[0], 1, LOAD, G_GCI_LOD_FRACTION, 0, 0, 0, 0);
		n = ops->createCombinerProgram(ctx, &c2, &a2, 0);
		check((lastDesc.flags & VKR_CF_LOD) != 0 && (lastDesc.flags & VKR_CF_TEXTURED) != 0 && (lastDesc.flags & VKR_CF_TILE0) == 0
			&& n->ops->usesLOD(n), "the alpha stage's inputs read; the LOD alone textured");
		n->ops->free_program(n);
	}
	/* noise without the LOD */
	{
		Combiner c2, a2;
		gli_cprog *n;
		memset(&c2, 0, sizeof(c2));
		memset(&a2, 0, sizeof(a2));
		c2.numStages = a2.numStages = 1;
		stage(&c2.stage[0], 1, LOAD, G_GCI_NOISE, 0, 0, 0, 0);
		stage(&a2.stage[0], 1, LOAD, G_GCI_PRIMITIVE, 0, 0, 0, 0);
		n = ops->createCombinerProgram(ctx, &c2, &a2, 0);
		check((lastDesc.flags & VKR_CF_LOD) == 0 && !n->ops->usesLOD(n) && needsAsked.usesNoise == 1, "noise is not the LOD");
		n->ops->free_program(n);
	}
	/* a second stage only with both; not in one cycle */
	alpha.numStages = 1;
	ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_2CYCLE, 0, 0, 0));
	check(lastDesc.ops[8] == -1 && lastDesc.ops[12] == -1 && (lastDesc.flags & VKR_CF_TWO_STAGES) == 0, "one alpha stage: one");
	alpha.numStages = 2;
	stage(&alpha.stage[1], 1, LOAD, G_GCI_TEXEL0, 0, 0, 0, 0);
	ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_COPY, 0, 0, 0));
	check(lastDesc.ops[8] == -1 && (lastDesc.flags & VKR_CF_TWO_STAGES) == 0 && (lastDesc.flags >> VKR_CF_CYCLE_SHIFT) == G_CYC_COPY,
		"not in another cycle");

	/* hardware lighting: shade colour read, not a rect, allowed */
	memset(&color, 0, sizeof(color));
	memset(&alpha, 0, sizeof(alpha));
	color.numStages = alpha.numStages = 1;
	stage(&color.stage[0], 1, LOAD, G_GCI_SHADE, 0, 0, 0, 0);
	stage(&alpha.stage[0], 1, LOAD, G_GCI_PRIMITIVE, 0, 0, 0, 0);
	hwLighting = 1;
	p = ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_1CYCLE, 0, 0, 0));
	check((lastDesc.flags & VKR_CF_HWLIGHT) != 0 && needsAsked.usesHwLighting == 1 && p->ops->usesHwLighting(p)
		&& (lastDesc.flags & VKR_CF_TEXTURED) == 0, "hardware lighting");
	p = ops->createCombinerProgramAsync(ctx, &color, &alpha, KEY(G_CYC_1CYCLE, 1, 0, 0));
	check((lastDesc.flags & VKR_CF_HWLIGHT) == 0 && lastRect == 1 && needsAsked.isRect == 1 && !p->ops->usesHwLighting(p),
		"not for a rect; built the same deferred");
	hwLighting = 0;
	ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_1CYCLE, 0, 0, 0));
	check((lastDesc.flags & VKR_CF_HWLIGHT) == 0, "not when not allowed");
	hwLighting = 1;
	stage(&color.stage[0], 1, LOAD, G_GCI_SHADE_ALPHA, 0, 0, 0, 0);
	ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_1CYCLE, 0, 0, 0));
	check((lastDesc.flags & VKR_CF_HWLIGHT) == 0, "not without the shade colour");
	hwLighting = 0;

	/* the renderer refusing: none, nothing kept */
	combinerFails = 1;
	check(ops->createCombinerProgram(ctx, &color, &alpha, 0) == NULL, "none when it cannot");
	combinerFails = 0;

	/* the pool: freed programs taken again; past it the heap */
	clear_log();
	q->ops->free_program(q);
	check(has("progdel:"), "freed: its program deleted");
	p = ops->createCombinerProgram(ctx, &color, &alpha, 0);
	check(p == q, "its cell taken again");
	{
		gli_cprog *many[4100];
		int n;
		for (n = 0; n < 4100; n++)
			many[n] = ops->createCombinerProgram(ctx, &color, &alpha, (u64)n);
		check(many[4099] != NULL && many[4099]->key == 4099, "past the pool too");
		for (n = 0; n < 4100; n++)
			many[n]->ops->free_program(many[n]);
	}
	stop();
}

static void test_specials(void)
{
	gli_cprog *s;
	start();
	clear_log();
	gDP.changed = 0;
	s = ops->createDepthFogShader(ctx);
	check(s != NULL && s->key == 0 && has("specialnew:"), "the depth fog shader");
	{
		char want[64];
		sprintf(want, "specialnew:%d:%d", VKR_VS_SPECIAL, VKR_FS_SHADOW_MAP);
		check(has(want), "its stages");
	}
	gDP.fogColor.r = 0.25f; gDP.fogColor.g = 0.5f; gDP.fogColor.b = 0.75f; gDP.fogColor.a = 1.0f;
	clear_log();
	s->ops->activate(s);
	{
		char want[160];
		sprintf(want, "use:%d blend:%d:%d:%d:%d sparams:%d:0.25:0.5:0.75:1 palette ", programCount - 1, VKR_BLEND_SRC_ALPHA,
			VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA, programCount - 1);
		check(strcmp(log_, want) == 0 && (gDP.changed & CHANGED_COMBINE), "used, blended, the fog colour, the palette");
	}
	check(s->ops->usesTexture(s) && s->ops->usesTile(s, 0) && !s->ops->usesTile(s, 1) && !s->ops->usesShade(s)
		&& !s->ops->usesLOD(s) && !s->ops->usesHwLighting(s) && s->ops->setEnableAlphaTest == NULL, "what a special reads");
	clear_log();
	s->ops->update(s, 1);
	check(log_[0] == '\0', "nothing to update");
	s->ops->free_program(s);
	check(has("progdel:"), "freed");

	clear_log();
	s = ops->createTexrectDrawerClearShader(ctx);
	gDP.changed = 0;
	s->ops->activate(s);
	{
		char want[64];
		sprintf(want, "specialnew:%d:%d use:%d ", VKR_VS_SPECIAL, VKR_FS_TEXRECT_CLEAR, programCount - 1);
		check(strcmp(log_, want) == 0 && (gDP.changed & CHANGED_COMBINE), "the texrect clear shader");
	}
	s->ops->free_program(s);
	clear_log();
	s = ops->createTexrectCopyShader(ctx);
	{
		char want[64];
		sprintf(want, "specialnew:%d:%d ", VKR_VS_SPECIAL, VKR_FS_TEXRECT_COPY);
		check(strcmp(log_, want) == 0, "the texrect copy shader");
	}
	s->ops->free_program(s);

	config.gammaCorrection.force = 1;
	config.gammaCorrection.level = 1.5f;
	clear_log();
	s = ops->createGammaCorrectionShader(ctx);
	{
		char want[96];
		sprintf(want, "specialnew:%d:%d sparams:%d:1.5:0:0:0 ", VKR_VS_SPECIAL, VKR_FS_GAMMA, programCount - 1);
		check(strcmp(log_, want) == 0, "gamma at the level forced");
	}
	s->ops->free_program(s);
	config.gammaCorrection.force = 0;
	clear_log();
	s = ops->createGammaCorrectionShader(ctx);
	check(strstr(log_, ":2:0:0:0 ") != NULL, "else 2");
	s->ops->free_program(s);

	clear_log();
	s = ops->createFXAAShader(ctx);
	{
		char want[64];
		sprintf(want, "specialnew:%d:%d ", VKR_VS_FXAA, VKR_FS_FXAA);
		check(strcmp(log_, want) == 0, "the FXAA shader");
	}
	memset(&drawnBuffer, 0, sizeof(drawnBuffer));
	{
		struct gli_tex t;
		memset(&t, 0, sizeof(t));
		t.realWidth = 640;
		t.realHeight = 480;
		drawnBuffer.fb.m_pTexture = &t;
		found = &drawnBuffer;
		viOrigin = 0x1234;
		clear_log();
		gDP.changed = 0;
		s->ops->activate(s);
		{
			char want[96];
			sprintf(want, "use:%d find:4660 sparams:%d:640:480:0:0 ", programCount - 1, programCount - 1);
			check(strcmp(log_, want) == 0 && (gDP.changed & CHANGED_COMBINE), "the shown buffer's size");
		}
		drawnBuffer.fb.m_pTexture = NULL;
		clear_log();
		s->ops->activate(s);
		check(strstr(log_, ":640:480:0:0") != NULL, "kept without its texture");
		found = NULL;
		clear_log();
		s->ops->activate(s);
		check(strstr(log_, ":640:480:0:0") != NULL, "kept without one");
	}
	s->ops->free_program(s);

	clear_log();
	s = ops->createTexrectDrawerDrawShader(ctx);
	{
		char want[64];
		sprintf(want, "specialnew:%d:%d ", VKR_VS_SPECIAL, VKR_FS_TEXRECT_DRAW);
		check(strcmp(log_, want) == 0 && s->ops->setEnableAlphaTest != NULL && s->ops->setTextureSize != NULL, "the texrect draw shader");
	}
	gDP.otherMode.depthSource = G_ZS_PRIM;
	gDP.primDepth.z = 0.75f;
	clear_log();
	gDP.changed = 0;
	s->ops->activate(s);
	check(strstr(log_, ":0:0.75:0:0 ") != NULL && (gDP.changed & CHANGED_COMBINE), "no alpha test, the primitive's depth");
	gDP.changed = 0;
	s->ops->setEnableAlphaTest(s, 1);
	check((gDP.changed & CHANGED_COMBINE) != 0, "the alpha test changes the combine");
	clear_log();
	s->ops->setTextureSize(s, 4, 5);
	check(log_[0] == '\0', "its texture size unused");
	gDP.otherMode.depthSource = 0;
	s->ops->activate(s);
	check(strstr(log_, ":1:0:0:0 ") != NULL, "the alpha test; no primitive depth");
	s->ops->free_program(s);

	clear_log();
	ops->resetShaderProgram(ctx);
	check(strcmp(log_, "use:-1 ") == 0, "no program");
	stop();
}

static void test_builder(void)
{
	char path[128];
	u32 version = 0;
	optionsBits = 5;
	start();
	check(!ops->isCombinerProgramBuilderObsolete(ctx) && !ops->isCombinerUberOnly(ctx), "current with the options");
	clear_log();
	ops->resetCombinerProgramBuilder(ctx);
	check(log_[0] == '\0', "not made again");
	optionsBits = 6;
	check(ops->isCombinerProgramBuilderObsolete(ctx), "old with other options");
	clear_log();
	ops->resetCombinerProgramBuilder(ctx);
	check(strncmp(log_, "destroy:1 create window:", 24) == 0, "made again");
	check(!ops->isCombinerProgramBuilderObsolete(ctx), "with the options it was made with");
	ops->getCombinerJournalPath(ctx, path, sizeof(path), &version);
	check(strcmp(path, "/cache/GLideN64.vulkan.keys.journal") == 0 && version == 1, "the journal's path and version");
	ops->getCombinerJournalPath(ctx, path, 10, &version);
	check(strlen(path) == 9, "cut to its size");
	check(!ops->saveShadersStorage(ctx, NULL) && !ops->loadShadersStorage(ctx, NULL), "no shader storage");
	stop();
}

static void test_draw(void)
{
	SPVertex v[3];
	u16 idx[3] = { 2, 1, 0 };
	RectVertex rv[4];
	gli_draw_triangles_params tp = GLI_DRAW_TRIANGLES_PARAMS_DEFAULT;
	gli_draw_rects_params rp = GLI_DRAW_RECTS_PARAMS_DEFAULT;
	int i;
	start();
	memset(v, 0, sizeof(v));
	for (i = 0; i < 3; i++) {
		v[i].x = 1.0f + i; v[i].y = 2.0f + i; v[i].z = 3.0f + i; v[i].w = 4.0f + i;
		v[i].r = 0.1f * i; v[i].g = 0.2f; v[i].b = 0.3f; v[i].a = 0.4f;
		v[i].flat_r = 0.5f; v[i].flat_g = 0.6f; v[i].flat_b = 0.7f; v[i].flat_a = 0.8f;
		v[i].s = 5.0f; v[i].t = 6.0f + i; v[i].modify = 9 + i; v[i].HWLight = 3;
	}
	tp.mode = GLI_DM_TRIANGLES;
	tp.verticesCount = 3;
	tp.vertices = v;
	upscalerActive = 0;
	hwLighting = 0;
	clear_log();
	ops->drawTriangles(ctx, &tp);
	{
		char want[96];
		sprintf(want, "trireserve:3 drawtris:%d:3:0 jitter:0:0 ", VKR_TOPO_TRIANGLES);
		check(strcmp(log_, want) == 0, "triangles: written, drawn, jitter cleared");
	}
	check(triRing[2].x == 3.0f && triRing[2].y == 4.0f && triRing[2].z == 5.0f && triRing[2].w == 6.0f && triRing[2].r == v[2].r
		&& triRing[1].g == 0.2f && triRing[1].b == 0.3f && triRing[1].a == 0.4f && triRing[2].s == 5.0f && triRing[2].t == 8.0f
		&& triRing[2].modify == 11 && triRing[0].num_lights == 0.0f, "its vertices, no lights");
	tp.flatColors = 1;
	tp.elements = idx;
	tp.elementsCount = 3;
	tp.mode = GLI_DM_TRIANGLE_STRIP;
	hwLighting = 1;
	clear_log();
	ops->drawTriangles(ctx, &tp);
	{
		char want[96];
		sprintf(want, "trireserve:3 indexreserve:3 drawtris:%d:3:3 jitter:0:0 ", VKR_TOPO_STRIP);
		check(strcmp(log_, want) == 0, "indexed, a strip");
	}
	check(triRing[1].r == 0.5f && triRing[1].g == 0.6f && triRing[1].b == 0.7f && triRing[1].a == 0.8f && triRing[2].num_lights == 3.0f,
		"flat colours; the lights");
	check(indexRing[0] == 2 && indexRing[1] == 1 && indexRing[2] == 0, "its elements");
	tp.mode = GLI_DM_LINES;
	tp.elements = NULL;
	clear_log();
	ops->drawTriangles(ctx, &tp);
	{
		char want[64];
		sprintf(want, "drawtris:%d:3:0", VKR_TOPO_LINES);
		check(has(want) && !has("indexreserve"), "lines; no elements without them");
	}

	/* the upscaler */
	upscalerActive = 1;
	drawn = NULL;
	tp.mode = GLI_DM_TRIANGLES;
	tp.elements = idx;
	clear_log();
	ops->drawTriangles(ctx, &tp);
	check(!has("uptris") && !has("colorimage"), "nothing drawn into: not told");
	memset(&drawnBuffer, 0, sizeof(drawnBuffer));
	drawnBuffer.fb.m_FBO = 33;
	drawnBuffer.fb.m_startAddress = 0x100000;
	drawnBuffer.fb.m_width = 320;
	drawnBuffer.fb.m_size = 2;
	drawnBuffer.fb.m_scale = 2.0f;
	drawn = &drawnBuffer;
	drawFbo = 34;
	clear_log();
	ops->drawTriangles(ctx, &tp);
	check(!has("uptris"), "another frame buffer drawn into: not told");
	drawFbo = 33;
	drawState = 4;
	config.frameBufferEmulation.N64DepthCompare = 0;
	gDP.combine.mux = 0x1111222233334444ull;
	gDP.otherMode._u64 = 0x5555666677778888ull;
	gli_cmb.current = NULL;
	gli_texture_current[0] = NULL;
	clear_log();
	ops->drawTriangles(ctx, &tp);
	check(has("colorimage:1048576:320:1 uptris jitter:0.25:-0.5 drawtris") && has("jitter:0:0"), "told, moved by its jitter, then not");
	check(upTris == triRing && upCounts[0] == 3 && upIndices == idx && upCounts[1] == 3 && upStrip == 0, "the vertices and elements");
	check(lastView.viewport[0] == 1 && lastView.viewport[3] == 4 && lastView.screen_scale[0] == 2.0f / 320.0f
		&& lastView.screen_scale[1] == -2.0f / 240.0f && lastView.scale == 2.0f && lastView.cull == 2, "the view");
	check(lastFlags == RDP_UPSCALER_TRANSLUCENT, "not depth tested or written: translucent");
	{
		uint64_t h = 7;
		h = h * 31 + 0x33334444u; h = h * 31 + 0x11112222u; h = h * 31 + 0x77778888u; h = h * 31 + 0x55556666u;
		check(lastSignature == h, "signed by the combine and modes");
		{
			struct gli_tex t;
			memset(&t, 0, sizeof(t));
			t.crc = 0x9999aaaabbbbccccull;
			gli_texture_current[0] = &t;
			ops->drawTriangles(ctx, &tp);
			check(lastSignature == h, "a texture without a program reading it: not");
			{
				Combiner color, alpha;
				gli_cprog *pr;
				memset(&color, 0, sizeof(color));
				memset(&alpha, 0, sizeof(alpha));
				color.numStages = alpha.numStages = 1;
				stage(&color.stage[0], 1, LOAD, G_GCI_TEXEL0, 0, 0, 0, 0);
				pr = ops->createCombinerProgram(ctx, &color, &alpha, 0);
				gli_cmb.current = pr;
				ops->drawTriangles(ctx, &tp);
				check(lastSignature == (h * 31 + 0xbbbbccccu) * 31 + 0x9999aaaau, "its texture's crc where it reads tile 0");
				stage(&color.stage[0], 1, LOAD, G_GCI_TEXEL1, 0, 0, 0, 0);
				color.numStages = 2; alpha.numStages = 2;
				stage(&color.stage[1], 1, LOAD, G_GCI_TEXEL0, 0, 0, 0, 0);
				stage(&alpha.stage[1], 1, LOAD, G_GCI_SHADE, 0, 0, 0, 0);
				pr->ops->free_program(pr);
				pr = ops->createCombinerProgram(ctx, &color, &alpha, KEY(G_CYC_2CYCLE, 0, 0, 0));
				gli_cmb.current = pr;
				ops->drawTriangles(ctx, &tp);
				check(lastSignature == h, "not where it reads tile 1 alone");
				gli_texture_current[0] = NULL;
				ops->drawTriangles(ctx, &tp);
				check(lastSignature == h, "not without a texture");
				gli_cmb.current = NULL;
				pr->ops->free_program(pr);
			}
		}
	}
	drawState = 3;
	ops->drawTriangles(ctx, &tp);
	check(lastFlags == (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE), "depth tested and written");
	drawState = 5;
	ops->drawTriangles(ctx, &tp);
	check(lastFlags == (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_TRANSLUCENT), "tested, not written, translucent");
	drawState = 0;
	ops->drawTriangles(ctx, &tp);
	check(lastFlags == 0, "none");
	config.frameBufferEmulation.N64DepthCompare = 1;
	drawState = 3;
	gDP.otherMode.depthCompare = 0;
	gDP.otherMode.depthUpdate = 0;
	ops->drawTriangles(ctx, &tp);
	check(lastFlags == 0, "the N64's depth compare: its modes, not the renderer's");
	drawState = 4;
	gDP.otherMode.depthCompare = 1;
	ops->drawTriangles(ctx, &tp);
	check(lastFlags == (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_TRANSLUCENT), "its compare; translucency kept");
	gDP.otherMode.depthUpdate = 1;
	ops->drawTriangles(ctx, &tp);
	check(lastFlags == (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE), "its update");
	config.frameBufferEmulation.N64DepthCompare = 0;
	tp.mode = GLI_DM_TRIANGLE_STRIP;
	tp.elements = NULL;
	ops->drawTriangles(ctx, &tp);
	check(upStrip == 1 && upIndices == NULL && upCounts[1] == 0, "a strip, without elements");
	upscalerActive = 0;

	/* rects */
	memset(rv, 0, sizeof(rv));
	for (i = 0; i < 4; i++) {
		rv[i].x = (f32)i; rv[i].y = 1; rv[i].z = 2; rv[i].w = 3; rv[i].s0 = 4; rv[i].t0 = 5; rv[i].s1 = 6; rv[i].t1 = 7;
	}
	rp.mode = GLI_DM_TRIANGLE_STRIP;
	rp.texrect = 1;
	rp.verticesCount = 4;
	rp.vertices = rv;
	clear_log();
	ops->drawRects(ctx, &rp);
	{
		char want[64];
		sprintf(want, "rectreserve:4 drawrects:%d:4 ", VKR_TOPO_STRIP);
		check(strcmp(log_, want) == 0, "rects: written and drawn");
	}
	check(rectRing[3].x == 3 && rectRing[3].w == 3 && rectRing[3].s0 == 4 && rectRing[3].t0 == 5 && rectRing[3].s1 == 6
		&& rectRing[3].t1 == 7, "their vertices and texture coordinates");
	rp.texrect = 0;
	rp.mode = GLI_DM_TRIANGLES;
	ops->drawRects(ctx, &rp);
	check(rectRing[3].x == 3 && rectRing[3].s0 == 0 && rectRing[3].t0 == 0 && rectRing[3].s1 == 0 && rectRing[3].t1 == 0
		&& rectRing[0].s0 == 0, "a fill rect's texture coordinates 0");
	{
		char want[32];
		sprintf(want, "drawrects:%d:4", VKR_TOPO_TRIANGLES);
		clear_log();
		ops->drawRects(ctx, &rp);
		check(has(want), "drawn as triangles");
	}
	upscalerActive = 1;
	clear_log();
	ops->drawRects(ctx, &rp);
	check(has("colorimage") && has("uprect:4") && upRect == rectRing && strstr(log_, "uprect:4 drawrects") != NULL,
		"the upscaler told, before it is drawn");
	upscalerActive = 0;
	clear_log();
	ops->drawRects(ctx, &rp);
	check(!has("uprect"), "not without it");

	clear_log();
	v[1].x = 9.0f;
	ops->drawLine(ctx, 2.5f, v);
	check(strcmp(log_, "trireserve:2 drawline:2:2.5 ") == 0 && triRing[1].x == 9.0f && triRing[1].y == 3.0f && triRing[1].z == 4.0f
		&& triRing[1].w == 5.0f && triRing[1].r == v[1].r && triRing[0].g == 0.2f && triRing[0].b == 0.3f && triRing[0].a == 0.4f
		&& triRing[1].s == 5.0f && triRing[1].t == 7.0f && triRing[1].modify == 10 && triRing[1].num_lights == 0.0f
		&& triRing[2].x != 3.0f, "a line: its two vertices, no lights");
	stop();
}

static void test_present(void)
{
	unsigned w = 9, h = 9;
	int src[4] = { 0, 0, 320, 240 }, dst[4] = { 80, 60, 720, 540 };
	gliden64_vulkan_presented_size(&w, &h);
	check(w == 0 && h == 0, "nothing presented without a renderer");
	start();
	gliden64_vulkan_presented_size(&w, &h);
	check(w == 11 && h == 22, "the presented size");
	clear_log();
	upscaledFrame = 0;
	gliden64_vulkan_present();
	check(strcmp(log_, "present ") == 0, "presented");
	upscaledFrame = 1;
	clear_log();
	gliden64_vulkan_present();
	check(strcmp(log_, "present-upscaled ") == 0 && ((unsigned char *)&presentedFrame)[0] == 0x5a, "the upscaler's frame presented");
	upscaledFrame = 0;

	upscalerActive = 0;
	clear_log();
	gliden64_vulkan_shown(0x100000, 320, 2, 240, 2.0f, src, dst);
	check(!has("shown"), "not without the upscaler");
	upscalerActive = 1;
	gliden64_vulkan_shown(0x100000, 320, 2, 240, 2.0f, src, dst);
	check(has("shown") && shownArgs[0] == 0x100000 && shownArgs[1] == 320 && shownArgs[2] == 1 && shownArgs[3] == 320
		&& shownArgs[4] == 240 && shownScale == 2.0f, "the shown buffer");
	check(shownMap[0] == 2.0f && shownMap[1] == 2.0f && shownMap[2] == 80.0f && shownMap[3] == 60.0f, "its map");
	src[0] = 10; src[1] = 20; src[2] = 110; src[3] = 70;
	gliden64_vulkan_shown(0x100000, 320, 3, 240, 2.0f, src, dst);
	check(shownMap[0] == 6.4f && shownMap[1] == 9.6f && shownMap[2] == 80.0f - 64.0f && shownMap[3] == 60.0f - 192.0f
		&& shownArgs[2] == 2, "an offset source");
	clear_log();
	src[2] = src[0];
	gliden64_vulkan_shown(0x100000, 320, 2, 240, 2.0f, src, dst);
	check(!has("shown"), "an empty width: not");
	src[2] = 110; src[3] = src[1];
	gliden64_vulkan_shown(0x100000, 320, 2, 240, 2.0f, src, dst);
	check(!has("shown"), "an empty height: not");
	upscalerActive = 0;
	stop();
}

int main(void)
{
	REG.VI_ORIGIN = &viOrigin;
	test_create();
	test_state();
	test_textures();
	test_framebuffers();
	test_stages();
	test_combiners();
	test_specials();
	test_builder();
	test_draw();
	test_present();
	if (failures != 0) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_vk_context: all passed\n");
	return 0;
}
