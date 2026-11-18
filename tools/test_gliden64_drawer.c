/* test_gliden64_drawer -- GLideN64's drawer (GraphicsDrawer.c) on stub
 * modules and a stub graphics context.
 *
 * Its data made with the context: the states set (no culling, scissor,
 * no depth write, depth compare always, depth test or not and the polygon
 * offset, the window's viewport, cleared, swapped), the modules started
 * and stopped in their order, the widest line, every state to set, the
 * triangle list emptied, and a game's textured rect special picked from
 * its name. Triangles: none or no buffer to draw into drop the list; else
 * the texrect drawer drawn out first, the states that changed set, flat
 * colours by the geometry mode, the screen coordinates viewport for
 * vertices given in screen space, drawn by their elements, or clipped as
 * the RSP clips them, a run at a time; what they drew handed to the frame
 * buffer and the software depth's buffer marked. DMA and screen space
 * triangles, taken DMA vertices drawn before they would overrun, lines
 * (wide ones as two triangles), rects and textured rects, with their
 * clamps, copy mode, immersive mode, the texrect drawer and the specials.
 * The states: culling, depth, scissor, viewport, textures, blending and
 * the depth image drawn into. Copies, blits (swapped for an inverted rect,
 * drawn where the context cannot blit) and clears.
 *
 *   ./test_gliden64_drawer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GraphicsDrawer.h"
#include "gli_drawer.h"
#include "gli_framebuffer.h"
#include "gli_fb_rdram.h"
#include "gli_blend.h"
#include "gli_combiner.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "gli_tri_clip.h"
#include "DisplayWindow.h"
#include "SoftwareRender.h"
#include "TextureFilterHandler.h"
#include "PostProcessor.h"
#include "NoiseTexture.h"
#include "ZlutTexture.h"
#include "PaletteTexture.h"
#include "DepthBuffer.h"
#include "FrameBufferInfo.h"
#include "TexrectDrawer.h"
#include "Config.h"
#include "GBI.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "Graphics/gli_context.h"

static int failures;
static s32 viewport[4];

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

Config gliden64Config;
gDPInfo gDP;
gSPInfo gSP;
RSPInfo RSP;
VIInfo VI;
gli_window g_window;
gli_context gli_ctx;
gli_combiner_info gli_cmb;
gli_tex *gli_texture_current[2];
u32 G_SHADING_SMOOTH = 0x200;
static u8 rdram[0x200000];
u8 *RDRAM = rdram;
extern u32 rectDepthBufferCopyFrame;

static char log_[8192];
static void say(const char *_s)
{
	if (strlen(log_) + strlen(_s) + 2 < sizeof(log_)) {
		strcat(log_, _s);
		strcat(log_, " ");
	}
}
static void sayf(const char *_f, long _a, long _b)
{
	char b[64];
	sprintf(b, _f, _a, _b);
	say(b);
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

/* ---- the modules started and stopped ---- */

void gli_textures_init(void) { say("textures-init"); }
void gli_textures_destroy(void) { say("textures-destroy"); }
void gli_depth_list_init(void) { say("depth-init"); }
void gli_depth_list_destroy(void) { say("depth-destroy"); }
void gli_fb_emulation_init(void) { say("fb-init"); }
void gli_fb_emulation_destroy(void) { say("fb-destroy"); }
void gli_combiner_init(void) { say("combiner-init"); }
void gli_combiner_destroy(void) { say("combiner-destroy"); }
void gli_TFH_init(void) { say("tfh-init"); }
static int tfhChanged;
bool gli_TFH_options_changed(void) { return tfhChanged != 0; }
void gli_TFH_shutdown(void) { say("tfh-shutdown"); }
void gli_pp_init(void) { say("pp-init"); }
void gli_pp_destroy(void) { say("pp-destroy"); }
void gli_zlut_init(void) { say("zlut-init"); }
void gli_zlut_destroy(void) { say("zlut-destroy"); }
void gli_noise_init(void) { say("noise-init"); }
void gli_noise_destroy(void) { say("noise-destroy"); }
void gli_palette_init(void) { say("palette-init"); }
void gli_palette_destroy(void) { say("palette-destroy"); }
void gli_fbinfo_reset(void) { say("fbinfo-reset"); }
void gli_texrect_init(void) { say("texrect-init"); }
void gli_texrect_destroy(void) { say("texrect-destroy"); }
void gli_window_swap_buffers(void) { say("swap"); }

/* ---- frame buffers ---- */

struct FrameBuffer { gli_fb fb; };
static struct FrameBuffer buffers[2];
static DepthBuffer depth;
static struct FrameBuffer *current;
static f32 changedMaxY = -1.0f;
static int dirtied, removedAt, copyDepthOk = 1, copyDepths;
struct FrameBuffer *gli_fb_current(void) { return current; }
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return &_p->fb; }
struct FrameBuffer *gli_fb_find(u32 _a)
{
	u32 i;
	for (i = 0; i < 2; ++i)
		if (_a >= buffers[i].fb.m_startAddress && _a < buffers[i].fb.m_endAddress)
			return &buffers[i];
	return NULL;
}
void gli_fb_set_changed(f32 _maxY) { changedMaxY = _maxY; say("fb-changed"); }
void gli_fb_set_dirty(gli_fb *_fb) { check(_fb == &buffers[1].fb, "the depth image's buffer dirtied"); dirtied++; }
void gli_fb_remove(u32 _a) { removedAt = (int)_a; say("fb-remove"); }
bool gli_fb_copy_depth(u32 _a) { (void)_a; copyDepths++; say("copy-depth"); return copyDepthOk != 0; }
void gli_fb_coords_scales(const struct FrameBuffer *_p, f32 *_x, f32 *_y)
{
	*_x = _p == NULL ? 1.0f : 2.0f;
	*_y = *_x;
	say(_p == NULL ? "scales-window" : "scales-buffer");
}
u16 gli_VI_GetMaxBufferHeight(u16 _w) { return _w > 320 ? 480 : 240; }
static int depthImage;
bool gli_isCurrentColorImageDepthImage(void) { return depthImage != 0; }
static gli_tex depthCopy;
static int noDepthCopy;
struct gli_tex *gli_depth_copy_texture(DepthBuffer *_d, const struct gli_fb *_b)
{
	check(_d == &depth && _b == &buffers[0].fb, "the current buffer's depth copied");
	say("depth-copy");
	return noDepthCopy ? NULL : &depthCopy;
}
void gli_depth_activate_texture(DepthBuffer *_d, const struct gli_fb *_b)
{
	check(_d == &depth && _b == &buffers[0].fb, "the current buffer's depth activated");
	say("depth-activate");
}
void gli_depth_clear(void) { say("depth-clear"); }

/* ---- the combiner, textures and blending ---- */

static int usesTile[2] = { 1, 0 }, usesTexture = 1, activated, updated;
static void p_activate(void *_p) { (void)_p; activated++; say("activate"); }
static void p_update(void *_p, bool _f) { (void)_p; check(!_f, "updated, not forced"); updated++; say("program-update"); }
static bool p_uses_texture(const void *_p) { (void)_p; return usesTexture != 0; }
static bool p_uses_tile(const void *_p, u32 _t) { (void)_p; return usesTile[_t] != 0; }
static const gli_cprog_ops progOps = { p_activate, p_update, NULL, NULL, p_uses_texture, p_uses_tile, NULL, NULL, NULL, NULL };
static gli_cprog program = { &progOps, 1 }, copyProgram = { &progOps, 2 };
static int rectMode = -1;
void gli_combiner_update(void) { say("combiner-update"); }
void gli_combiner_update_parameters(void) { say("combiner-parameters"); }
void gli_combiner_set_rect_mode(bool _r) { rectMode = _r; }
void gli_combiner_set_depth_fog(void) { say("depth-fog"); }
void gli_textures_update(u32 _t) { sayf("tex-update%ld", (long)_t, 0); }
void gli_textures_activate_dummy(u32 _t) { sayf("tex-dummy%ld", (long)_t, 0); }
static gli_blend_state blendOut = { 1, GLI_BF_SRC_ALPHA, GLI_BF_ONE_MINUS_SRC_ALPHA, 0 };
static int blendLegacy = -1, blendBlast = -1;
void gli_blend_mode(int _legacy, int _blast, gli_blend_state *_out) { blendLegacy = _legacy; blendBlast = _blast; *_out = blendOut; }

/* ---- the drawer's maths ---- */

static int cullMode;
int gli_drawer_cull_mode(void) { return cullMode; }
static gli_depth_state depthState;
void gli_drawer_depth_state(gli_depth_state *_out) { *_out = depthState; }
static int needAdjust;
int gli_drawer_need_adjust(int _a) { needAdjust = _a; return _a; }
static f32 scissorArgs[5];
void gli_drawer_scissor_rect(f32 _sx, f32 _sy, f32 _ox, f32 _oy, int _adjust, f32 _as, int _imm, s32 *_rect)
{
	scissorArgs[0] = _sx; scissorArgs[1] = _sy; scissorArgs[2] = _ox; scissorArgs[3] = _oy; scissorArgs[4] = _as;
	(void)_adjust; (void)_imm;
	_rect[0] = 1; _rect[1] = 2; _rect[2] = 3; _rect[3] = 4;
}
static f32 viewportArgs[3];
static int viewportBuffer = -1;
void gli_drawer_viewport_rect(int _buffer, f32 _sx, f32 _sy, u32 _ox, u32 _oy, int _adjust, f32 _as, s32 *_rect)
{
	viewportBuffer = _buffer; viewportArgs[0] = _sx; viewportArgs[1] = _sy; viewportArgs[2] = (f32)_ox;
	(void)_oy; (void)_adjust; (void)_as;
	_rect[0] = 5; _rect[1] = 6; _rect[2] = 7; _rect[3] = 8;
}
static int fillOk = 1;
int gli_drawer_fill_rect(RectVertex *_v, const s32 *_r, const f32 *_scale, int _adj, f32 _as)
{
	(void)_v; (void)_adj; (void)_as;
	check(_r[0] == 10 && _r[1] == 20 && _r[2] == 30 && _r[3] == 40 && _scale[0] == 2.0f, "the rect filled as given, at the scales");
	say("fill-rect");
	return fillOk;
}
static u32 clampOut;
static const gli_tex *texrectTex[2];
u32 gli_drawer_texrect_vertices(RectVertex *_v, const gli_texrect_params *_p, const gli_tex *const *_tex,
	const f32 *_scale, int _adj, f32 _as)
{
	(void)_v; (void)_p; (void)_scale; (void)_adj; (void)_as;
	texrectTex[0] = _tex[0];
	texrectTex[1] = _tex[1];
	say("texrect-vertices");
	return clampOut;
}
static int corrected;
void gli_drawer_texrect_correct(f32 *_rect, f32 *_prev) { (void)_rect; (void)_prev; corrected++; }
static int immersiveRect, immersiveFull;
int gli_drawer_immersive_rect(const f32 *_rect, int *_fullScreen) { (void)_rect; *_fullScreen = immersiveFull; return immersiveRect; }
void gli_drawer_immersive_clip(RectVertex *_v, int _full, const f32 *_scale) { (void)_v; (void)_scale; sayf("immersive-clip%ld", (long)_full, 0); }
void gli_drawer_thick_line(SPVertex *_out, u32 _v0, u32 _v1, f32 _half, u32 _flag)
{
	(void)_out; (void)_flag;
	check(_v0 == 1 && _v1 == 2 && _half == 1.5f, "a thick line from its vertices, half its width either side");
	say("thick-line");
}
static gli_copy_rect copied;
void gli_drawer_copy_rect(RectVertex *_v, const gli_copy_rect *_p) { (void)_v; copied = *_p; say("copy-rect"); }
static u32 clipOut = 3;
static u32 clipCalls, clipFirst[64], clipCount[64];
static const void *clipVertices[64];
u32 gli_tri_clip(const struct SPVertex *_vertices, const u16 *_elements, u32 _count, struct SPVertex *_out)
{
	(void)_out;
	if (clipCalls < 64) {
		clipVertices[clipCalls] = _vertices;
		clipFirst[clipCalls] = _elements != NULL ? (u32)(_elements - gli_tris.elements) : 0xFFFF;
		clipCount[clipCalls] = _count;
	}
	clipCalls++;
	return clipOut;
}
static int outsideAt = -1;
static SPVertex *dmaBase;
u32 gli_tri_clip_vertex_outside(const struct SPVertex *_v) { return outsideAt >= 0 && _v == dmaBase + outsideAt; }
static f32 renderedMaxY = 17.0f;
static u32 renderedCount;
static const u16 *renderedElements;
f32 gli_renderTriangles(const SPVertex *_v, const u16 *_e, u32 _n) { (void)_v; renderedElements = _e; renderedCount = _n; say("render"); return renderedMaxY; }
static int immersiveActive;
bool gli_gSPImmersiveActive(void) { return immersiveActive != 0; }
void gli_gSPImmersiveScreenTriangles(SPVertex *_v, u32 _n) { (void)_v; sayf("immersive-screen%ld", (long)_n, 0); }

/* ---- the texrect drawer and the CPU texrects ---- */

static int texrectEmpty = 1, canContinue, addRect, depthCopyRect, bgCopy, paletteMod;
bool gli_texrect_draw(void) { say("texrect-draw"); return 1; }
bool gli_texrect_is_empty(void) { return texrectEmpty != 0; }
bool gli_texrect_can_continue(void) { return canContinue != 0; }
bool gli_texrect_add_rect(void) { say("add-rect"); return addRect != 0; }
void gli_texrect_add_background_rect(void) { say("add-bg-rect"); }
int gli_texrect_is_depth_copy(void) { return depthCopyRect; }
void gli_texrect_depth_row(const gli_texrect_params *_p) { (void)_p; say("depth-row"); }
int gli_texrect_bg_copy(const gli_texrect_params *_p) { (void)_p; say("bg-copy"); return bgCopy; }
int gli_texrect_palette_mod(const gli_texrect_params *_p) { (void)_p; say("palette-mod"); return paletteMod; }
void gli_RDP_RepeatLastLoadBlock(void) { say("repeat-load"); }

/* ---- the context ---- */

static u32 enables[16];
static int enableSet[16];
static int en_index(u32 _p)
{
	switch (_p) {
	case GLI_EN_BLEND: return 0;
	case GLI_EN_CULL_FACE: return 1;
	case GLI_EN_DEPTH_TEST: return 2;
	case GLI_EN_POLYGON_OFFSET_FILL: return 3;
	case GLI_EN_SCISSOR_TEST: return 4;
	}
	return 5;
}
static const char *const enNames[6] = { "blend", "cull", "depth-test", "offset", "scissor", "?" };
static void c_enable(void *_i, u32 _p, bool _e)
{
	char b[32];
	(void)_i;
	enables[en_index(_p)] = _e;
	enableSet[en_index(_p)]++;
	sprintf(b, "%s=%d", enNames[en_index(_p)], _e ? 1 : 0);
	say(b);
}
static u32 culled = 0xFFFF, depthCompare = 0xFFFF, clampMode = 0xFFFF, blendSrc, blendDst;
static int depthWrite = -1;
static s32 scissor[4];
static f32 blendColor[4], clearColor[4], offset[2];
static void c_cull(void *_i, u32 _m) { (void)_i; culled = _m; say("cull-face"); }
static void c_depth_write(void *_i, bool _e) { (void)_i; depthWrite = _e; sayf("depth-write=%ld", (long)_e, 0); }
static void c_depth_compare(void *_i, u32 _m) { (void)_i; depthCompare = _m; say("depth-compare"); }
static void c_clamp(void *_i, u32 _m) { (void)_i; clampMode = _m; say("clamp"); }
static void c_viewport(void *_i, s32 _x, s32 _y, s32 _w, s32 _h)
{
	(void)_i; viewport[0] = _x; viewport[1] = _y; viewport[2] = _w; viewport[3] = _h; say("viewport");
}
static void c_scissor(void *_i, s32 _x, s32 _y, s32 _w, s32 _h)
{
	(void)_i; scissor[0] = _x; scissor[1] = _y; scissor[2] = _w; scissor[3] = _h; say("scissor");
}
static void c_blending(void *_i, u32 _s, u32 _d) { (void)_i; blendSrc = _s; blendDst = _d; say("blending"); }
static void c_blend_color(void *_i, f32 _r, f32 _g, f32 _b, f32 _a)
{
	(void)_i; blendColor[0] = _r; blendColor[1] = _g; blendColor[2] = _b; blendColor[3] = _a; say("blend-color");
}
static void c_clear(void *_i, f32 _r, f32 _g, f32 _b, f32 _a)
{
	(void)_i; clearColor[0] = _r; clearColor[1] = _g; clearColor[2] = _b; clearColor[3] = _a; say("clear");
}
static void c_offset(void *_i, f32 _f, f32 _u) { (void)_i; offset[0] = _f; offset[1] = _u; say("polygon-offset"); }
static gli_tex_parameters texParams[4];
static int texParamsSet;
static void c_tex_params(void *_i, const gli_tex_parameters *_p) { (void)_i; texParams[texParamsSet++ & 3] = *_p; say("tex-params"); }
static gli_draw_triangles_params tris[8];
static int trisDrawn;
static void c_triangles(void *_i, const gli_draw_triangles_params *_p) { (void)_i; tris[trisDrawn++ & 7] = *_p; say("triangles"); }
static gli_draw_rects_params rects;
static int rectsDrawn;
static void c_rects(void *_i, const gli_draw_rects_params *_p) { (void)_i; rects = *_p; rectsDrawn++; say("rects"); }
static f32 lineWidth;
static SPVertex lineVertices[2];
static void c_line(void *_i, f32 _w, void *_v) { (void)_i; lineWidth = _w; memcpy(lineVertices, _v, sizeof(lineVertices)); say("line"); }
static f32 c_max_line(void *_i) { (void)_i; return 4.0f; }
static gli_blit_params blit;
static int blitOk = 1;
static bool c_blit(void *_i, const gli_blit_params *_p) { (void)_i; blit = *_p; say("blit"); return blitOk != 0; }
static u32 boundRead, boundDraw;
static void c_bind(void *_i, u32 _t, u32 _n)
{
	(void)_i;
	if (_t == GLI_BT_READ_FRAMEBUFFER)
		boundRead = _n;
	else
		boundDraw = _n;
	say("bind-fb");
}
static gli_context_ops ops;

static void setup(void)
{
	memset(&ops, 0, sizeof(ops));
	ops.enable = c_enable;
	ops.cullFace = c_cull;
	ops.enableDepthWrite = c_depth_write;
	ops.setDepthCompare = c_depth_compare;
	ops.setClampMode = c_clamp;
	ops.setViewport = c_viewport;
	ops.setScissor = c_scissor;
	ops.setBlending = c_blending;
	ops.setBlendColor = c_blend_color;
	ops.clearColorBuffer = c_clear;
	ops.setPolygonOffset = c_offset;
	ops.setTextureParameters = c_tex_params;
	ops.drawTriangles = c_triangles;
	ops.drawRects = c_rects;
	ops.drawLine = c_line;
	ops.getMaxLineWidth = c_max_line;
	ops.blitFramebuffers = c_blit;
	ops.bindFramebuffer = c_bind;
	gli_ctx.ops = &ops;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_cmb.current = &program;
	gli_cmb.texrectCopy = &copyProgram;
	g_window.screenWidth = 800;
	g_window.screenHeight = 600;
	g_window.heightOffset = 30;
	g_window.scaleX = 2.5f;
	g_window.scaleY = 2.0f;
	g_window.adjustScale = 0.75f;
	VI.width = 320;
	VI.height = 240;
	dmaBase = gli_drawer_dma_vertices(0);
}

static void make_buffers(void)
{
	memset(buffers, 0, sizeof(buffers));
	buffers[0].fb.m_startAddress = 0x100000;
	buffers[0].fb.m_endAddress = 0x100000 + 320 * 240 * 2;
	buffers[0].fb.m_width = 320;
	buffers[0].fb.m_scale = 2.0f;
	buffers[0].fb.m_originX = 3;
	buffers[0].fb.m_originY = 5;
	buffers[0].fb.m_pDepthBuffer = &depth;
	buffers[0].fb.m_size = G_IM_SIZ_16b;
	buffers[1].fb.m_startAddress = 0x180000;
	buffers[1].fb.m_endAddress = 0x180000 + 320 * 240 * 2;
	buffers[1].fb.m_width = 320;
	current = &buffers[0];
}

/* nothing to set: only what a draw itself does */
static void quiet(void)
{
	gSP.changed = 0;
	gDP.changed = 0;
	gli_cmb.changed = 0;
	gli_tris.modify = 0;
	memset(enableSet, 0, sizeof(enableSet));
	trisDrawn = rectsDrawn = texParamsSet = 0;
	clipCalls = 0;
	memset(viewport, 0, sizeof(viewport));
	rectMode = -1;
	depthWrite = -1;
	clear_log();
}

static void add_triangles(u32 _n, s32 _max)
{
	u32 i;
	for (i = 0; i < _n; ++i)
		gli_tris.elements[i] = (u16)(i % (u32)(_max + 1));
	gli_tris.num = _n;
	gli_tris.maxElement = _max;
}

static void test_init(void)
{
	setup();
	strcpy(RSP.romname, "Paper Mario");
	gli_tris.num = 7;
	gli_tris.clip = 1;
	gli_tris.vertices[3].x = 9.0f;
	gli_tris.elements[5] = 9;
	config.frameBufferEmulation.N64DepthCompare = 0;
	clear_log();
	gli_drawer_init_data();
	check(has("cull=0 scissor=1") && enables[4] == 1 && depthWrite == 0 && depthCompare == GLI_COMPARE_ALWAYS && enables[2] == 1,
		"no culling, the scissor on, no depth write, compare always, the depth test on");
	check(offset[0] == -3.0f && offset[1] == -3.0f && enableSet[3] == 0, "its polygon offset");
	check(viewport[0] == 0 && viewport[1] == 30 && viewport[2] == 800 && viewport[3] == 600, "the window's viewport");
	check(clearColor[0] == 0.0f && clearColor[3] == 0.0f && has("clear swap"), "cleared and swapped");
	check(has("swap textures-init depth-init fb-init combiner-init tfh-init pp-init zlut-init noise-init palette-init fbinfo-reset texrect-init"),
		"the modules started in their order");
	check(gSP.changed == 0xFFFFFFFF && gDP.changed == 0xFFFFFFFF, "every state to set");
	check(gli_tris.num == 0 && gli_tris.clip == 0 && gli_tris.vertices[3].x == 0.0f && gli_tris.elements[5] == 0,
		"the triangle list emptied");
	check(!gli_drawer_is_drawing() && !gli_drawer_is_triangle(), "nothing drawn yet");

	config.frameBufferEmulation.N64DepthCompare = 1;
	memset(enableSet, 0, sizeof(enableSet));
	clear_log();
	gli_drawer_init_data();
	check(enables[2] == 0 && enables[3] == 0 && enableSet[3] == 1 && !has("polygon-offset"),
		"N64 depth compare: no depth test, no polygon offset");
	config.frameBufferEmulation.N64DepthCompare = 0;

	clear_log();
	tfhChanged = 0;
	gli_drawer_destroy_data();
	check(!strcmp(log_, "texrect-destroy palette-destroy zlut-destroy noise-destroy pp-destroy combiner-destroy fb-destroy depth-destroy textures-destroy "),
		"the modules stopped in their order");
	clear_log();
	tfhChanged = 1;
	gli_drawer_destroy_data();
	check(has("pp-destroy tfh-shutdown combiner-destroy"), "the texture filter shut down when its options changed");
	tfhChanged = 0;
}

static void test_triangles(void)
{
	make_buffers();
	config.frameBufferEmulation.enable = 1;

	/* none: nothing */
	quiet();
	gli_tris.num = 0;
	gli_tris.maxElement = 4;
	gli_tris.clip = 1;
	gli_drawer_draw_triangles();
	check(trisDrawn == 0 && gli_tris.maxElement == 0 && gli_tris.clip == 0 && !has("texrect-draw"), "no triangles: the list dropped");
	/* no buffer to draw into */
	quiet();
	add_triangles(6, 3);
	gli_tris.clip = 1;
	current = NULL;
	gli_drawer_draw_triangles();
	check(trisDrawn == 0 && gli_tris.num == 0 && gli_tris.maxElement == 0 && gli_tris.clip == 0, "no buffer: the list dropped");
	config.frameBufferEmulation.enable = 0;
	quiet();
	add_triangles(6, 3);
	gli_drawer_draw_triangles();
	check(trisDrawn == 1, "no frame buffer emulation: drawn without a buffer");
	current = &buffers[0];
	config.frameBufferEmulation.enable = 1;

	/* drawn */
	quiet();
	add_triangles(6, 3);
	gSP.geometryMode = G_SHADE | G_SHADING_SMOOTH;
	RSP.LLE = 0;
	changedMaxY = -1.0f;
	gli_drawer_draw_triangles();
	check(trisDrawn == 1 && tris[0].mode == GLI_DM_TRIANGLES && tris[0].elementsType == GLI_DT_UNSIGNED_BYTE
		&& tris[0].verticesCount == 4 && tris[0].elementsCount == 6 && tris[0].vertices == gli_tris.vertices
		&& tris[0].elements == gli_tris.elements && tris[0].combiner == &program && !tris[0].flatColors,
		"its elements drawn, as many vertices as the largest");
	check(!strncmp(log_, "texrect-draw triangles", 22), "the texrect drawer drawn out first, nothing to set");
	check(gli_drawer_is_triangle() && gli_drawer_is_drawing(), "drawing triangles");
	check(renderedCount == 6 && renderedElements == gli_tris.elements && changedMaxY == 17.0f, "what it drew handed on");
	check(gli_tris.num == 0 && gli_tris.maxElement == 0 && gli_tris.clip == 0, "the list emptied");
	config.frameBufferEmulation.enable = 0;
	quiet();
	add_triangles(3, 2);
	changedMaxY = -1.0f;
	current = NULL;
	gli_drawer_draw_triangles();
	check(!has("render") && changedMaxY == -1.0f, "no frame buffer emulation: nothing handed on");
	current = &buffers[0];
	config.frameBufferEmulation.enable = 1;

	/* software depth marks the depth image's buffer */
	config.frameBufferEmulation.copyDepthToRDRAM = cdSoftwareRender;
	gDP.depthImageAddress = 0x180010;
	gDP.otherMode.depthUpdate = 1;
	dirtied = 0;
	quiet();
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(dirtied == 1, "software depth: its buffer dirtied");
	gDP.otherMode.depthUpdate = 0;
	quiet();
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(dirtied == 1, "no depth update: not");
	gDP.otherMode.depthUpdate = 1;
	config.frameBufferEmulation.copyDepthToRDRAM = cdDisable;
	quiet();
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(dirtied == 1, "no software depth: not");
	config.frameBufferEmulation.copyDepthToRDRAM = cdSoftwareRender;
	gDP.depthImageAddress = 0x300000;
	quiet();
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(dirtied == 1, "no buffer at the depth image: not");
	config.frameBufferEmulation.copyDepthToRDRAM = cdDisable;

	/* flat colours */
	{
		static const u32 modes[5] = { G_SHADE | 0x200, 0x200, G_SHADE, G_LIGHTING, 0x200 };
		static const u32 lle[5] = { 0, 0, 0, 0, 1 };
		static const int flat[5] = { 0, 1, 1, 0, 0 };
		u32 i;
		for (i = 0; i < 5; ++i) {
			quiet();
			add_triangles(3, 2);
			gSP.geometryMode = modes[i];
			RSP.LLE = lle[i];
			gli_drawer_draw_triangles();
			check(tris[0].flatColors == flat[i], "flat colours without lighting, shading or smooth shading, not for LLE");
		}
		RSP.LLE = 0;
		gSP.geometryMode = G_SHADE | 0x200;
	}

	/* vertices in screen space */
	quiet();
	add_triangles(3, 2);
	gli_tris.modify = MODIFY_XY;
	gSP.changed = CHANGED_VIEWPORT;
	gli_drawer_draw_triangles();
	check(!has("combiner-update") && (gSP.changed & CHANGED_VIEWPORT) && gli_tris.modify == 0,
		"screen space: the viewport not set from the RSP's, the modify cleared");
	check(viewport[0] == 6 && viewport[1] == 10 && viewport[2] == 640 && viewport[3] == 480, "the screen coordinates viewport");
	buffers[0].fb.m_scale = 2.5f;
	buffers[0].fb.m_width = 641;
	quiet();
	add_triangles(3, 2);
	gli_tris.modify = MODIFY_XY;
	gli_drawer_draw_triangles();
	check(viewport[0] == 8 && viewport[1] == 13 && viewport[2] == 1603 && viewport[3] == 1200, "rounded, as tall as the VI's tallest for its width");
	buffers[0].fb.m_scale = 2.0f;
	buffers[0].fb.m_width = 320;
	quiet();
	current = NULL;
	config.frameBufferEmulation.enable = 0;
	add_triangles(3, 2);
	gli_tris.modify = 0x0F;
	gli_drawer_draw_triangles();
	check(viewport[0] == 0 && viewport[1] == 0 && viewport[2] == 800 && viewport[3] == 480, "the window's, at its scales");
	current = &buffers[0];
	config.frameBufferEmulation.enable = 1;

	/* clipped, a run at a time */
	{
		const u32 run = (ELEMBUFF_SIZE * 3) / GLI_TRI_CLIP_MAX / 3 * 3;
		quiet();
		gli_tris.num = ELEMBUFF_SIZE;
		gli_tris.maxElement = 3;
		gli_tris.clip = 1;
		clipOut = 3;
		gli_drawer_draw_triangles();
		check(clipCalls == (ELEMBUFF_SIZE + run - 1) / run && clipFirst[0] == 0 && clipCount[0] == run
			&& clipFirst[1] == run && clipCount[clipCalls - 1] == ELEMBUFF_SIZE - (clipCalls - 1) * run
			&& clipVertices[0] == gli_tris.vertices, "clipped a run at a time");
		check(trisDrawn == (int)clipCalls && tris[0].verticesCount == 3 && tris[0].mode == GLI_DM_TRIANGLES
			&& tris[0].elements == NULL && tris[0].combiner == &program, "each run drawn as its clipped vertices");
		check(renderedCount == ELEMBUFF_SIZE && gli_tris.clip == 0, "all of them handed on");
		quiet();
		gSP.geometryMode = 0x200;
		add_triangles(6, 2);
		gli_tris.clip = 1;
		gli_drawer_draw_triangles();
		check(tris[0].flatColors, "with its flat colours");
		gSP.geometryMode = G_SHADE | 0x200;
		quiet();
		add_triangles(6, 2);
		gli_tris.clip = 1;
		clipOut = 0;
		gli_drawer_draw_triangles();
		check(clipCalls == 1 && trisDrawn == 0, "nothing left of a run: not drawn");
		clipOut = 3;
	}
}

static void test_states(void)
{
	make_buffers();
	/* culling */
	{
		static const u32 modes[4] = { GLI_CULLMODE_FRONT, GLI_CULLMODE_FRONT, GLI_CULLMODE_BACK, GLI_CULLMODE_FRONT_AND_BACK };
		int m;
		for (m = 0; m < 4; ++m) {
			quiet();
			culled = 0xFFFF;
			cullMode = m;
			gSP.changed = CHANGED_GEOMETRYMODE;
			add_triangles(3, 2);
			gli_drawer_draw_triangles();
			check(enables[1] == (m != GLI_CULL_NONE) && (m == GLI_CULL_NONE ? culled == 0xFFFF : culled == modes[m])
				&& !(gSP.changed & CHANGED_GEOMETRYMODE), "culling as the geometry mode says");
		}
		cullMode = 0;
		quiet();
		gDP.changed = CHANGED_RENDERMODE;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(enableSet[1] == 0, "the geometry mode unchanged: culling left");
	}
	/* depth */
	{
		static const u32 compares[3] = { GLI_COMPARE_LESS, GLI_COMPARE_LEQUAL, GLI_COMPARE_ALWAYS };
		int c;
		for (c = 0; c < 3; ++c) {
			quiet();
			depthState.set = GLI_DS_COMPARE;
			depthState.compare = (u8)c;
			depthState.offset = (u8)(c & 1);
			gDP.changed = CHANGED_RENDERMODE;
			add_triangles(3, 2);
			gli_drawer_draw_triangles();
			check(depthCompare == compares[c] && enables[3] == (u32)(c & 1) && !has("depth-test=") && !has("depth-write="),
				"the depth compare and its offset");
		}
		quiet();
		depthState.set = GLI_DS_WRITE | GLI_DS_TEST | GLI_DS_CLAMP;
		depthState.write = 1;
		depthState.test = 0;
		depthState.clipping = 1;
		depthCompare = 0xFFFF;
		gDP.changed = CHANGED_RENDERMODE;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(depthWrite == 1 && enables[2] == 0 && clampMode == 0 && depthCompare == 0xFFFF && enableSet[3] == 0,
			"the depth write, test and clipping");
		depthState.clipping = 0;
		quiet();
		gDP.changed = CHANGED_RENDERMODE;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(clampMode == 2, "no clipping");
		{
			static const u8 sets[3] = { GLI_DS_WRITE, GLI_DS_TEST, GLI_DS_CLAMP };
			int s;
			for (s = 0; s < 3; ++s) {
				quiet();
				clampMode = 0xFFFF;
				depthState.set = sets[s];
				gDP.changed = CHANGED_RENDERMODE;
				add_triangles(3, 2);
				gli_drawer_draw_triangles();
				check((depthWrite != -1) == (s == 0) && has("depth-test=") == (s == 1) && (clampMode != 0xFFFF) == (s == 2),
					"each depth state set only when asked");
			}
		}
		depthState.set = 0;
	}
	/* scissor */
	quiet();
	gDP.changed = CHANGED_SCISSOR;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(scissor[0] == 1 && scissor[3] == 4 && !(gDP.changed & CHANGED_SCISSOR) && scissorArgs[0] == 2.0f
		&& scissorArgs[2] == 3.0f && scissorArgs[3] == 5.0f, "the scissor at the buffer's scale and origin");
	quiet();
	current = NULL;
	config.frameBufferEmulation.enable = 0;
	gDP.changed = CHANGED_SCISSOR;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(scissorArgs[0] == 2.5f && scissorArgs[1] == 2.0f && scissorArgs[2] == 0.0f && scissorArgs[4] == 0.75f,
		"no buffer: at the window's scales");
	current = &buffers[0];
	config.frameBufferEmulation.enable = 1;
	/* viewport */
	quiet();
	gSP.changed = CHANGED_VIEWPORT;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(viewport[0] == 5 && viewport[3] == 8 && viewportBuffer == 1 && viewportArgs[0] == 2.0f && viewportArgs[2] == 3.0f
		&& !(gSP.changed & CHANGED_VIEWPORT), "the RSP's viewport in the buffer");
	quiet();
	current = NULL;
	config.frameBufferEmulation.enable = 0;
	gSP.changed = CHANGED_VIEWPORT;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(viewportBuffer == 0 && viewportArgs[0] == 2.5f && viewportArgs[1] == 2.0f, "in the window");
	current = &buffers[0];
	config.frameBufferEmulation.enable = 1;
	/* textures */
	{
		static const u32 sp[4] = { CHANGED_TEXTURE, 0, 0, 0 };
		static const u32 dp[4] = { 0, CHANGED_TILE, CHANGED_TMEM, CHANGED_RENDERMODE };
		int i;
		for (i = 0; i < 4; ++i) {
			quiet();
			gSP.changed = sp[i];
			gDP.changed = dp[i];
			gli_cmb.changed = i == 3;
			add_triangles(3, 2);
			gli_drawer_draw_triangles();
			check(has("tex-update0 tex-dummy1") && !(gDP.changed & (CHANGED_TILE | CHANGED_TMEM)) && !(gSP.changed & CHANGED_TEXTURE),
				"the textures the program samples updated, the others dummies");
		}
		quiet();
		gDP.changed = CHANGED_RENDERMODE;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(!has("tex-update") && !has("tex-dummy"), "nothing of them changed: not");
		gli_cmb.current = NULL;
		quiet();
		gSP.changed = CHANGED_TEXTURE | CHANGED_GEOMETRYMODE;
		gDP.changed = CHANGED_TILE;
		gSP.texture.on = 1;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(!has("tex-update") && !(gDP.changed & CHANGED_TILE), "no program yet: only the flags cleared");
		gli_cmb.current = &program;
	}
	/* blending */
	quiet();
	gDP.changed = CHANGED_CYCLETYPE;
	blendOut.enable = 1;
	blendOut.src = GLI_BF_SRC_ALPHA;
	blendOut.dst = GLI_BF_ONE_MINUS_SRC_ALPHA;
	blendOut.fogColor = 1;
	gDP.fogColor.r = 0.25f;
	gDP.fogColor.a = 0.5f;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(blendLegacy == 0 && enables[0] == 1 && blendSrc == GLI_BLEND_SRC_ALPHA && blendDst == GLI_BLEND_ONE_MINUS_SRC_ALPHA
		&& blendColor[0] == 0.25f && blendColor[3] == 0.5f && !(gDP.changed & CHANGED_CYCLETYPE), "blending as the blender says");
	{
		static const u8 f[7] = { GLI_BF_ZERO, GLI_BF_ONE, GLI_BF_SRC_ALPHA, GLI_BF_ONE_MINUS_SRC_ALPHA, GLI_BF_DST_ALPHA,
			GLI_BF_CONSTANT_ALPHA, GLI_BF_ONE_MINUS_CONSTANT_ALPHA };
		static const u32 g[7] = { GLI_BLEND_ZERO, GLI_BLEND_ONE, GLI_BLEND_SRC_ALPHA, GLI_BLEND_ONE_MINUS_SRC_ALPHA,
			GLI_BLEND_DST_ALPHA, GLI_BLEND_CONSTANT_ALPHA, GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA };
		int i;
		for (i = 0; i < 7; ++i) {
			quiet();
			gDP.changed = CHANGED_RENDERMODE;
			blendOut.src = f[i];
			blendOut.dst = f[6 - i];
			blendOut.fogColor = 0;
			add_triangles(3, 2);
			gli_drawer_draw_triangles();
			check(blendSrc == g[i] && blendDst == g[6 - i] && !has("blend-color"), "each blend factor");
		}
	}
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	blendOut.enable = 0;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(enables[0] == 0 && !has("blending"), "no blending");
	blendOut.enable = 1;
	/* Blast Corps */
	{
		int i;
		for (i = 0; i < 3; ++i) {
			quiet();
			config.generalEmulation.hacks = i == 0 ? 0 : hack_blastCorps;
			gDP.otherMode.cycleType = i == 1 ? G_CYC_COPY : G_CYC_1CYCLE;
			gSP.texture.on = i == 2;
			gDP.changed = CHANGED_RENDERMODE;
			add_triangles(3, 2);
			gli_drawer_draw_triangles();
			check(blendBlast == 0, "Blast Corps' hack only for an untextured cycle that samples a texture");
		}
		quiet();
		gSP.texture.on = 0;
		gDP.changed = CHANGED_RENDERMODE;
		usesTexture = 0;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(blendBlast == 0, "nor one that samples none");
		usesTexture = 1;
		quiet();
		gDP.changed = CHANGED_RENDERMODE;
		add_triangles(3, 2);
		gli_drawer_draw_triangles();
		check(blendBlast == 1, "Blast Corps' hack");
		config.generalEmulation.hacks = 0;
	}
	/* the program's states */
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(has("combiner-update") && has("combiner-parameters") && rectMode == 0, "the program and its parameters, not in rect mode");

	/* drawing into the depth image */
	config.generalEmulation.enableFragmentDepthWrite = 1;
	config.frameBufferEmulation.N64DepthCompare = 0;
	depthImage = 1;
	gDP.otherMode.cycleType = G_CYC_1CYCLE;
	gDP.otherMode.depthCompare = 1;
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(has("depth-copy tex-params") && texParams[0].handle == depthCopy.name && texParams[0].textureUnitIndex == GLI_TU_DepthTex
		&& texParams[0].target == GLI_TT_TEXTURE_2D && texParams[0].maxMipmapLevel == 0
		&& texParams[0].minFilter == GLI_TP_FILTER_NEAREST && texParams[0].magFilter == GLI_TP_FILTER_NEAREST,
		"into the depth image with its compare: a copy of the depth, nearest");
	check(enables[2] == 1 && depthCompare == GLI_COMPARE_ALWAYS && depthWrite == 1 && (gDP.changed & CHANGED_RENDERMODE),
		"its depth written always, the render mode to set again");
	noDepthCopy = 1;
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	depthWrite = -1;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(depthWrite == -1 && !(gDP.changed & CHANGED_RENDERMODE), "no copy: left");
	noDepthCopy = 0;
	gDP.otherMode.depthCompare = 0;
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(!has("depth-copy") && !(blendSrc == GLI_BLEND_ZERO && blendDst == GLI_BLEND_ONE) && depthWrite == 1,
		"no compare: no copy, the colour as it was");
	current = NULL;
	config.frameBufferEmulation.enable = 0;
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	blendOut.enable = 0;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(enables[0] == 1 && blendSrc == GLI_BLEND_ZERO && blendDst == GLI_BLEND_ONE, "no buffer: the colour kept");
	blendOut.enable = 1;
	current = &buffers[0];
	config.frameBufferEmulation.enable = 1;
	gDP.otherMode.cycleType = G_CYC_COPY;
	gDP.otherMode.depthCompare = 1;
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	add_triangles(3, 2);
	gli_drawer_draw_triangles();
	check(!has("depth-copy"), "copy mode: no copy");
	{
		int i;
		for (i = 0; i < 3; ++i) {
			config.generalEmulation.enableFragmentDepthWrite = i != 0;
			depthImage = i != 1;
			config.frameBufferEmulation.N64DepthCompare = i == 2;
			quiet();
			gDP.changed = CHANGED_RENDERMODE;
			depthWrite = -1;
			add_triangles(3, 2);
			gli_drawer_draw_triangles();
			check(depthWrite == -1, "not into the depth image, no depth write or N64 depth compare: nothing");
		}
	}
	config.generalEmulation.enableFragmentDepthWrite = 0;
	config.frameBufferEmulation.N64DepthCompare = 0;
	depthImage = 0;
	gDP.otherMode.cycleType = G_CYC_1CYCLE;
	gDP.otherMode.depthCompare = 0;
}

static void test_dma(void)
{
	SPVertex *v;
	u32 i;
	make_buffers();
	config.frameBufferEmulation.enable = 1;
	quiet();
	gli_drawer_draw_dma_triangles(0);
	check(trisDrawn == 0 && !has("texrect-draw"), "no DMA triangles: nothing");
	current = NULL;
	gli_drawer_draw_dma_triangles(3);
	check(trisDrawn == 0, "no buffer: nothing");
	current = &buffers[0];
	v = gli_drawer_take_dma_vertices(6);
	check(v == dmaBase, "the first taken at the start");
	check(gli_drawer_take_dma_vertices(3) == dmaBase + 6, "the next after them");
	quiet();
	outsideAt = -1;
	gli_drawer_draw_dma_pending();
	check(trisDrawn == 1 && tris[0].verticesCount == 9 && tris[0].vertices == dmaBase && tris[0].mode == GLI_DM_TRIANGLES
		&& tris[0].elementsCount == 0 && renderedCount == 9 && renderedElements == NULL, "the pending ones drawn and handed on");
	check(gli_drawer_take_dma_vertices(1) == dmaBase, "taken from the start again");
	gli_drawer_draw_dma_pending();
	/* clipped */
	gli_drawer_take_dma_vertices(9);
	outsideAt = 7;
	quiet();
	gli_drawer_draw_dma_pending();
	check(clipCalls == 1 && clipFirst[0] == 0xFFFF && clipCount[0] == 9 && clipVertices[0] == dmaBase && trisDrawn == 1,
		"a vertex outside: clipped");
	outsideAt = 8;
	gli_drawer_take_dma_vertices(9);
	quiet();
	gli_drawer_draw_dma_pending();
	check(clipCalls == 1, "the last one outside: clipped");
	{
		const u32 run = (ELEMBUFF_SIZE * 3) / GLI_TRI_CLIP_MAX / 3 * 3;
		outsideAt = 0;
		gli_drawer_take_dma_vertices(run + 6);
		quiet();
		gli_drawer_draw_dma_pending();
		check(clipCalls == 2 && clipVertices[0] == dmaBase && clipVertices[1] == dmaBase + run && clipCount[1] == 6,
			"a run at a time, each from its own vertices");
	}
	outsideAt = -1;
	/* taken past the end: what is there drawn first */
	for (i = 0; i < 4096 / 6; ++i)
		gli_drawer_take_dma_vertices(6);
	quiet();
	v = gli_drawer_take_dma_vertices(6);
	check(trisDrawn == 1 && tris[0].verticesCount == 4096 / 6 * 6 && v == dmaBase, "past the end: drawn first, from the start");
	quiet();
	gli_drawer_take_dma_vertices(4096 - 12);
	check(trisDrawn == 0, "up to the end: not");
	quiet();
	gli_drawer_take_dma_vertices(6);
	check(trisDrawn == 0, "to the very end: not");
	gli_drawer_draw_dma_pending();

	/* screen space */
	quiet();
	gli_drawer_draw_screen_triangles(0);
	check(trisDrawn == 0, "no screen triangles: nothing");
	current = NULL;
	gli_drawer_draw_screen_triangles(3);
	check(trisDrawn == 0, "no buffer: nothing");
	current = &buffers[0];
	cullMode = 2;
	dmaBase[0].y = 3.0f;
	dmaBase[1].y = 9.0f;
	dmaBase[2].y = 5.0f;
	dmaBase[1].modify = 0;
	gSP.changed = CHANGED_GEOMETRYMODE;
	changedMaxY = -1.0f;
	gli_drawer_draw_screen_triangles(3);
	check(trisDrawn == 1 && tris[0].mode == GLI_DM_TRIANGLES && tris[0].verticesCount == 3 && tris[0].vertices == dmaBase
		&& dmaBase[1].modify == MODIFY_ALL && changedMaxY == 9.0f, "screen triangles: all of them modified, the lowest handed on");
	check(has("immersive-screen3") && has("cull=0 triangles") && !has("cull-face") && (gSP.changed & CHANGED_GEOMETRYMODE),
		"immersive mode's, the cull mode not set, no culling, the geometry mode to set again");
	cullMode = 0;
	check(viewport[2] == 640 && gli_tris.modify == 0, "the screen coordinates viewport");
	check(!has("render"), "not rendered in software");
	quiet();
	gli_drawer_take_dma_vertices(4);
	gli_drawer_draw_screen_strip(4);
	check(trisDrawn == 1 && tris[0].mode == GLI_DM_TRIANGLE_STRIP && gli_drawer_take_dma_vertices(1) == dmaBase, "a strip, the vertices given back");
	gli_drawer_draw_dma_pending();
	{
		f32 y;
		quiet();
		for (y = 0; y < 3; ++y)
			dmaBase[(int)y].y = -1.0f - y;
		changedMaxY = -1.0f;
		gli_drawer_draw_screen_triangles(3);
		check(changedMaxY == 0.0f, "none below the top: 0");
	}
}

static void test_lines_rects(void)
{
	make_buffers();
	config.frameBufferEmulation.enable = 1;
	gli_tris.vertices[1].x = 7.0f;
	gli_tris.vertices[2].x = 8.0f;
	gli_tris.vertices[1].modify = 0;
	quiet();
	config.frameBufferEmulation.nativeResFactor = 0;
	gli_drawer_draw_line(1, 2, 1.0f, 0);
	check(lineWidth == 2.5f && lineVertices[0].x == 7.0f && lineVertices[1].x == 8.0f && has("texrect-draw"),
		"a line at the window's scale, of its vertices");
	check(!gli_drawer_is_triangle() && gli_drawer_is_drawing(), "drawing a line");
	quiet();
	config.frameBufferEmulation.nativeResFactor = 3;
	gli_drawer_draw_line(1, 2, 1.0f, 0);
	check(lineWidth == 3.0f, "at the native factor");
	quiet();
	gli_drawer_draw_line(1, 2, 3.0f, 0);
	check(has("thick-line") && trisDrawn == 1 && tris[0].mode == GLI_DM_TRIANGLE_STRIP && tris[0].verticesCount == 4
		&& !has(" line"), "wider than the context draws: two triangles");
	config.frameBufferEmulation.nativeResFactor = 0;
	quiet();
	gli_drawer_draw_line(1, 2, 1.6f, 0);
	check(!has("thick-line"), "up to its width: a line");
	quiet();
	gli_tris.vertices[1].modify = MODIFY_XY;
	gSP.changed = CHANGED_VIEWPORT;
	gli_drawer_draw_line(1, 2, 1.0f, 0);
	check(!has("combiner-update") && viewport[2] == 640, "screen space: the screen coordinates viewport");
	gli_tris.vertices[1].modify = 0;
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	gli_drawer_draw_line(1, 2, 1.0f, 0);
	check(has("combiner-update") && rectMode == 0, "its states set");
	quiet();
	current = NULL;
	gli_drawer_draw_line(1, 2, 1.0f, 0);
	check(!has(" line") && has("texrect-draw"), "no buffer: no line");
	current = &buffers[0];

	/* rects */
	gli_drawer_drop_render_state();
	quiet();
	gSP.changed = CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT | CHANGED_TEXTURE;
	gli_drawer_draw_rect(10, 20, 30, 40);
	check(has("texrect-draw") && has("combiner-update") && rectMode == 1 && enables[1] == 0 && enableSet[1] == 1
		&& gli_drawer_is_drawing() && !gli_drawer_is_triangle(), "a rect: its states, in rect mode, no culling");
	check(rectsDrawn == 1 && rects.mode == GLI_DM_TRIANGLE_STRIP && !rects.texrect && rects.verticesCount == 4
		&& rects.vertices == gli_drawer_rect() && rects.combiner == &program, "drawn as a strip, not a texrect");
	check(has("scales-buffer") && viewport[2] == 640 && (gSP.changed & CHANGED_GEOMETRYMODE) && (gSP.changed & CHANGED_VIEWPORT),
		"at the buffer's scales, the geometry mode and viewport to set again");
	quiet();
	gSP.changed = CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
	gli_drawer_draw_rect(10, 20, 30, 40);
	check(!has("combiner-update"), "the geometry mode and viewport alone: no states set");
	fillOk = 0;
	quiet();
	gli_drawer_draw_rect(10, 20, 30, 40);
	check(rectsDrawn == 0 && !gli_drawer_is_drawing() && (gSP.changed & CHANGED_VIEWPORT), "nothing to fill: not drawn, nothing drawing");
	fillOk = 1;
	quiet();
	current = NULL;
	config.frameBufferEmulation.enable = 1;
	gli_drawer_draw_rect(10, 20, 30, 40);
	check(rectsDrawn == 0, "no buffer: no rect");
	current = &buffers[0];
}

static gli_texrect_params rect_params(int _cmd)
{
	gli_texrect_params p;
	memset(&p, 0, sizeof(p));
	p.ulx = 1.0f;
	p.lrx = 9.0f;
	p.texrectCmd = (u8)_cmd;
	return p;
}

static gli_tex tex0, tex1;

static void test_textured_rects(void)
{
	gli_texrect_params p;
	make_buffers();
	config.frameBufferEmulation.enable = 1;
	tex0.name = 61;
	tex1.name = 62;
	gli_texture_current[0] = &tex0;
	gli_texture_current[1] = &tex1;
	gSP.textureTile[0] = &gDP.tiles[0];
	gSP.textureTile[1] = &gDP.tiles[1];

	p = rect_params(1);
	quiet();
	gSP.changed = CHANGED_GEOMETRYMODE | CHANGED_TEXTURE;
	texrectEmpty = 0;
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("texrect-draw") && has("combiner-update") && rectMode == 1 && enableSet[1] == 1 && enables[1] == 0,
		"a texrect: its states, in rect mode, no culling");
	check(rectsDrawn == 1 && rects.mode == GLI_DM_TRIANGLE_STRIP && rects.texrect && rects.verticesCount == 4
		&& rects.combiner == &program, "drawn as a texrect strip");
	check(texrectTex[0] == &tex0 && texrectTex[1] == NULL, "the textures the program samples");
	check(viewport[2] == 640 && (gSP.changed & CHANGED_VIEWPORT) && (gSP.changed & CHANGED_GEOMETRYMODE),
		"in the buffer's screen coordinates, the states to set again");
	check(gli_drawer_is_drawing() && !gli_drawer_is_triangle(), "drawing a texrect");
	texrectEmpty = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("texrect-draw"), "nothing in the texrect drawer: not drawn out");
	quiet();
	gSP.changed = CHANGED_TEXTURE;
	p.texrectCmd = 0;
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("combiner-update") && rectsDrawn == 1, "not a texrect command: no states set");
	quiet();
	buffers[1].fb.m_scale = 1.0f;
	buffers[1].fb.m_width = 320;
	gli_drawer_draw_textured_rect(&p, &buffers[1]);
	check(viewport[2] == 320 && viewport[0] == 0, "into another buffer: its viewport");
	p.texrectCmd = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, NULL);
	check(has("scales-window") && viewport[2] == 640, "no buffer given: at the window's scales, the current buffer's viewport");
	quiet();
	current = NULL;
	gli_drawer_draw_textured_rect(&p, NULL);
	check(rectsDrawn == 0, "no buffer to draw into: not drawn");
	current = &buffers[0];

	/* going on in the texrect drawer */
	canContinue = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("combiner-update tex-update0 tex-dummy1 combiner-parameters") && rectMode == 1 && enableSet[1] == 0,
		"going on: the program and its textures alone");
	quiet();
	current = NULL;
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 1, "going on: drawn without a buffer check");
	current = &buffers[0];
	canContinue = 0;

	/* both tiles, and their clamps */
	usesTile[1] = 1;
	clampOut = 1 | (2 << 2);
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(texrectTex[1] == &tex1 && texParamsSet == 2 && texParams[0].handle == 61 && texParams[0].wrapS == GLI_TP_WRAP_CLAMP_TO_EDGE
		&& texParams[0].wrapT == GLI_PARAM_INVALID && texParams[0].textureUnitIndex == GLI_TU_TEX0 && texParams[0].target == GLI_TT_TEXTURE_2D
		&& texParams[1].handle == 62 && texParams[1].wrapT == GLI_TP_WRAP_CLAMP_TO_EDGE && texParams[1].wrapS == GLI_PARAM_INVALID
		&& texParams[1].textureUnitIndex == GLI_TU_TEX1, "each tile clamped as its vertices say");
	clampOut = 0;
	gSP.textureTile[1] = NULL;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(texrectTex[1] == NULL, "no tile: not sampled");
	gSP.textureTile[1] = &gDP.tiles[1];
	gli_texture_current[1] = NULL;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(texrectTex[1] == NULL, "no texture: not sampled");
	gli_texture_current[1] = &tex1;
	usesTile[1] = 0;

	/* copy mode: nearest */
	gDP.otherMode.cycleType = G_CYC_COPY;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(texParamsSet == 1 && texParams[0].handle == 61 && texParams[0].minFilter == GLI_TP_FILTER_NEAREST
		&& texParams[0].magFilter == GLI_TP_FILTER_NEAREST && texParams[0].textureUnitIndex == GLI_TU_TEX0
		&& texParams[0].target == GLI_TT_TEXTURE_2D, "copy mode: tile 0 nearest");
	tex0.frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(texParamsSet == 0, "but a multisampled one");
	tex0.frameBufferTexture = GLI_TEX_FB_NONE;
	gDP.otherMode.cycleType = G_CYC_1CYCLE;

	/* immersive mode */
	immersiveRect = 1;
	immersiveFull = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("immersive-clip1") && rectsDrawn == 1, "immersive: clipped");
	p.texrectCmd = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("immersive-clip"), "not a texrect command: not immersive");
	p.texrectCmd = 1;
	immersiveRect = 0;

	/* the texrect drawer */
	config.graphics2D.enableNativeResTexrects = 1;
	gDP.otherMode.textureFilter = 2;
	addRect = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("add-rect") && rectsDrawn == 0, "native res texrects: into the texrect drawer");
	addRect = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("add-rect") && rectsDrawn == 1, "not taken: drawn");
	addRect = 1;
	{
		int i;
		for (i = 0; i < 9; ++i) {
			config.graphics2D.enableNativeResTexrects = i != 0;
			gDP.otherMode.textureFilter = i == 1 ? 0 : 2;
			usesTexture = i != 2;
			buffers[0].fb.m_cfb = i == 3;
			gli_texture_current[0] = i == 4 ? NULL : &tex0;
			tex0.frameBufferTexture = i == 5 ? GLI_TEX_FB_ONE_SAMPLE : GLI_TEX_FB_NONE;
			tex0.bHDTexture = i == 6;
			tex1.frameBufferTexture = i == 7 ? GLI_TEX_FB_ONE_SAMPLE : GLI_TEX_FB_NONE;
			tex1.bHDTexture = i == 8;
			quiet();
			if (i != 4)
				gli_drawer_draw_textured_rect(&p, &buffers[0]);
			check(!has("add-rect"), "not without native res texrects, bilinear, a texture, or for a frame buffer's or HD one");
		}
		config.graphics2D.enableNativeResTexrects = 1;
		gDP.otherMode.textureFilter = 2;
		usesTexture = 1;
		buffers[0].fb.m_cfb = 0;
		gli_texture_current[0] = &tex0;
		tex0.frameBufferTexture = tex1.frameBufferTexture = GLI_TEX_FB_NONE;
		tex0.bHDTexture = tex1.bHDTexture = 0;
		gli_texture_current[1] = NULL;
		quiet();
		gli_drawer_draw_textured_rect(&p, NULL);
		check(has("add-rect"), "no second texture, no buffer: taken");
		gli_texture_current[1] = &tex1;
		immersiveRect = 1;
		quiet();
		gli_drawer_draw_textured_rect(&p, &buffers[0]);
		check(!has("add-rect"), "not an immersive one");
		immersiveRect = 0;
	}
	config.graphics2D.enableNativeResTexrects = 0;
	gli_drawer_set_background_mode(1);
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("add-bg-rect") && !has("add-rect") && rectsDrawn == 0, "background mode: a background rect");
	gli_drawer_set_background_mode(0);
	addRect = 0;

	/* the plain texrect call */
	corrected = 0;
	config.graphics2D.correctTexrectCoords = tcSmart;
	quiet();
	gli_drawer_textured_rect(1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 1.0f, 0, 0, 0);
	check(corrected == 1 && rectsDrawn == 1 && has("scales-buffer"), "a texrect command corrected, into the current buffer");
	quiet();
	gDP.changed = CHANGED_RENDERMODE;
	gli_drawer_textured_rect(1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 1.0f, 0, 0, 0);
	check(has("combiner-update"), "its states set");
	config.graphics2D.enableNativeResTexrects = 1;
	quiet();
	gli_drawer_textured_rect(1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 1.0f, 0, 0, 0);
	check(corrected == 2, "native res texrects: not corrected");
	config.graphics2D.enableNativeResTexrects = 0;
	config.graphics2D.correctTexrectCoords = tcDisable;
	quiet();
	gli_drawer_textured_rect(1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 1.0f, 0, 0, 0);
	check(corrected == 2, "no correction: not");
}

/* a ROM's name, at most its header's 20 characters */
static void special(const char *_rom)
{
	memset(RSP.romname, 0, sizeof(RSP.romname));
	strncpy(RSP.romname, _rom, sizeof(RSP.romname) - 1);
	gli_drawer_init_data();
}

static void test_specials(void)
{
	gli_texrect_params p = rect_params(1);
	make_buffers();
	config.frameBufferEmulation.enable = 1;

	/* the depth copy: Perfect Dark, Turok */
	special("perfect dark");
	depthCopyRect = 1;
	config.frameBufferEmulation.copyDepthToRDRAM = cdCopyFromVRam;
	g_window.buffersSwapCount = 4;
	rectDepthBufferCopyFrame = 0;
	copyDepths = 0;
	buffers[0].fb.m_cleared = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 0 && copyDepths == 1 && has("copy-depth repeat-load depth-row") && buffers[0].fb.m_cleared
		&& rectDepthBufferCopyFrame == 4 && (gSP.changed & CHANGED_VIEWPORT), "Perfect Dark: the depth copied once a frame, its row written");
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(copyDepths == 1 && has("repeat-load depth-row"), "not again in the frame");
	g_window.buffersSwapCount = 5;
	copyDepthOk = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(copyDepths == 2 && !has("depth-row") && rectsDrawn == 0, "not copied: nothing more");
	copyDepthOk = 1;
	config.frameBufferEmulation.copyDepthToRDRAM = cdSoftwareRender;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("copy-depth") && !has("repeat-load") && has("depth-row"), "software depth: its row only");
	config.frameBufferEmulation.copyDepthToRDRAM = cdDisable;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("depth-row") && rectsDrawn == 0, "no depth copy: dropped");
	config.frameBufferEmulation.copyDepthToRDRAM = cdSoftwareRender;
	current = NULL;
	quiet();
	gli_drawer_draw_textured_rect(&p, NULL);
	check(!has("depth-row") && rectsDrawn == 0, "no buffer: dropped");
	config.frameBufferEmulation.enable = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, NULL);
	check(!has("depth-row") && rectsDrawn == 0, "nor without frame buffer emulation");
	config.frameBufferEmulation.enable = 1;
	current = &buffers[0];
	depthCopyRect = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 1, "not a depth copy: drawn");
	p.texrectCmd = 0;
	depthCopyRect = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 1 && !has("depth-row"), "not a texrect command: no special");
	p.texrectCmd = 1;
	special("TUROK_DINOSAUR_HUNTE");
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("depth-row"), "Turok: the same");
	special("ZELDA");
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("depth-row") && rectsDrawn == 1, "another game: none");

	/* Conker: an 8-bit buffer drawn into itself */
	special("Conker BFD");
	gDP.tiles[0].frameBufferAddress = 0x100000;
	buffers[0].fb.m_size = G_IM_SIZ_8b;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 0 && !has("depth-row"), "Conker: an 8-bit buffer into itself dropped");
	buffers[0].fb.m_size = G_IM_SIZ_16b;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("depth-row"), "else as the depth copy");
	buffers[0].fb.m_size = G_IM_SIZ_8b;
	gDP.tiles[0].frameBufferAddress = 0x100010;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("depth-row"), "another buffer: as the depth copy");
	buffers[0].fb.m_size = G_IM_SIZ_16b;
	depthCopyRect = 0;

	special("YOSHI");
	bgCopy = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(!has("bg-copy"), "another Yoshi: none");
	/* Yoshi: the background copied on the CPU */
	special("Yoshi Story");
	bgCopy = 1;
	gDP.colorImage.address = 0x123400;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("bg-copy fb-remove") && removedAt == 0x123400 && rectsDrawn == 0, "Yoshi: copied, its buffer removed");
	bgCopy = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("bg-copy") && !has("fb-remove") && rectsDrawn == 1, "not copied: drawn");

	/* Paper Mario */
	special("Paper Mario");
	gDP.textureImage.address = 0x400;
	gDP.colorImage.address = 0x400;
	gDP.colorImage.width = 64;
	memset(rdram + 0x14d500, 0x5a, 4096);
	memset(rdram + 0x400, 0, 4096);
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rdram[0x400] == 0x5a && rdram[0x400 + 4095] == 0x5a && rdram[0x400 + 4096] == 0 && rectsDrawn == 0,
		"Paper Mario's darkness copied in RDRAM");
	gDP.colorImage.width = 32;
	gDP.textureImage.width = 64;
	gDP.tiles[0].frameBufferAddress = 5;
	gDP.tiles[0].textureMode = 3;
	updated = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(gDP.tiles[0].frameBufferAddress == 0 && gDP.tiles[0].textureMode == TEXTUREMODE_NORMAL && has("tex-update0 program-update")
		&& updated == 1 && rectsDrawn == 1, "its darkness texture: tile 0 normal, updated, drawn");
	gDP.textureImage.width = 32;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(updated == 1 && rectsDrawn == 1 && !has("palette-mod"), "another width: drawn");
	gDP.colorImage.address = 0x500;
	gDP.colorImage.width = 64;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 1, "into another image: drawn");
	gDP.textureImage.address = 0x800;
	paletteMod = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(has("palette-mod") && rectsDrawn == 0, "its palette modified");
	paletteMod = 0;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 1, "else drawn");
	special("MARIO STORY");
	paletteMod = 1;
	quiet();
	gli_drawer_draw_textured_rect(&p, &buffers[0]);
	check(rectsDrawn == 0, "Mario Story: the same");
	paletteMod = 0;

	/* shadow maps */
	{
		static const char *const roms[4] = { "Beetle Adventure Rac", "HSV ADVENTURE RACING", "Duck Dodgers", "Daffy Duck Starring" };
		int i;
		for (i = 0; i < 4; ++i) {
			special(roms[i]);
			gli_ctx.integerTextures = 1;
			gDP.textureImage.size = 2;
			gDP.depthImageAddress = 0x100000;
			gDP.colorImage.width = 320;
			gDP.textureImage.address = 0x100000 + 320 * 320 * 6 / 4 - 1;
			quiet();
			blendLegacy = -1;
			gli_drawer_draw_textured_rect(&p, &buffers[0]);
			check(has("depth-activate depth-fog") && blendLegacy == 1 && rectsDrawn == 1, "a shadow map: the depth fog, legacy blending");
		}
		gDP.textureImage.address = 0x100000 + 320 * 320 * 6 / 4;
		quiet();
		gli_drawer_draw_textured_rect(&p, &buffers[0]);
		check(!has("depth-fog") && rectsDrawn == 1, "past the depth image: not");
		gDP.textureImage.address = 0x0FFFFF;
		quiet();
		gli_drawer_draw_textured_rect(&p, &buffers[0]);
		check(!has("depth-fog"), "before it: not");
		gDP.textureImage.address = 0x100000;
		gDP.textureImage.size = 1;
		quiet();
		gli_drawer_draw_textured_rect(&p, &buffers[0]);
		check(!has("depth-fog"), "not 16 bits: not");
		gDP.textureImage.size = 2;
		gli_ctx.integerTextures = 0;
		quiet();
		gli_drawer_draw_textured_rect(&p, &buffers[0]);
		check(!has("depth-fog") && rectsDrawn == 0, "no integer textures: dropped");
		gli_ctx.integerTextures = 1;
		current = NULL;
		config.frameBufferEmulation.enable = 0;
		quiet();
		gli_drawer_draw_textured_rect(&p, NULL);
		check(!has("depth-fog") && rectsDrawn == 1, "no buffer: drawn");
		current = &buffers[0];
		config.frameBufferEmulation.enable = 1;
	}
	special("SUPER MARIO 64");
}

static void test_copies(void)
{
	gli_copy_params c;
	gli_blit_copy_params b;
	memset(&c, 0, sizeof(c));
	c.r.srcX1 = 100;
	c.r.dstWidth = 300;
	c.r.dstHeight = 200;
	c.tex[0] = &tex0;
	c.tex[1] = &tex1;
	c.combiner = &program;
	c.filter = GLI_TP_FILTER_LINEAR;
	tex0.frameBufferTexture = GLI_TEX_FB_NONE;
	tex1.frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	quiet();
	activated = 0;
	gli_drawer_copy(&c);
	check(copied.srcX1 == 100 && texParamsSet == 2 && texParams[0].handle == 61 && texParams[0].textureUnitIndex == GLI_TU_TEX0
		&& texParams[0].target == GLI_TT_TEXTURE_2D && texParams[0].minFilter == GLI_TP_FILTER_LINEAR
		&& texParams[0].magFilter == GLI_TP_FILTER_LINEAR && texParams[0].wrapS == GLI_TP_WRAP_CLAMP_TO_EDGE
		&& texParams[0].wrapT == GLI_TP_WRAP_CLAMP_TO_EDGE, "a copy: its texture filtered, clamped");
	check(texParams[1].handle == 62 && texParams[1].textureUnitIndex == GLI_TU_TEX1 && texParams[1].target == GLI_TT_TEXTURE_2D_MULTISAMPLE
		&& texParams[1].minFilter == GLI_PARAM_INVALID && texParams[1].wrapS == GLI_PARAM_INVALID, "a multisampled one as it is");
	check(viewport[0] == 0 && viewport[1] == 0 && viewport[2] == 300 && viewport[3] == 200, "the target's viewport");
	check(enableSet[1] == 1 && enables[1] == 0 && enables[0] == 0 && enables[2] == 0 && depthWrite == 0, "no culling, blending, depth");
	check(has("activate scissor=0 rects scissor=1") && activated == 1 && rectsDrawn == 1 && rects.combiner == &program
		&& rects.mode == GLI_DM_TRIANGLE_STRIP && rects.verticesCount == 4 && rects.texrect, "drawn with its program, no scissor");
	check((gSP.changed & (CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT)) == (CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT)
		&& (gDP.changed & (CHANGED_RENDERMODE | CHANGED_TILE | CHANGED_COMBINE)) == (CHANGED_RENDERMODE | CHANGED_TILE | CHANGED_COMBINE)
		&& gli_drawer_is_drawing() && !gli_drawer_is_triangle(), "the states to set again");
	c.tex[1] = NULL;
	quiet();
	gli_drawer_copy(&c);
	check(texParamsSet == 1, "no second texture: not set");
	c.tex[0] = NULL;
	c.tex[1] = &tex1;
	quiet();
	gli_drawer_copy(&c);
	check(texParamsSet == 1 && texParams[0].handle == 62 && texParams[0].textureUnitIndex == GLI_TU_TEX1, "the second alone");
	c.tex[0] = &tex0;
	c.tex[1] = NULL;

	/* blits */
	memset(&b, 0, sizeof(b));
	b.copy = c;
	b.copy.r.srcX0 = 1; b.copy.r.srcX1 = 2; b.copy.r.srcY0 = 3; b.copy.r.srcY1 = 4;
	b.copy.r.dstX0 = 5; b.copy.r.dstX1 = 6; b.copy.r.dstY0 = 7; b.copy.r.dstY1 = 8;
	b.readBuffer = 21;
	b.drawBuffer = 22;
	b.mask = GLI_BM_COLOR_BUFFER;
	quiet();
	gli_drawer_blit_or_copy(&b);
	check(blit.readBuffer == 21 && blit.drawBuffer == 22 && blit.srcX0 == 1 && blit.srcX1 == 2 && blit.srcY0 == 3 && blit.srcY1 == 4
		&& blit.dstX0 == 5 && blit.dstX1 == 6 && blit.dstY0 == 7 && blit.dstY1 == 8 && blit.mask == GLI_BM_COLOR_BUFFER
		&& blit.filter == GLI_TP_FILTER_LINEAR && rectsDrawn == 0, "blitted");
	b.copy.r.invertX = 1;
	quiet();
	gli_drawer_blit_or_copy(&b);
	check(blit.srcX0 == 2 && blit.srcX1 == 1 && blit.srcY0 == 3 && blit.dstX0 == 5, "inverted in x: its source swapped");
	b.copy.r.invertX = 0;
	b.copy.r.invertY = 1;
	quiet();
	gli_drawer_blit_or_copy(&b);
	check(blit.srcY0 == 4 && blit.srcY1 == 3 && blit.srcX0 == 1 && blit.dstY0 == 7, "in y");
	blitOk = 0;
	quiet();
	gli_drawer_blit_or_copy(&b);
	check(has("blit bind-fb bind-fb") && boundRead == 21 && boundDraw == 22 && rectsDrawn == 1 && copied.invertY == 1
		&& copied.srcY0 == 3, "no blit: drawn, its rect as it was");
	blitOk = 1;

	/* the texrect copy */
	{
		gli_copy_rect r;
		memset(&r, 0, sizeof(r));
		r.dstX1 = 77;
		r.dstWidth = 40;
		quiet();
		gli_drawer_copy_texrect(&r, &tex0, GLI_TP_FILTER_NEAREST);
		check(copied.dstX1 == 77 && rects.combiner == &copyProgram && texParamsSet == 1 && texParams[0].handle == 61
			&& texParams[0].minFilter == GLI_TP_FILTER_NEAREST && viewport[2] == 40, "the texrect copy: its program, one texture");
	}

	/* clears */
	{
		f32 rgba[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
		quiet();
		gli_drawer_clear_color(rgba);
		check(clearColor[0] == 0.1f && clearColor[3] == 0.4f, "cleared to its colour");
		gli_drawer_clear_color(NULL);
		check(clearColor[0] == 0.0f && clearColor[3] == 0.0f, "or to 0");
		quiet();
		gDP.otherMode.depthUpdate = 1;
		gli_drawer_clear_depth();
		check(has("depth-clear depth-write=1"), "the depth cleared, its write as the mode says");
		gDP.otherMode.depthUpdate = 0;
		quiet();
		gli_drawer_clear_depth();
		check(has("depth-write=0"), "no depth update");
		current = NULL;
		config.frameBufferEmulation.enable = 1;
		quiet();
		gli_drawer_clear_depth();
		check(!has("depth-clear"), "no buffer: not");
		current = &buffers[0];
	}
}

static void test_texrect_drawer_face(void)
{
	make_buffers();
	quiet();
	gli_drawer_texrect_states();
	check(has("combiner-update") && rectMode == 1 && has("tex-update0") && has("cull=0"), "the texrect drawer's states, no culling");
	quiet();
	gDP.otherMode.cycleType = G_CYC_1CYCLE;
	blendOut.enable = 1;
	blendOut.src = GLI_BF_ONE;
	gli_drawer_set_blend_mode();
	check(blendLegacy == 0 && blendSrc == GLI_BLEND_ONE, "its blending");
	quiet();
	depthState.set = GLI_DS_TEST;
	depthState.test = 1;
	gli_drawer_update_depth_compare();
	check(enables[2] == 1, "its depth");
	depthState.set = 0;
	quiet();
	gli_drawer_screen_viewport(&buffers[0]);
	check(viewport[0] == 6 && viewport[2] == 640 && (gSP.changed & CHANGED_VIEWPORT), "its viewport");
	quiet();
	gli_drawer_update_scissor(NULL);
	check(scissorArgs[0] == 2.5f && has("scissor"), "its scissor");
	check(gli_drawer_rect() != NULL && gli_drawer_vertices() == gli_tris.vertices && gli_drawer_dma_vertices(765) == dmaBase,
		"its vertices");
	gli_drawer_drop_render_state();
	check(!gli_drawer_is_drawing(), "its state dropped");
}

int main(void)
{
	test_init();
	test_triangles();
	test_states();
	test_dma();
	test_lines_rects();
	test_textured_rects();
	test_specials();
	test_copies();
	test_texrect_drawer_face();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("drawer: ok\n");
	return 0;
}
