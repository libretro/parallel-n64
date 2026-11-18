/* test_gliden64_framebuffer -- GLideN64's frame buffer list, overscan
 * buffer and what the VI shows (FrameBuffer.c), with the list's slots
 * (FrameBufferList.c), on stub modules and a stub graphics context.
 *
 * Started, nothing is current, the screen is bound, the overscan buffer
 * takes the window's offsets, scale and size (a frame buffer of its own
 * when it is on), the VI state is reset, and the copies to and from RDRAM
 * start with frame buffer emulation. A colour image: too wide ignored,
 * Resident Evil 2's 512 the VI's width, without emulation one screen
 * sized buffer; the stereo left eye captured; an auxiliary buffer copied
 * and dropped; the one before made readable, its end set, its RDRAM saved
 * when it was drawn and not copied, cleared or a CPU one, the buffers it
 * overlaps cut or dropped; the buffer at the address reused (a sub buffer,
 * truncated, removed, or redrawn, cleared to the fill colour in a new
 * size) or made, read from RDRAM, its depth buffer attached or saved, an
 * auxiliary one's depth cleared after a depth buffer, cleared in immersive
 * mode, stamped with the swap. With every slot in use the oldest buffer
 * but the current one goes. The list's lookups, the changed marks, RDRAM
 * fills, texture checks, syncs, the depth copy, VI resizes, the depth
 * attachment, the drawn buffer, the coordinate scales, the textures
 * activated. Rendered: the screen sized buffer, or the VI's buffer
 * through the post-processors, resolved, into the window or the overscan
 * buffer, with the next buffer and the left eye, the temporal upscaler's
 * whole buffer, the black bars removed, the depth cleared for RE2, the
 * scissor reset. The stereo eye and post-processing buffers made, copied
 * into, drawn and let go. Stopped, every buffer goes.
 *
 *   ./test_gliden64_framebuffer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FrameBuffer.h"
#include "gli_fb_rdram.h"
#include "gli_fb_list.h"
#include "gli_framebuffer.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "gli_combiner.h"
#include "gli_drawer.h"
#include "gli_vi_update.h"
#include "StereoFrames.h"
#include "PostProcessor.h"
#include "FrameBufferInfo.h"
#include "TexrectDrawer.h"
#include "DepthBuffer.h"
#include "DisplayWindow.h"
#include "Config.h"
#include "GBI.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "BufferCopy/ColorBufferToRDRAM.h"
#include "BufferCopy/DepthBufferToRDRAM.h"
#include "BufferCopy/RDRAMtoColorBuffer.h"
#include "Graphics/gli_context.h"
#include "Graphics/VulkanContext/vk_device.h"
#include "Graphics/VulkanContext/vk_ContextImpl.h"

static int failures;
static char log_[8192];

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		if (getenv("FB_TEST_LOG") != NULL)
			printf("  log: %s\n", log_);
		failures++;
	}
}

Config gliden64Config;
gDPInfo gDP;
gSPInfo gSP;
VIInfo VI;
N64Regs REG;
gli_window g_window;
gli_context gli_ctx;
gli_combiner_info gli_cmb;
bool g_stereoFramesCapturing;
uint32_t RemoveFBBlackBars;
static u32 viOrigin, viWidth;

static void say(const char *_s)
{
	if (strlen(log_) + strlen(_s) + 2 < sizeof(log_)) {
		strcat(log_, _s);
		strcat(log_, " ");
	}
}
static void sayn(const char *_s, u32 _n)
{
	char b[64];
	sprintf(b, "%s:%u", _s, _n);
	say(b);
}
static void clear_log(void) { log_[0] = '\0'; }
static int has(const char *_s) { return strstr(log_, _s) != NULL; }
/* @_a logged before @_b */
static int before(const char *_a, const char *_b)
{
	const char *a = strstr(log_, _a), *b = strstr(log_, _b);
	return a != NULL && b != NULL && a < b;
}
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

/* ---- textures ---- */

static gli_tex textures[512];
static int texIndex;
static gli_tex *lastAdded;
static u32 lastAddedTarget;
static const gli_tex *lastRemoved;
gli_tex *gli_textures_add_fb(u32 _target)
{
	gli_tex *t = &textures[texIndex++ & 511];
	memset(t, 0, sizeof(*t));
	t->name = 1000 + (u32)texIndex;
	t->width = 320;
	t->height = 240;
	t->realWidth = 640;
	t->realHeight = 480;
	lastAdded = t;
	lastAddedTarget = _target;
	say("tex-add");
	return t;
}
void gli_textures_remove_fb(gli_tex *_t) { lastRemoved = _t; say("tex-remove"); }
static u32 activatedUnit;
static const gli_tex *activated;
void gli_textures_activate(u32 _t, gli_tex *_tex) { activatedUnit = _t; activated = _tex; say("activate"); }

/* ---- a buffer's own sides ---- */

static u32 nextFbo = 1;
static int made, freed;
static const gli_fb *lastFreed;
static gli_tex fbTextures[512];
static int fbTexIndex;
void gli_fb_make(gli_fb *_fb)
{
	memset(_fb, 0, sizeof(*_fb));
	_fb->m_FBO = nextFbo++;
	_fb->m_pTexture = &fbTextures[fbTexIndex++ & 511];
	memset(_fb->m_pTexture, 0, sizeof(gli_tex));
	_fb->m_pTexture->realWidth = 640;
	_fb->m_pTexture->realHeight = 480;
	_fb->m_pTexture->width = 320;
	_fb->m_pTexture->height = 240;
	_fb->m_pTexture->name = 5000 + _fb->m_FBO;
	made++;
	say("make");
}
void gli_fb_free(gli_fb *_fb) { lastFreed = _fb; freed++; sayn("free", _fb->m_FBO); }
static const struct gli_fb_list *initList;
void gli_fb_init(gli_fb *_fb, struct gli_fb_list *_list, u32 _address, u16 _format, u16 _size, u16 _width, int _cfb)
{
	initList = _list;
	gli_fb_list_lend_copy(_list, _fb, 64);
	_fb->m_startAddress = _address;
	_fb->m_endAddress = _address + (((u32)_width << _size >> 1) * 240) - 1;
	_fb->m_width = _width;
	_fb->m_height = 240;
	_fb->m_size = _size;
	_fb->m_cfb = (u8)_cfb;
	_fb->m_scale = 2.0f;
	_fb->m_pTexture->format = _format;
	_fb->m_pTexture->size = _size;
	sayn("init", _address);
}
static int ended;
void gli_fb_update_end_address(gli_fb *_fb) { (void)_fb; ended++; say("end"); }
int gli_fb_is_auxiliary(const gli_fb *_fb) { return _fb->m_width != VI.width; }
static int overlapping;
int gli_fb_overlapping(const gli_fb *_a, const gli_fb *_b) { (void)_a; (void)_b; return overlapping; }
static int reuseResult = GLI_FB_REUSE_KEEP;
static u32 reuseSwap, reuseAddress, reuseWidth, reuseSize;
static f32 reuseScale;
int gli_fb_reuse(gli_fb *_fb, u32 _swapCount, u32 _address, u32 _width, u32 _size, f32 _scale)
{
	(void)_fb;
	reuseSwap = _swapCount;
	reuseAddress = _address;
	reuseWidth = _width;
	reuseSize = _size;
	reuseScale = _scale;
	say("reuse");
	return reuseResult;
}
static const gli_fb *saved;
static int savedCount;
void gli_fb_save_rdram(gli_fb *_fb) { saved = _fb; savedCount++; say("save"); }
static u32 validSwap;
static int validForce;
int gli_fb_is_valid(gli_fb *_fb, int _force, u32 _swapCount) { (void)_fb; validForce = _force; validSwap = _swapCount; return 1; }
static const gli_fb *resolvedFb;
static int resolvedForce = -1;
void gli_fb_resolve_texture(gli_fb *_fb, int _bForce) { resolvedFb = _fb; resolvedForce = _bForce; sayn("resolve", _fb->m_FBO); }
static gli_tex sampled;
static int noTexture, gotDrawnInto = -1;
static u32 gotUnit;
static const gli_fb *gotFb;
gli_tex *gli_fb_get_texture(gli_fb *_fb, u32 _t, int _drawnInto)
{
	gotFb = _fb; gotUnit = _t; gotDrawnInto = _drawnInto;
	say("get-texture");
	return noTexture ? NULL : &sampled;
}
gli_tex *gli_fb_get_texture_bg(gli_fb *_fb, u32 _t, int _drawnInto)
{
	gotFb = _fb; gotUnit = _t; gotDrawnInto = _drawnInto;
	say("get-texture-bg");
	return noTexture ? NULL : &sampled;
}
static s32 filled[4];
static const gli_fb *filledFb;
int gli_fb_rdram_fill_buffer(gli_fb *_fb, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	filledFb = _fb; filled[0] = _ulx; filled[1] = _uly; filled[2] = _lrx; filled[3] = _lry;
	say("fill");
	return 1;
}
void gli_fb_texture_reset(void) { say("tex-reset"); }
static int useRemove, useResult = 1;
static const gli_fb *usedFb, *usedCurrent;
static u32 usedArgs[4];
int gli_fb_texture_use(gli_fb *_fb, gli_fb *_current, u32 _address, u32 _width, u32 _bytes, u32 _swapCount, int *_remove)
{
	usedFb = _fb; usedCurrent = _current;
	usedArgs[0] = _address; usedArgs[1] = _width; usedArgs[2] = _bytes; usedArgs[3] = _swapCount;
	*_remove = useRemove;
	say("tex-use");
	return useResult;
}
void gli_fb_texture_tiles(void) { say("tex-tiles"); }
static gli_tex *attachedTex;
static u32 attachedFbo, attachedW, attachedH;
static f32 attachedScale;
static u16 attachedFmt, attachedSize;
void gli_fb_attach_texture(gli_tex *_t, u32 _fbo, u16 _w, u16 _h, f32 _scale, u16 _fmt, u16 _size)
{
	attachedTex = _t; attachedFbo = _fbo; attachedW = _w; attachedH = _h;
	attachedScale = _scale; attachedFmt = _fmt; attachedSize = _size;
	_t->width = (u16)(_w * _scale);
	_t->height = (u16)(_h * _scale);
	_t->realWidth = _t->width;
	_t->realHeight = _t->height;
	say("attach-tex");
}
u16 gli_VI_GetMaxBufferHeight(u16 _w) { return _w > 320 ? 480 : 240; }

/* ---- depth ---- */

static DepthBuffer depth;
static gli_tex depthTexture;
static DepthBuffer *currentDepth;
DepthBuffer *gli_depth_current_buffer(void) { return currentDepth; }
void gli_depth_init_image_texture(DepthBuffer *_d, const gli_fb *_b) { (void)_d; (void)_b; say("depth-image"); }
void gli_depth_init_texture(DepthBuffer *_d, const gli_fb *_b) { (void)_d; (void)_b; say("depth-texture"); }
static u32 depthAttachFbo, depthAttachTarget, depthBindFbo;
void gli_depth_set_attachment(DepthBuffer *_d, u32 _fbo, u32 _target) { (void)_d; depthAttachFbo = _fbo; depthAttachTarget = _target; say("depth-attach"); }
void gli_depth_bind_image_texture(DepthBuffer *_d, u32 _fbo) { (void)_d; depthBindFbo = _fbo; say("depth-bind-image"); }
static u32 depthSaved;
void gli_depth_save(u32 _address) { depthSaved = _address; say("depth-save"); }
void gli_depth_list_init(void) { say("depth-list-init"); }
void gli_depth_list_destroy(void) { say("depth-list-destroy"); }

/* ---- the copies ---- */

static u32 c2rAddress;
static int c2rSync;
void gli_c2r_init(void) { say("c2r-init"); }
void gli_c2r_destroy(void) { say("c2r-destroy"); }
void gli_c2r_copy_to_rdram(u32 _address, bool _sync) { c2rAddress = _address; c2rSync = _sync; sayn("c2r", _address); }
void gli_c2r_copy_chunk_to_rdram(u32 _address) { sayn("c2r-chunk", _address); }
static int d2rResult;
void gli_d2r_init(void) { say("d2r-init"); }
void gli_d2r_destroy(void) { say("d2r-destroy"); }
bool gli_d2r_copy_to_rdram(u32 _address) { sayn("d2r", _address); return d2rResult; }
bool gli_d2r_copy_chunk_to_rdram(u32 _address) { sayn("d2r-chunk", _address); return 1; }
static const struct FrameBuffer *r2cbBuffer;
static int r2cbAlpha;
void gli_r2cb_init(void) { say("r2cb-init"); }
void gli_r2cb_destroy(void) { say("r2cb-destroy"); }
void gli_r2cb_add_address(u32 _address, u32 _size) { sayn("r2cb-add", _address + _size); }
void gli_r2cb_copy_from_rdram(u32 _address, bool _cfb) { r2cbAlpha = _cfb; sayn("r2cb", _address); }
void gli_r2cb_copy_buffer_from_rdram(struct FrameBuffer *_p) { r2cbBuffer = _p; say("r2cb-buffer"); }

/* ---- the drawer ---- */

static gli_copy_params copies[4];
static int copiesMade;
void gli_drawer_copy(const gli_copy_params *_p) { copies[copiesMade++ & 3] = *_p; say("copy"); }
static gli_blit_copy_params blitCopy;
void gli_drawer_blit_or_copy(const gli_blit_copy_params *_p) { blitCopy = *_p; say("blit-or-copy"); }
static f32 clearedTo[4];
void gli_drawer_clear_color(const f32 *_c) { memcpy(clearedTo, _c, sizeof(clearedTo)); say("clear-color"); }
void gli_drawer_clear_depth(void) { say("clear-depth"); }
void gli_gDPGetFillColor(f32 _c[4]) { _c[0] = 0.25f; _c[1] = 0.5f; _c[2] = 0.75f; _c[3] = 1.0f; }
bool gli_texrect_draw(void) { say("texrect"); return 1; }

/* ---- post-processing, stereo, immersive ---- */

static struct FrameBuffer *ppResult;
static const struct FrameBuffer *ppInput[2];
static int ppRuns;
struct FrameBuffer *gli_pp_run(struct FrameBuffer *_p)
{
	ppInput[ppRuns++ & 1] = _p;
	say("pp");
	/* the post-processors draw into a buffer of their own */
	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, GLI_BT_DRAW_FRAMEBUFFER, 777);
	return ppResult != NULL ? ppResult : _p;
}
static int stereoEntered;
static struct FrameBuffer *leftEye;
static const struct FrameBuffer *captured, *leftEyeOf[2];
static int leftEyeAsks;
bool gli_StereoFrames_wasEntered(u32 _a) { (void)_a; return stereoEntered; }
void gli_StereoFrames_noteEntered(u32 _a) { sayn("stereo-entered", _a); }
void gli_StereoFrames_captureLeftEye(struct FrameBuffer *_p) { captured = _p; say("capture"); }
struct FrameBuffer *gli_StereoFrames_leftEye(const struct FrameBuffer *_p) { leftEyeOf[leftEyeAsks++ & 1] = _p; return leftEye; }
void gli_StereoFrames_destroy(void) { say("stereo-destroy"); }
void gli_gSPStereoEndFrame(void) { say("stereo-end"); }
static int immersiveClears, immersiveFlat = 1;
bool gli_gSPImmersiveClearsShownBuffers(void) { return immersiveClears; }
bool gli_gSPImmersiveIsFlat(void) { return immersiveFlat; }
static u32 presented;
void gli_gSPImmersivePresent(u32 _a) { presented = _a; say("present"); }
static int fbInfo;
bool gli_FBInfo_supported(void) { return fbInfo; }

/* ---- the VI ---- */

static int viStateInits, viOk = 1, presentOk = 1;
static gli_vi_result viResult;
static gli_vi_state *viState;
void gli_vi_state_init(gli_vi_state *_s) { viState = _s; viStateInits++; say("vi-init"); }
int gli_vi_update(gli_vi_state *_s, gli_vi_result *_r) { check(_s == viState, "the VI state carried"); *_r = viResult; say("vi"); return viOk; }
static u32 srcArgs[6];
static f32 srcScale;
static gli_vi_result srcResult;
void gli_vi_source(const gli_vi_result *_r, u32 _bufStart, u32 _bufWidth, u32 _bufHeight,
		u32 _bufSize, f32 _bufScale, u32 _texRealHeight, gli_vi_source_rect *_o)
{
	srcResult = *_r;
	srcArgs[0] = _bufStart; srcArgs[1] = _bufWidth; srcArgs[2] = _bufHeight;
	srcArgs[3] = _bufSize; srcArgs[4] = _texRealHeight;
	srcScale = _bufScale;
	memset(_o, 0, sizeof(*_o));
	_o->srcHeight = 240;
	_o->srcWidth = 7;
}
static gli_vi_present_in presentIn;
static gli_vi_source_rect presentSrc;
static gli_vi_present_out presentOut;
int gli_vi_present_rects(const gli_vi_result *_r, const gli_vi_source_rect *_s,
		const gli_vi_present_in *_in, gli_vi_present_out *_o)
{
	(void)_r;
	presentSrc = *_s;
	presentIn = *_in;
	*_o = presentOut;
	return presentOk;
}
static int swaps;
void gli_window_swap_buffers(void) { swaps++; say("swap"); }

/* ---- Vulkan ---- */

static int vulkan, upscaler;
int gliden64_vulkan_enabled(void) { return vulkan; }
bool gliden64_upscaler_active(void) { return upscaler; }
static unsigned shownArgs[4];
static float shownScale;
static int shownSrc[4], shownDst[4];
void gliden64_vulkan_shown(unsigned _address, unsigned _width, unsigned _size,
	unsigned _height, float _scale, const int *_src, const int *_dst)
{
	shownArgs[0] = _address; shownArgs[1] = _width; shownArgs[2] = _size; shownArgs[3] = _height;
	shownScale = _scale;
	memcpy(shownSrc, _src, sizeof(shownSrc));
	memcpy(shownDst, _dst, sizeof(shownDst));
	say("shown");
}

/* ---- the context ---- */

static u32 ctxFbo = 900;
static u32 boundTarget, boundName;
static u32 lastDeletedFbo;
static s32 scissor[4];
static gli_init_texture_params texInit;
static gli_tex_parameters texParams;
static gli_fb_render_target rtarget;
static gli_blit_params blit;
static int blitResult = 1, fbError;
static u32 c_create_fb(void *_i) { (void)_i; say("create-fb"); return ctxFbo++; }
static void c_delete_fb(void *_i, u32 _n) { (void)_i; lastDeletedFbo = _n; sayn("delete-fb", _n); }
static void c_bind(void *_i, u32 _t, u32 _n)
{
	char b[64];
	(void)_i;
	boundTarget = _t;
	boundName = _n;
	sprintf(b, "bind:%s:%u", _t == GLI_BT_DRAW_FRAMEBUFFER ? "draw" : _t == GLI_BT_READ_FRAMEBUFFER ? "read" :
		_t == GLI_BT_FRAMEBUFFER ? "both" : "?", _n);
	say(b);
}
static void c_clear(void *_i, f32 _r, f32 _g, f32 _b, f32 _a)
{
	(void)_i;
	check(_r == 0.0f && _g == 0.0f && _b == 0.0f && _a == 0.0f, "cleared to black");
	say("clear");
}
static bool c_fb_error(const void *_i) { (void)_i; return fbError; }
static void c_scissor(void *_i, s32 _x, s32 _y, s32 _w, s32 _h) { (void)_i; scissor[0] = _x; scissor[1] = _y; scissor[2] = _w; scissor[3] = _h; say("scissor"); }
static u32 c_convert(const void *_i, u32 _f) { (void)_i; return _f + 1; }
static void c_init_tex(void *_i, const gli_init_texture_params *_p) { (void)_i; texInit = *_p; say("init-tex"); }
static void c_tex_params(void *_i, const gli_tex_parameters *_p) { (void)_i; texParams = *_p; say("tex-params"); }
static void c_target(void *_i, const gli_fb_render_target *_p) { (void)_i; rtarget = *_p; say("render-target"); }
static bool c_blit(void *_i, const gli_blit_params *_p) { (void)_i; blit = *_p; say("blit"); return blitResult; }
static void c_reset_shader(void *_i) { (void)_i; say("reset-shader"); }
static gli_context_ops ops;

static struct gli_cprog *copyProgram = (struct gli_cprog *)&ops;

static void setup(void)
{
	memset(&ops, 0, sizeof(ops));
	ops.createFramebuffer = c_create_fb;
	ops.deleteFramebuffer = c_delete_fb;
	ops.bindFramebuffer = c_bind;
	ops.clearColorBuffer = c_clear;
	ops.isFramebufferError = c_fb_error;
	ops.setScissor = c_scissor;
	ops.convertInternalTextureFormat = c_convert;
	ops.init2DTexture = c_init_tex;
	ops.setTextureParameters = c_tex_params;
	ops.addFrameBufferRenderTarget = c_target;
	ops.blitFramebuffers = c_blit;
	ops.resetShaderProgram = c_reset_shader;
	gli_ctx.ops = &ops;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_cmb.texrectCopy = copyProgram;
	REG.VI_ORIGIN = &viOrigin;
	REG.VI_WIDTH = &viWidth;
	VI.width = 320;
	VI.height = 240;
	g_window.width = 640;
	g_window.height = 480;
	g_window.screenWidth = 800;
	g_window.screenHeight = 600;
	g_window.heightOffset = 10;
	g_window.scaleX = 2.0f;
	g_window.buffersSwapCount = 9;
	gDP.depthImageAddress = 0x700000;
	gDP.scissor.lry = 240.0f;
	depth.m_pDepthBufferTexture = &depthTexture;
}

/* frame buffer emulation on, started afresh */
static void start(void)
{
	gli_fb_emulation_destroy();
	memset(&config, 0, sizeof(config));
	config.frameBufferEmulation.enable = 1;
	reuseResult = GLI_FB_REUSE_KEEP;
	overlapping = 0;
	currentDepth = NULL;
	gli_fb_emulation_init();
	clear_log();
	made = freed = savedCount = ended = 0;
}

static struct FrameBuffer *colour(u32 _address, u32 _width)
{
	gli_fb_set_color_image(_address, G_IM_FMT_RGBA, G_IM_SIZ_16b, _width);
	return gli_fb_current();
}

static void test_init_destroy(void)
{
	memset(&config, 0, sizeof(config));
	config.frameBufferEmulation.enable = 1;
	config.frameBufferEmulation.enableOverscan = 0;
	clear_log();
	viStateInits = 0;
	gli_fb_emulation_init();
	check(gli_fb_current() == NULL && gli_fb_drawn() == NULL, "started: nothing current");
	check(has("bind:draw:0") && viStateInits == 1, "the screen bound, the VI state reset");
	check(has("c2r-init") && has("d2r-init") && has("r2cb-init") && before("c2r-init", "d2r-init") && before("d2r-init", "r2cb-init"),
		"the copies started with emulation, in order");
	check(!has("create-fb"), "no overscan frame buffer when it is off");
	/* started again without stopping: nothing current or copied from */
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	gli_fb_set_copy_buffer(gli_fb_current());
	gli_fb_emulation_init();
	check(gli_fb_current() == NULL, "started again: nothing current");
	clear_log();
	d2rResult = 0;
	gli_fb_copy_depth(0x10);
	check(!has("c2r"), "started again: no copy buffer");
	gli_fb_emulation_destroy();

	/* stopped: nothing current or copied from */
	gli_fb_emulation_init();
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	gli_fb_set_copy_buffer(gli_fb_current());
	gli_fb_emulation_destroy();
	check(gli_fb_current() == NULL, "stopped: nothing current");
	clear_log();
	d2rResult = 0;
	gli_fb_copy_depth(0x10);
	check(!has("c2r"), "stopped: no copy buffer");

	/* the RDRAM copies' storage let go when stopped */
	gli_fb_emulation_init();
	{
		const u8 *first;
		gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
		first = gli_fb_current()->fb.m_RdramCopy;
		gli_fb_emulation_destroy();
		gli_fb_emulation_init();
		gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
		check(first != NULL && gli_fb_current()->fb.m_RdramCopy != first, "stopped: the RDRAM copies let go");
		gli_fb_emulation_destroy();
	}

	config.frameBufferEmulation.enable = 0;
	config.frameBufferEmulation.enableOverscan = 1;
	clear_log();
	gli_fb_emulation_init();
	check(!has("c2r-init") && !has("d2r-init") && !has("r2cb-init"), "no copies without emulation");
	check(has("create-fb"), "an overscan frame buffer when it is on");

	/* made and let go */
	VI.height = 240;
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	clear_log();
	freed = 0;
	gli_fb_emulation_destroy();
	check(has("bind:both:0") && freed == 1 && has("stereo-destroy"), "stopped: the screen bound, every buffer let go, the stereo frames");
	check(has("r2cb-destroy") && before("r2cb-destroy", "d2r-destroy") && before("d2r-destroy", "c2r-destroy") && before("c2r-destroy", "free"),
		"the copies stopped before the list, in order");
	check(has("delete-fb:900") || lastDeletedFbo == 900, "the overscan frame buffer deleted");
	check(has("tex-remove") && lastRemoved == NULL, "its texture removed, none yet");
	check(gli_fb_current() == NULL && gli_fb_find(0x100000) == NULL, "nothing left");
	clear_log();
	gli_fb_emulation_destroy();
	check(has("delete-fb:0"), "deleted again: its frame buffer forgotten");
}

static void test_screen_size(void)
{
	struct FrameBuffer *p;
	start();
	config.frameBufferEmulation.enable = 0;
	VI.height = 0;
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(made == 0, "no screen sized buffer without a VI height");
	VI.height = 240;
	VI.width = 320;
	gli_fb_set_color_image(0x100000, G_IM_FMT_I, G_IM_SIZ_8b, 200);
	check(made == 1 && has("init:640"), "a screen sized buffer at twice the VI's width");
	p = gli_fb_drawn();
	check(p != NULL && p->fb.m_width == 320 && p->fb.m_size == G_IM_SIZ_16b && p->fb.m_pTexture->format == G_IM_FMT_RGBA
		&& !p->fb.m_cfb && initList != NULL, "the VI's width, RGBA 16 bit, not a CPU one, in the list");
	check(gli_fb_current() == NULL, "it is drawn into, not current");
	gli_fb_set_color_image(0x200000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(made == 1, "once only");
	gli_fb_set_current_draw_buffer();
	check(boundTarget == GLI_BT_DRAW_FRAMEBUFFER && boundName == p->fb.m_FBO, "it is bound to draw without a current one");
	VI.width = 320;
}

static void test_save_new(void)
{
	struct FrameBuffer *p;
	gli_fb *fb;
	start();
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 641);
	check(made == 0 && gli_fb_current() == NULL, "too wide: ignored");
	viWidth = 300;
	config.generalEmulation.hacks = hack_RE2;
	p = colour(0x100000, 512);
	check(p != NULL && p->fb.m_width == 300, "RE2's 512 is the VI's width");
	p = colour(0x400000, 320);
	check(p->fb.m_width == 320, "RE2's other widths kept");
	config.generalEmulation.hacks = 0;
	start();
	p = colour(0x100000, 512);
	check(p != NULL && p->fb.m_width == 512, "512 without the hack");

	start();
	g_window.buffersSwapCount = 33;
	gDP.depthImageAddress = 0x700000;
	gli_fb_set_color_image(0x100000, G_IM_FMT_I, G_IM_SIZ_8b, 320);
	p = gli_fb_current();
	fb = &p->fb;
	check(initList != NULL && fb->m_RdramCopy != NULL, "made in the list, which lends it its RDRAM copy");
	check(made == 1 && has("init:1048576") && fb->m_size == G_IM_SIZ_8b && fb->m_pTexture->format == G_IM_FMT_I,
		"made at the address in its format and size");
	check(r2cbBuffer == p && savedCount == 0, "read from RDRAM; not a CPU buffer: not saved");
	check(fb->m_swapCount == 33 && fb->m_isDepthBuffer == 0 && fb->m_copied == 0 && !fb->m_isPauseScreen && !fb->m_isOBScreen,
		"stamped with the swap, not a depth buffer");
	check(!has("depth-save") && !has("reuse"), "its depth buffer attached, not saved; nothing to reuse");
	check(gli_fb_find(0x100000) == p && gli_fb_find(0x100000 + 320 * 240 - 1) == p && gli_fb_find(0x100000 + 320 * 240) == NULL,
		"found in its range");
	g_window.buffersSwapCount = 9;

	clear_log();
	gli_fb_save_cpu_buffer(0x300000, G_IM_SIZ_32b, 320);
	p = gli_fb_current();
	check(p->fb.m_cfb == 1 && p->fb.m_size == G_IM_SIZ_32b && p->fb.m_pTexture->format == G_IM_FMT_RGBA, "a CPU buffer RGBA");
	check(saved == &p->fb && strstr(log_, "r2cb-buffer save") != NULL, "a CPU buffer saved after its read");

	clear_log();
	p = colour(0x700000, 320);
	check(has("depth-save") && depthSaved == 0x700000 && p->fb.m_isDepthBuffer == 1, "at the depth image: saved as one, a depth buffer");
}

static void test_previous(void)
{
	struct FrameBuffer *a, *b;
	start();
	a = colour(0x100000, 320);
	a->fb.m_height = 2;
	clear_log();
	ended = savedCount = 0;
	b = colour(0x200000, 320);
	check(b != a && a->fb.m_readable == 1 && ended == 1, "the one before readable, its end set");
	check(saved == &a->fb && savedCount == 1, "drawn, not copied: its RDRAM saved");
	check(before("end", "save") && before("save", "init:2097152"), "in order, before the new one");

	start();
	a = colour(0x100000, 320);
	a->fb.m_height = 1;
	savedCount = 0;
	colour(0x200000, 320);
	check(savedCount == 0, "one line high: not saved");
	start();
	a = colour(0x100000, 320);
	a->fb.m_height = 5;
	a->fb.m_copiedToRdram = 1;
	savedCount = 0;
	colour(0x200000, 320);
	check(savedCount == 0, "copied: not saved");
	start();
	a = colour(0x100000, 320);
	a->fb.m_height = 5;
	a->fb.m_cleared = 1;
	savedCount = 0;
	colour(0x200000, 320);
	check(savedCount == 0, "cleared: not saved");
	start();
	a = colour(0x100000, 320);
	a->fb.m_height = 5;
	a->fb.m_cfb = 1;
	savedCount = 0;
	colour(0x200000, 320);
	check(savedCount == 0, "a CPU one: not saved");
	start();
	a = colour(0x100000, 320);
	a->fb.m_height = 5;
	a->fb.m_RdramCopySize = 4;
	savedCount = 0;
	colour(0x200000, 320);
	check(savedCount == 0, "already saved: not again");
	start();
	a = colour(0x700000, 320);
	a->fb.m_height = 5;
	savedCount = 0;
	colour(0x200000, 320);
	check(savedCount == 0, "a depth buffer: not saved");

	/* the buffers it overlaps: one starting in it, one it starts in */
	start();
	a = colour(0x200000, 320);
	b = colour(0x200000 - 640 * 100, 320);
	gli_fb_stereo_sync();
	check(gli_fb_find(0x200000 + 640 * 200) == NULL && freed == 1 && lastFreed == &a->fb && gli_fb_current() == b,
		"one starting in it dropped");
	start();
	a = colour(0x200000, 320);
	overlapping = 1;
	b = colour(0x200000 - 640 * 100, 320);
	gli_fb_stereo_sync();
	check(gli_fb_find(0x200000) == a && b->fb.m_endAddress == 0x200000 - 1 && freed == 0, "overlapping enough: it is cut");
	/* truncated, it is made where the other is */
	start();
	a = colour(0x200000 - 640 * 100, 320);
	reuseResult = GLI_FB_REUSE_TRUNCATE;
	b = colour(0x200000, 320);
	reuseResult = GLI_FB_REUSE_KEEP;
	overlapping = 0;
	freed = 0;
	gli_fb_stereo_sync();
	check(freed == 1 && lastFreed == &a->fb && gli_fb_find(0x200000 - 640 * 100) == NULL, "one it starts in dropped");
	start();
	a = colour(0x200000 - 640 * 100, 320);
	reuseResult = GLI_FB_REUSE_TRUNCATE;
	b = colour(0x200000, 320);
	reuseResult = GLI_FB_REUSE_KEEP;
	freed = 0;
	overlapping = 1;
	gli_fb_stereo_sync();
	check(freed == 0 && a->fb.m_endAddress == 0x200000 - 1 && gli_fb_find(0x200000 - 1) == a, "overlapping enough: that one cut");
	/* by the next colour image too */
	start();
	a = colour(0x200000, 320);
	b = colour(0x200000 - 640 * 100, 320);
	freed = 0;
	overlapping = 0;
	colour(0x900000, 320);
	check(freed == 1 && lastFreed == &a->fb, "the next colour image drops what the one before overlaps");
	overlapping = 0;
}

static void test_aux_and_stereo(void)
{
	struct FrameBuffer *a;
	start();
	config.frameBufferEmulation.copyAuxToRDRAM = 1;
	a = colour(0x100000, 64);
	clear_log();
	colour(0x200000, 320);
	check(has("c2r:1048576") && c2rSync == 1 && gli_fb_find(0x100000) == NULL, "an auxiliary current one copied and dropped");
	check(before("c2r:1048576", "free") && has("bind:draw:0"), "copied, then dropped and the screen bound");
	(void)a;
	start();
	config.frameBufferEmulation.copyAuxToRDRAM = 1;
	config.generalEmulation.hacks = hack_Snap;
	colour(0x100000, 64);
	clear_log();
	colour(0x200000, 320);
	check(!has("c2r") && gli_fb_find(0x100000) != NULL, "not with Snap's hack");
	start();
	colour(0x100000, 64);
	clear_log();
	colour(0x200000, 320);
	check(!has("c2r"), "not without the option");
	start();
	config.frameBufferEmulation.copyAuxToRDRAM = 1;
	colour(0x100000, 320);
	clear_log();
	colour(0x200000, 320);
	check(!has("c2r"), "not a colour buffer");

	start();
	a = colour(0x100000, 320);
	g_stereoFramesCapturing = 1;
	stereoEntered = 1;
	captured = NULL;
	clear_log();
	colour(0x200000, 320);
	check(captured == a && before("capture", "bind:draw:") && boundName != 0, "the left eye captured, the buffer bound again");
	check(strstr(log_, "capture bind:draw:") != NULL, "bound straight after");
	stereoEntered = 0;
	captured = NULL;
	colour(0x300000, 320);
	check(captured == NULL, "not where it was not entered");
	stereoEntered = 1;
	g_stereoFramesCapturing = 0;
	colour(0x400000, 320);
	check(captured == NULL, "not when not capturing");
	g_stereoFramesCapturing = 1;
	colour(0x700000, 320);
	captured = NULL;
	colour(0x500000, 320);
	check(captured == NULL, "not a depth buffer");
	g_stereoFramesCapturing = 0;
	stereoEntered = 0;

	start();
	config.stereo.mode = stereoBothEyes;
	clear_log();
	colour(0x100000, 320);
	check(has("stereo-entered:1048576"), "both eyes: noted entered");
	config.stereo.mode = stereoOff;
	clear_log();
	colour(0x200000, 320);
	check(!has("stereo-entered"), "not without");
}

static void test_reuse(void)
{
	struct FrameBuffer *a, *p;
	start();
	a = colour(0x100000, 320);
	colour(0x200000, 320);
	reuseResult = GLI_FB_REUSE_SUB;
	gSP.changed = 0;
	gDP.changed = 0;
	made = 0;
	clear_log();
	config.frameBufferEmulation.nativeResFactor = 0;
	g_window.scaleX = 3.0f;
	g_window.buffersSwapCount = 12;
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(gli_fb_current() == a && made == 0 && reuseAddress == 0x100000 && reuseWidth == 320 && reuseSize == G_IM_SIZ_16b
		&& reuseScale == 3.0f && reuseSwap == 12, "found: asked how it is reused at the window's scale");
	check((gSP.changed & CHANGED_VIEWPORT) && (gDP.changed & CHANGED_SCISSOR) && !has("depth") && a->fb.m_swapCount != 12,
		"a sub buffer: the viewport and scissor, nothing more");
	config.frameBufferEmulation.nativeResFactor = 4;
	colour(0x200000, 320);
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(reuseScale == 4.0f, "the native factor's scale");
	config.frameBufferEmulation.nativeResFactor = 0;
	g_window.scaleX = 2.0f;

	reuseResult = GLI_FB_REUSE_TRUNCATE;
	colour(0x200000, 320);
	made = 0;
	p = colour(0x100000, 320);
	check(made == 1 && p != a, "truncated: made again");

	start();
	a = colour(0x100000, 320);
	colour(0x200000, 320);
	reuseResult = GLI_FB_REUSE_REMOVE;
	made = freed = 0;
	p = colour(0x100000, 320);
	check(freed == 1 && lastFreed == &a->fb && made == 1 && p->fb.m_startAddress == 0x100000, "removed: dropped and made again");

	start();
	a = colour(0x100000, 320);
	colour(0x200000, 320)->fb.m_cleared = 1;
	reuseResult = GLI_FB_REUSE_KEEP;
	a->fb.m_resolved = 1;
	clear_log();
	made = savedCount = 0;
	p = colour(0x100000, 320);
	check(p == a && made == 0 && a->fb.m_resolved == 0, "kept: not resolved");
	check(strstr(log_, "reuse bind:both:") != NULL && boundName == a->fb.m_FBO, "bound to draw and read");
	check(!has("clear-color") && savedCount == 0, "the same size: not cleared");
	a->fb.m_pResolveTexture = &textures[500];
	a->fb.m_copiedToRdram = 1;
	colour(0x200000, 320);
	clear_log();
	savedCount = 0;
	gli_fb_set_color_image(0x100000, G_IM_FMT_I, G_IM_SIZ_8b, 320);
	check(has("clear-color") && clearedTo[0] == 0.25f && clearedTo[3] == 1.0f, "another size: cleared to the fill colour");
	check(a->fb.m_size == G_IM_SIZ_8b && a->fb.m_pTexture->format == G_IM_FMT_I && a->fb.m_pTexture->size == G_IM_SIZ_8b
		&& textures[500].format == G_IM_FMT_I && textures[500].size == G_IM_SIZ_8b, "its size and its textures' formats");
	check(savedCount == 1 && saved == &a->fb && before("clear-color", "save"), "copied: saved again");
	a->fb.m_pResolveTexture = NULL;
	a->fb.m_copiedToRdram = 0;
	colour(0x200000, 320);
	savedCount = 0;
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(savedCount == 0 && a->fb.m_size == G_IM_SIZ_16b, "not copied: not saved");

	/* the current one at another address or width is looked up */
	start();
	a = colour(0x100000, 320);
	reuseAddress = 0;
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(reuseAddress == 0x100000 && gli_fb_current() == a, "the current one asked again");
	clear_log();
	gli_fb_set_color_image(0x100000 + 640, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(has("reuse") && gli_fb_current() == a, "inside it: found");
	/* two at one address: only the newest removed */
	start();
	a = colour(0x100000, 320);
	colour(0x900000, 320);
	reuseResult = GLI_FB_REUSE_TRUNCATE;
	p = colour(0x100000, 320);
	reuseResult = GLI_FB_REUSE_KEEP;
	check(p != a, "made again at the address");
	gli_fb_remove(0x100000);
	check(gli_fb_is_at(0x100000) && gli_fb_find(0x100000) == a, "only the newest removed");
	reuseResult = GLI_FB_REUSE_KEEP;
}

static void test_depth_clear_immersive(void)
{
	struct FrameBuffer *p;
	start();
	currentDepth = &depth;
	depthTexture.width = 640;
	gli_ctx.depthFramebufferTextures = 1;
	gli_ctx.weakBlitFramebuffer = 0;
	colour(0x700000, 320);
	clear_log();
	p = colour(0x100000, 64);
	check(p->fb.m_pDepthBuffer == &depth && has("clear-depth"), "an auxiliary one after a depth buffer: its depth cleared");
	colour(0x700000, 320);
	config.generalEmulation.hacks = hack_LoadDepthTextures;
	clear_log();
	colour(0x1100000, 64);
	check(!has("clear-depth"), "not when depth textures load");
	config.generalEmulation.hacks = 0;
	colour(0x200000, 320);
	clear_log();
	colour(0x1200000, 64);
	check(!has("clear-depth"), "not after a colour buffer");
	colour(0x700000, 320);
	clear_log();
	colour(0x1300000, 320);
	check(!has("clear-depth"), "not a colour buffer");
	currentDepth = NULL;
	colour(0x700000, 320);
	clear_log();
	colour(0x1400000, 64);
	check(!has("clear-depth"), "not without a depth buffer");

	start();
	immersiveClears = 1;
	g_window.buffersSwapCount = 20;
	clear_log();
	p = colour(0x100000, 320);
	check(strstr(log_, "bind:both:") != NULL && has("clear ") && before("bind:both", "clear "), "immersive: a new swap's buffer cleared");
	check(p->fb.m_swapCount == 20, "stamped");
	clear_log();
	colour(0x200000, 320);
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	check(count_of("clear ") == 1, "once a swap");
	g_window.buffersSwapCount = 21;
	clear_log();
	colour(0x150000, 64);
	check(!has("clear "), "not an auxiliary one");
	clear_log();
	colour(0x700000, 320);
	check(!has("clear "), "not a depth buffer");
	immersiveClears = 0;
	g_window.buffersSwapCount = 30;
	clear_log();
	colour(0x1500000, 320);
	check(!has("clear "), "not without immersive mode");
	g_window.buffersSwapCount = 9;

	/* the flags a buffer drawn into again starts with */
	start();
	p = colour(0x100000, 320);
	colour(0x200000, 320);
	p->fb.m_isPauseScreen = 1;
	p->fb.m_isOBScreen = 1;
	p->fb.m_copied = 1;
	colour(0x100000, 320);
	check(!p->fb.m_isPauseScreen && !p->fb.m_isOBScreen && !p->fb.m_copied, "not a pause or OB screen, not copied");
}

static void test_slots_full(void)
{
	u32 i;
	struct FrameBuffer *oldest, *first, *p;
	start();
	first = colour(0x10000, 320);
	gli_fb_set_copy_buffer(first);
	for (i = 1; i < GLI_FB_MAX; ++i)
		colour(0x10000 + i * 0x30000, 320);
	check(freed == 0 && made == GLI_FB_MAX, "every slot in use");
	oldest = gli_fb_find(0x10000);
	check(oldest == first, "the oldest still there");
	freed = 0;
	p = colour(0x10000 + GLI_FB_MAX * 0x30000, 320);
	check(freed == 1 && lastFreed == &first->fb && gli_fb_find(0x10000) == NULL, "past it the oldest goes");
	check(p == first, "its slot reused");
	/* the copy buffer forgotten: the depth copy goes on to the depth */
	clear_log();
	d2rResult = 0;
	gli_fb_copy_depth(0x1234);
	check(has("d2r:4660") && !has("c2r"), "the copy buffer forgotten with it");

}

static void test_lookups(void)
{
	struct FrameBuffer *a, *b;
	start();
	a = colour(0x100000, 320);
	b = colour(0x200000, 320);
	check(gli_fb_current() == b && gli_fb_drawn() == b, "current and drawn");
	check(gli_fb_is_at(0x100000) && !gli_fb_is_at(0x100002), "a buffer starts at an address");
	check(gli_fb_find_tmp(0x200010) == a && gli_fb_find_tmp(0x100010) == b, "another buffer than the one at an address");
	check(gli_fb_of(a) == &a->fb && gli_fb_of(NULL) == NULL && gli_fb_current_info() == &b->fb, "its plain state");
	check(gli_fb_start_address(a) == 0x100000 && gli_fb_width(a) == 320, "its address and width");
	a->fb.m_cfb = 1;
	check(gli_fb_is_cpu(a) && !gli_fb_is_cpu(b), "a CPU one");
	a->fb.m_isDepthBuffer = 0;
	gli_fb_set_is_depth(a);
	check(gli_fb_is_depth(a) && !gli_fb_is_depth(b), "a depth one");
	check(gli_fb_has_texture(a), "it has a texture");
	a->fb.m_cfb = 0;
	a->fb.m_copiedToRdram = 0;
	check(gli_fb_rendered_uncopied(a), "rendered, not copied");
	a->fb.m_copiedToRdram = 1;
	check(!gli_fb_rendered_uncopied(a), "copied");
	a->fb.m_copiedToRdram = 0;
	a->fb.m_cfb = 1;
	check(!gli_fb_rendered_uncopied(a), "a CPU one");
	a->fb.m_cfb = 0;
	g_window.buffersSwapCount = 44;
	check(gli_fb_is_valid_scanout(a) && validForce == 1 && validSwap == 44, "valid as scanned out: forced, this swap");
	g_window.buffersSwapCount = 9;
	savedCount = 0;
	gli_fb_copy_rdram(a);
	check(saved == &a->fb && savedCount == 1, "its RDRAM saved");
	gli_fb_resolve(a);
	check(resolvedFb == &a->fb && resolvedForce == 0, "resolved, not forced");
	check(!gli_fb_current_has_depth(), "no depth buffer");
	b->fb.m_pDepthBuffer = &depth;
	check(gli_fb_current_has_depth(), "a depth buffer");
	depth.m_width = 320;
	check(!gli_fb_depth_width_differs(0x200000, 320) && gli_fb_depth_width_differs(0x200000, 321)
		&& !gli_fb_depth_width_differs(0x100000, 321) && !gli_fb_depth_width_differs(0x900000, 321), "the depth width");
	gli_fb_set_current(a);
	check(gli_fb_current() == a, "set current");
	gli_fb_set_current(NULL);
	check(gli_fb_current_info() == NULL && !gli_fb_current_has_depth(), "none current");
	clear_log();
	gli_fb_make_current(0x200004);
	check(gli_fb_current() == b && boundTarget == GLI_BT_DRAW_FRAMEBUFFER && boundName == b->fb.m_FBO, "made current and bound");
	clear_log();
	gli_fb_make_current(0x900000);
	check(gli_fb_current() == b && !has("bind"), "none there: nothing");
	gli_fb_set_current_draw_buffer();
	check(boundName == b->fb.m_FBO, "the current one bound");
	gli_fb_set_current(NULL);
	clear_log();
	gli_fb_set_current_draw_buffer();
	check(boundName == a->fb.m_FBO, "else the oldest");
	gli_fb_bind_draw(b);
	check(boundTarget == GLI_BT_DRAW_FRAMEBUFFER && boundName == b->fb.m_FBO, "bound to draw");
	gli_fb_remove(0x100002);
	check(gli_fb_find(0x100000) == a, "removed only at its start");
	clear_log();
	gli_fb_remove(0x100000);
	check(gli_fb_find(0x100000) == NULL && lastFreed == &a->fb && !has("bind"), "removed; not current: nothing bound");
	gli_fb_set_current(b);
	clear_log();
	gli_fb_remove(0x200000);
	check(gli_fb_current() == NULL && has("bind:draw:0"), "the current one removed: forgotten, the screen bound");
	gli_fb_set_current_draw_buffer();
	check(1, "nothing to bind");
}

static void test_changed_fill(void)
{
	struct FrameBuffer *a;
	start();
	gli_fb_set_changed(10.0f);
	check(gDP.colorImage.changed == TRUE, "changed with none current");
	a = colour(0x100000, 320);
	a->fb.m_height = 5;
	a->fb.m_cfb = 1;
	a->fb.m_copiedToRdram = 1;
	a->fb.m_changed = 0;
	gDP.colorImage.height = 20;
	gDP.scissor.lry = 200.0f;
	gli_fb_set_changed(100.7f);
	check(gDP.colorImage.height == 100 && a->fb.m_height == 100, "the image as tall as drawn");
	check(a->fb.m_cfb == 0 && a->fb.m_changed == 1 && a->fb.m_copiedToRdram == 0, "drawn: not a CPU one, changed, not copied");
	gli_fb_set_changed(50.0f);
	check(gDP.colorImage.height == 100, "never shorter");
	gli_fb_set_changed(300.0f);
	check(gDP.colorImage.height == 200 && a->fb.m_height == 200, "at most the scissor");
	a->fb.m_height = 230;
	gli_fb_set_changed(10.0f);
	check(a->fb.m_height == 230, "the buffer never shorter");

	viOrigin = 0x100010;
	gli_fb_clear_changed();
	check(gDP.colorImage.changed == FALSE && a->fb.m_changed == 0, "cleared, the shown buffer too");
	viOrigin = 0x900000;
	gli_fb_clear_changed();
	check(1, "none shown");

	clear_log();
	gli_fb_fill_rdram(1, 2, 3, 4);
	check(filledFb == &a->fb && filled[0] == 1 && filled[1] == 2 && filled[2] == 3 && filled[3] == 4, "filled in RDRAM");
	config.frameBufferEmulation.copyFromRDRAM = 1;
	clear_log();
	gli_fb_fill_rdram(1, 2, 3, 4);
	check(!has("fill"), "not a colour buffer copied from RDRAM");
	a->fb.m_isDepthBuffer = 1;
	gli_fb_fill_rdram(1, 2, 3, 4);
	check(has("fill"), "a depth one is");
	gli_fb_set_current(NULL);
	clear_log();
	gli_fb_fill_rdram(1, 2, 3, 4);
	check(!has("fill"), "none current");
}

static void test_check_texture(void)
{
	struct FrameBuffer *a, *b;
	start();
	config.frameBufferEmulation.enable = 0;
	clear_log();
	check(!gli_fb_check_texture(0x100000, 320, 2) && has("tex-reset") && !has("tex-tiles"), "without emulation: reset, no more");
	config.frameBufferEmulation.enable = 1;
	a = colour(0x100000, 320);
	b = colour(0x200000, 320);
	clear_log();
	check(!gli_fb_check_texture(0x200000, 320, 2) && !has("tex-use") && has("tex-tiles"), "not readable: not used");
	a->fb.m_readable = 1;
	clear_log();
	useResult = 1;
	useRemove = 0;
	g_window.buffersSwapCount = 15;
	check(gli_fb_check_texture(0x100010, 64, 2), "used");
	check(usedFb == &a->fb && usedCurrent == &b->fb && usedArgs[0] == 0x100010 && usedArgs[1] == 64 && usedArgs[2] == 2
		&& usedArgs[3] == 15, "with the current one and the swap");
	check(before("tex-reset", "tex-use") && before("tex-use", "tex-tiles"), "in order");
	useResult = 0;
	check(!gli_fb_check_texture(0x100010, 64, 2), "not used");
	useRemove = 1;
	useResult = 1;
	freed = 0;
	check(gli_fb_check_texture(0x100010, 64, 2) && freed == 1 && gli_fb_find(0x100000) == NULL, "removed when it says");
	gli_fb_set_current(NULL);
	b->fb.m_readable = 1;
	useRemove = 0;
	gli_fb_check_texture(0x200000, 64, 2);
	check(usedCurrent == NULL, "none current");
	clear_log();
	check(!gli_fb_check_texture(0x900000, 64, 2) && !has("tex-use"), "none there");
	g_window.buffersSwapCount = 9;
}

static void test_sync(void)
{
	struct FrameBuffer *a;
	start();
	colour(0x100000, 64);
	a = colour(0x200000, 320);
	colour(0x300000, 32);
	gli_fb_set_current(a);
	config.frameBufferEmulation.copyAuxToRDRAM = 1;
	gDP.colorImage.address = 0x200000;
	clear_log();
	ended = 0;
	gli_fb_full_sync(0);
	check(has("c2r:1048576") && has("c2r:3145728") && gli_fb_find(0x100000) == NULL && gli_fb_find(0x300000) == NULL,
		"full sync: the auxiliary ones copied and dropped");
	check(c2rSync == 1, "synchronously");
	check(gli_fb_current() == a && before("c2r:3145728", "free") && before("free", "texrect") && before("texrect", "end") && ended == 1,
		"then the texrects drawn and the end set");
	check(!has("c2r:2097152"), "no copy to RDRAM without the option");
	config.frameBufferEmulation.copyAuxToRDRAM = 0;
	config.frameBufferEmulation.copyToRDRAM = ctSync;
	clear_log();
	gli_fb_full_sync(0);
	check(has("c2r:2097152") && c2rSync == 1 && before("end", "c2r:2097152"), "copied to RDRAM, synced");
	config.frameBufferEmulation.copyToRDRAM = ctSync + 1;
	gli_fb_full_sync(0);
	check(c2rSync == 0, "asynchronously");
	config.frameBufferEmulation.copyToRDRAM = ctDisable;
	config.generalEmulation.hacks = hack_subscreen;
	clear_log();
	gli_fb_full_sync(0);
	check(has("c2r:2097152"), "copied for the subscreen hack");
	fbInfo = 1;
	clear_log();
	gli_fb_full_sync(0);
	check(!has("c2r"), "not with frame buffer info");
	fbInfo = 0;
	a->fb.m_width = 64;
	clear_log();
	gli_fb_full_sync(0);
	check(!has("c2r"), "not an auxiliary one");
	a->fb.m_width = 320;
	gli_fb_set_current(NULL);
	clear_log();
	gli_fb_full_sync(0);
	check(!has("c2r") && !has("end"), "none current");
	config.generalEmulation.hacks = 0;

	config.frameBufferEmulation.copyDepthToRDRAM = cdDisable + 1;
	d2rResult = 0;
	clear_log();
	gli_fb_full_sync(0);
	check(!has("d2r"), "no depth copy for HLE");
	gli_fb_full_sync(1);
	check(has("d2r:2097152"), "LLE: the depth copied");
	fbInfo = 1;
	clear_log();
	gli_fb_full_sync(1);
	check(!has("d2r"), "not with frame buffer info");
	fbInfo = 0;
	config.frameBufferEmulation.copyDepthToRDRAM = cdDisable;
	clear_log();
	gli_fb_full_sync(1);
	check(!has("d2r"), "not without the option");

	gli_fb_set_current(a);
	clear_log();
	ended = 0;
	gli_fb_stereo_sync();
	check(before("texrect", "end") && ended == 1, "stereo sync: texrects, the end");
	gli_fb_set_current(NULL);
	ended = 0;
	gli_fb_stereo_sync();
	check(ended == 0, "none current");
}

static void test_depth_copy(void)
{
	struct FrameBuffer *a, *d;
	start();
	a = colour(0x100000, 320);
	a->fb.m_RdramCopySize = 9;
	gli_fb_set_copy_buffer(a);
	clear_log();
	check(gli_fb_copy_depth(0x555) && has("c2r:1048576") && c2rSync == 1 && !has("d2r"), "the copy buffer copied instead");
	check(a->fb.m_RdramCopySize == 0, "its validity check by RDRAM off");
	clear_log();
	d2rResult = 0;
	check(!gli_fb_copy_depth(0x555) && has("d2r:1365") && !has("c2r"), "once: then the depth, not copied");
	d = colour(0x700000, 320);
	colour(0x100000, 320);
	d2rResult = 1;
	savedCount = 0;
	gDP.depthImageAddress = 0x700000;
	check(gli_fb_copy_depth(0x555) && savedCount == 1 && saved == &d->fb, "copied: the depth buffer's RDRAM saved");
	gDP.depthImageAddress = 0x7f0000;
	savedCount = 0;
	check(gli_fb_copy_depth(0x555) && savedCount == 0, "none at the depth image");
	gDP.depthImageAddress = 0x700000;
	d2rResult = 0;
	clear_log();
	gli_fb_copy_depth_chunk(0x66);
	gli_fb_copy_chunk(0x77);
	gli_fb_copy_to_rdram(0x88, 1);
	gli_fb_copy_from_rdram(0x99, 1);
	gli_fb_add_address(0x10, 0x20);
	check(has("d2r-chunk:102") && has("c2r-chunk:119") && has("c2r:136") && c2rSync == 1 && has("r2cb:153") && r2cbAlpha == 1
		&& has("r2cb-add:48"), "the copies' ways through");
	gli_fb_copy_to_rdram(0x88, 0);
	gli_fb_copy_from_rdram(0x99, 0);
	check(c2rSync == 0 && r2cbAlpha == 0, "their flags");
}

static void test_vi_resized(void)
{
	struct FrameBuffer *a;
	start();
	a = colour(0x100000, 320);
	colour(0x200000, 640);
	colour(0x300000, 200);
	clear_log();
	gli_fb_vi_resized(320, 640);
	check(gli_fb_find(0x100000) == NULL && gli_fb_find(0x200000) == NULL && gli_fb_find(0x300000) != NULL, "both widths dropped");
	check(gli_fb_current() == NULL && before("free", "depth-list-destroy") && before("depth-list-destroy", "depth-list-init"),
		"nothing current; the depth buffers made again");
	(void)a;
}

static void test_attach_depth(void)
{
	struct FrameBuffer *a, *b;
	start();
	gli_fb_attach_depth_buffer();
	check(1, "none current");
	a = colour(0x100000, 320);
	a->fb.m_pDepthBuffer = &depth;
	currentDepth = NULL;
	clear_log();
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == NULL && !has("depth-image"), "no depth buffer: none attached");
	currentDepth = &depth;
	a->fb.m_FBO = 0;
	a->fb.m_pDepthBuffer = &depth;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == NULL, "no frame buffer: none attached");
	a->fb.m_FBO = 77;
	gli_ctx.depthFramebufferTextures = 1;
	gli_ctx.weakBlitFramebuffer = 1;
	depthTexture.width = 640;
	a->fb.m_pTexture->realWidth = 640;
	clear_log();
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == &depth && depthAttachFbo == 77 && depthAttachTarget == GLI_BT_DRAW_FRAMEBUFFER,
		"weak blits: attached as wide");
	check(before("depth-image", "depth-texture") && before("depth-texture", "depth-attach") && !has("depth-bind-image"),
		"its textures made, then attached; no image bound");
	depthTexture.width = 650;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == NULL, "weak blits: wider not");
	gli_ctx.weakBlitFramebuffer = 0;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == &depth, "wider is");
	depthTexture.width = 630;
	depth.m_width = 318;
	a->fb.m_width = 320;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == NULL, "narrower, two apart, not");
	depth.m_width = 319;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == &depth, "one apart is");
	depth.m_width = 321;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == &depth, "one apart the other way");
	depth.m_width = 322;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == NULL, "two apart the other way not");
	depthTexture.width = 640;
	gli_ctx.depthFramebufferTextures = 0;
	depth.m_depthRenderbufferWidth = 640;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == &depth, "a renderbuffer as wide");
	depth.m_depthRenderbufferWidth = 641;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == NULL, "a renderbuffer not as wide");
	depth.m_depthRenderbufferWidth = 640;
	config.frameBufferEmulation.N64DepthCompare = 1;
	clear_log();
	gli_fb_attach_depth_buffer();
	check(has("depth-bind-image") && depthBindFbo == 77 && before("depth-attach", "depth-bind-image"), "N64 depth compare: its image bound");
	config.frameBufferEmulation.N64DepthCompare = 0;
	fbError = 1;
	fbError = 0;

	/* without emulation, the oldest */
	b = colour(0x200000, 320);
	b->fb.m_pTexture->realWidth = 640;
	config.frameBufferEmulation.enable = 0;
	a->fb.m_pDepthBuffer = NULL;
	b->fb.m_pDepthBuffer = NULL;
	gli_fb_attach_depth_buffer();
	check(a->fb.m_pDepthBuffer == &depth && b->fb.m_pDepthBuffer == NULL, "without emulation the oldest");
	config.frameBufferEmulation.enable = 1;
	b->fb.m_pDepthBuffer = &depth;
	gli_fb_clear_depth_buffer(&depth);
	check(a->fb.m_pDepthBuffer == NULL && b->fb.m_pDepthBuffer == NULL, "the depth buffer forgotten by all");
	a->fb.m_pDepthBuffer = &depth;
	b->fb.m_pDepthBuffer = (DepthBuffer *)&depthTexture;
	gli_fb_clear_depth_buffer(&depth);
	check(a->fb.m_pDepthBuffer == NULL && b->fb.m_pDepthBuffer == (DepthBuffer *)&depthTexture, "only that one");
	start();
	config.frameBufferEmulation.enable = 0;
	gli_fb_attach_depth_buffer();
	check(1, "without emulation none there");
	gli_ctx.depthFramebufferTextures = 0;
	gli_ctx.weakBlitFramebuffer = 0;
}

static void test_drawn_scales(void)
{
	struct FrameBuffer *a, *b;
	f32 x, y;
	start();
	check(gli_fb_drawn() == NULL, "none");
	a = colour(0x100000, 320);
	b = colour(0x200000, 640);
	gli_fb_set_current(NULL);
	check(gli_fb_drawn() == NULL, "with emulation none current: none");
	config.frameBufferEmulation.enable = 0;
	check(gli_fb_drawn() == a, "without, the oldest");
	gli_fb_set_current(b);
	check(gli_fb_drawn() == b, "the current one");
	config.frameBufferEmulation.enable = 1;
	gli_fb_coords_scales(b, &x, &y);
	check(x == 1.0f / 640.0f && y == 1.0f / 480.0f, "its scales");
	VI.width = 200;
	gli_fb_coords_scales(NULL, &x, &y);
	check(x == 1.0f / 200.0f && y == 1.0f / 240.0f, "the VI's without one");
	VI.width = 320;
}

static void test_activate(void)
{
	struct FrameBuffer *a, *b;
	start();
	a = colour(0x100000, 320);
	b = colour(0x200000, 320);
	gDP.changed = 0;
	clear_log();
	noTexture = 0;
	gli_fb_activate_texture(1, 0x100000);
	check(gotFb == &a->fb && gotUnit == 1 && gotDrawnInto == 0 && activated == &sampled && activatedUnit == 1
		&& (gDP.changed & CHANGED_FB_TEXTURE), "its texture activated");
	gli_fb_activate_texture(0, 0x200000);
	check(gotDrawnInto == 1 && activatedUnit == 0, "drawn into: the current one");
	gDP.changed = 0;
	clear_log();
	gli_fb_activate_texture(0, 0x100002);
	check(!has("get-texture") && gDP.changed == 0, "only at its start");
	noTexture = 1;
	gli_fb_activate_texture(0, 0x100000);
	check(!has("activate") && gDP.changed == 0, "no texture: nothing");
	noTexture = 0;
	clear_log();
	gli_fb_activate_texture_bg(1, 0x200000);
	check(has("get-texture-bg") && gotFb == &b->fb && gotDrawnInto == 1 && activatedUnit == 1 && (gDP.changed & CHANGED_FB_TEXTURE),
		"the background's");
	gli_fb_activate_texture_bg(0, 0x100000);
	check(gotDrawnInto == 0 && gotFb == &a->fb, "not drawn into");
	gDP.changed = 0;
	clear_log();
	gli_fb_activate_texture_bg(0, 0x100002);
	check(!has("get-texture") && gDP.changed == 0, "the background only at its start");
	noTexture = 1;
	gli_fb_activate_texture_bg(0, 0x100000);
	check(!has("activate") && gDP.changed == 0, "the background with no texture: nothing");
	noTexture = 0;
}

static void test_fill_info(void)
{
	FBInfoEntry info[5];
	start();
	colour(0x100000, 320);
	colour(0x200000, 640);
	colour(0x300000, 320);
	colour(0x400000, 320);
	colour(0x700000, 320);
	gli_fb_save_cpu_buffer(0x500000, G_IM_SIZ_16b, 320);
	colour(0x600000, 320);
	memset(info, 0, sizeof(info));
	gli_fb_fill_info(info, 2);
	check(info[0].addr == 0x600000 && info[0].width == 320 && info[0].height == 240 && info[0].size == G_IM_SIZ_16b,
		"the newest first");
	check(info[1].addr == 0x400000 && info[2].addr == 0, "not CPU or depth ones, at most as many as asked");
	memset(info, 0, sizeof(info));
	gli_fb_fill_info(info, 4);
	check(info[2].addr == 0x300000 && info[3].addr == 0x100000 && info[4].addr == 0, "the VI's width only");
}

static void test_depth_current(void)
{
	u32 a = 1, w = 1;
	currentDepth = NULL;
	check(!gli_depth_current(&a, &w) && a == 1 && w == 1, "no depth buffer");
	currentDepth = &depth;
	depth.m_address = 0x700000;
	depth.m_width = 320;
	check(gli_depth_current(&a, &w) && a == 0x700000 && w == 320, "its address and width");
	currentDepth = NULL;
}

/* ---- rendering ---- */

static void test_render_screen_size(void)
{
	struct FrameBuffer *p, pp;
	start();
	config.frameBufferEmulation.enable = 0;
	clear_log();
	gli_fb_render();
	check(!has("swap") && !has("vi"), "none: nothing");
	VI.height = 240;
	gli_fb_set_color_image(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320);
	p = gli_fb_drawn();
	memset(&pp, 0, sizeof(pp));
	pp.fb.m_FBO = 333;
	pp.fb.m_pTexture = &textures[501];
	ppResult = &pp;
	gDP.changed = 0;
	vulkan = 1;
	config.stereo.mode = stereoOff;
	config.frameBufferEmulation.forceDepthBufferClear = 0;
	clear_log();
	gli_fb_render();
	ppResult = NULL;
	check(ppInput[(ppRuns - 1) & 1] == p, "post-processed");
	check(shownArgs[0] == 640 && shownArgs[1] == 320 && shownArgs[2] == G_IM_SIZ_16b && shownArgs[3] == 240 && shownScale == 2.0f,
		"shown to Vulkan");
	check(shownSrc[0] == 0 && shownSrc[1] == 0 && shownSrc[2] == 640 && shownSrc[3] == 480, "the window's source");
	check(shownDst[0] == 80 && shownDst[1] == 70 && shownDst[2] == 720 && shownDst[3] == 550, "centred on the screen");
	check(before("pp", "shown") && before("shown", "bind:draw:0") && before("bind:draw:0", "clear ") && before("clear ", "blit-or-copy"),
		"the screen bound and cleared before the blit");
	check(blitCopy.copy.r.srcX0 == 0 && blitCopy.copy.r.srcY0 == 480 && blitCopy.copy.r.srcX1 == 640 && blitCopy.copy.r.srcY1 == 0
		&& blitCopy.copy.r.srcWidth == 640 && blitCopy.copy.r.srcHeight == 480, "flipped from the window's size");
	check(blitCopy.copy.r.dstX0 == 80 && blitCopy.copy.r.dstY0 == 70 && blitCopy.copy.r.dstX1 == 720 && blitCopy.copy.r.dstY1 == 550
		&& blitCopy.copy.r.dstWidth == 800 && blitCopy.copy.r.dstHeight == 610, "to the screen");
	check(blitCopy.copy.filter == GLI_TP_FILTER_LINEAR && blitCopy.mask == GLI_BM_COLOR_BUFFER && blitCopy.copy.tex[0] == &textures[501]
		&& blitCopy.copy.combiner == copyProgram && blitCopy.readBuffer == 333 && blitCopy.drawBuffer == 0 && blitCopy.copy.tex[1] == NULL,
		"linear, its colour, from the post-processed buffer");
	check(before("blit-or-copy", "bind:read:0") && before("bind:read:0", "swap") && strstr(log_, "swap bind:draw:") != NULL
		&& boundName == p->fb.m_FBO, "the screen read, swapped, the buffer bound again");
	check(!has("stereo-end") && !has("clear-depth") && (gDP.changed & CHANGED_SCISSOR), "the scissor changed");
	vulkan = 0;
	config.stereo.mode = stereoBothEyes;
	config.frameBufferEmulation.forceDepthBufferClear = 1;
	clear_log();
	gli_fb_render();
	check(!has("shown") && before("stereo-end", "swap") && before("swap", "clear-depth"), "stereo ended, the depth cleared");
	config.stereo.mode = stereoOff;
}

static void vi_buffer_reset(void)
{
	memset(&viResult, 0, sizeof(viResult));
	viResult.vi_origin = 0x100000 + 640;
	viResult.vi_vres = 200;
	viResult.vi_minhpass = 3;
	viResult.vi_maxhpass = 4;
	viResult.vi_v_start = 5;
	viOk = 1;
	presentOk = 1;
	memset(&presentOut, 0, sizeof(presentOut));
	presentOut.src[0] = 1; presentOut.src[1] = 2; presentOut.src[2] = 3; presentOut.src[3] = 4;
	presentOut.dst[0] = 5; presentOut.dst[1] = 6; presentOut.dst[2] = 7; presentOut.dst[3] = 8;
	presentOut.nextSrcY1 = 300;
	presentOut.nextDstY[0] = 40;
	presentOut.nextDstY[1] = 50;
	copiesMade = 0;
	swaps = 0;
}

static struct FrameBuffer *vi_buffer(void)
{
	struct FrameBuffer *p;
	start();
	p = colour(0x100000, 320);
	p->fb.m_height = 240;
	p->fb.m_scale = 2.0f;
	p->fb.m_pTexture->realWidth = 640;
	p->fb.m_pTexture->realHeight = 480;
	vi_buffer_reset();
	return p;
}

static void render(void)
{
	copiesMade = 0;
	gli_fb_render();
}

/* the VI's buffer, @_lines long in RDRAM, and an older one at @_next
 * from its start */
static struct FrameBuffer *vi_buffer_next(u32 _lines, u32 _next, struct FrameBuffer **_n)
{
	struct FrameBuffer *p, *n;
	start();
	n = colour(0x100000 + _next, 320);
	p = colour(0x100000, 320);
	p->fb.m_height = 240;
	p->fb.m_endAddress = 0x100000 + 640 * _lines - 1;
	p->fb.m_scale = 2.0f;
	p->fb.m_pTexture->realWidth = 640;
	p->fb.m_pTexture->realHeight = 480;
	vi_buffer_reset();
	*_n = n;
	return p;
}

static void test_render(void)
{
	struct FrameBuffer *p, *n, eye, pp;
	gli_tex *oldTex;
	start();
	viOk = 0;
	colour(0x100000, 320);
	clear_log();
	render();
	check(strstr(log_, "vi bind:draw:0 clear swap bind:draw:") != NULL, "nothing scanned out: the screen cleared and swapped, the current one bound");
	gli_fb_set_current(NULL);
	clear_log();
	render();
	check(strstr(log_, "swap") != NULL && !has("swap bind"), "none current: none bound");

	p = vi_buffer();
	viResult.vi_origin = 0x900000;
	clear_log();
	render();
	check(!has("swap") && !has("copy"), "no buffer scanned out: nothing");

	p = vi_buffer();
	p->fb.m_isMainBuffer = 0;
	presentOk = 0;
	freed = 0;
	clear_log();
	render();
	check(p->fb.m_isMainBuffer == 1 && freed == 1 && gli_fb_find(0x100000) == NULL && !has("swap"), "running off its texture: dropped");

	p = vi_buffer();
	gDP.changed = 0;
	g_window.heightOffset = 10;
	clear_log();
	render();
	check(srcArgs[0] == 0x100000 && srcArgs[1] == 320 && srcArgs[2] == 240 && srcArgs[3] == G_IM_SIZ_16b && srcScale == 2.0f
		&& srcArgs[4] == 480, "the VI's source from the buffer");
	check(srcResult.vi_vres == 200 && srcResult.vi_minhpass == 3, "black bars kept");
	check(presentIn.bufScale == 2.0f && presentIn.texSize[0] == 640 && presentIn.texSize[1] == 480 && presentIn.windowWidth == 640
		&& presentIn.drawingWidth == 640 && presentIn.offset[0] == 80 && presentIn.offset[1] == 60
		&& presentIn.bufferSize[0] == 800 && presentIn.bufferSize[1] == 610 && presentIn.hasNext == 0 && presentIn.immersive == 0,
		"presented by the window and the overscan buffer off");
	check(presentIn.dstScaleY == 2.0f && presentSrc.srcHeight == 240, "the window's height over the NTSC field");
	check(copiesMade == 1 && copies[0].r.srcX0 == 1 && copies[0].r.srcY0 == 2 && copies[0].r.srcX1 == 3 && copies[0].r.srcY1 == 4
		&& copies[0].r.dstX0 == 5 && copies[0].r.dstY0 == 6 && copies[0].r.dstX1 == 7 && copies[0].r.dstY1 == 8, "copied by the rects");
	check(copies[0].r.srcWidth == 640 && copies[0].r.srcHeight == 480 && copies[0].r.dstWidth == 800 && copies[0].r.dstHeight == 610
		&& copies[0].filter == GLI_TP_FILTER_LINEAR && copies[0].tex[0] == p->fb.m_pTexture && copies[0].combiner == copyProgram
		&& copies[0].r.invertY == 1 && copies[0].tex[1] == NULL, "its texture, linear, inverted");
	check(before("pp", "bind:draw:0") && before("bind:draw:0", "clear ") && before("clear ", "copy") && before("copy", "bind:read:0")
		&& before("bind:read:0", "present") && before("present", "swap"), "post-processed, the screen bound and cleared, copied, presented, swapped");
	check(presented == 0x100000 && strstr(log_, "swap bind:draw:") != NULL && boundName == p->fb.m_FBO, "the current one bound again");
	check(scissor[0] == 80 && scissor[1] == 10 && scissor[2] == 640 && scissor[3] == 480 && (gDP.changed & CHANGED_SCISSOR),
		"the scissor the window's");
	check(!has("resolve") && !has("clear-depth") && !has("stereo-end") && !has("shown"), "nothing more");

	/* PAL */
	p = vi_buffer();
	viResult.vi_ispal = 1;
	render();
	check(presentIn.dstScaleY == 480.0f / 288.0f, "the PAL field");

	/* black bars removed, NTSC only */
	p = vi_buffer();
	RemoveFBBlackBars = 1;
	render();
	check(srcResult.vi_vres == 240 && srcResult.vi_minhpass == 0 && srcResult.vi_maxhpass == 0 && srcResult.vi_v_start == 0,
		"black bars removed");
	p = vi_buffer();
	viResult.vi_ispal = 1;
	render();
	check(srcResult.vi_vres == 200 && srcResult.vi_v_start == 5 && srcResult.vi_maxhpass == 4, "not on PAL");
	RemoveFBBlackBars = 0;

	/* immersive */
	p = vi_buffer();
	config.stereo.immersive = 1;
	immersiveFlat = 0;
	render();
	check(presentIn.immersive == 1, "immersive");
	immersiveFlat = 1;
	render();
	check(presentIn.immersive == 0, "immersive, flat");
	config.stereo.immersive = 0;
	immersiveFlat = 0;
	render();
	check(presentIn.immersive == 0, "not immersive");
	immersiveFlat = 1;

	/* multisampled, post-processed */
	p = vi_buffer();
	memset(&pp, 0, sizeof(pp));
	pp.fb.m_FBO = 444;
	pp.fb.m_resolveFBO = 445;
	pp.fb.m_pTexture = &textures[502];
	pp.fb.m_pResolveTexture = &textures[503];
	textures[502].frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	textures[503].realWidth = 1280;
	textures[503].realHeight = 960;
	ppResult = &pp;
	render();
	ppResult = NULL;
	check(resolvedFb == &pp.fb && resolvedForce == 1 && copies[0].tex[0] == &textures[503] && copies[0].r.srcWidth == 1280
		&& copies[0].r.srcHeight == 960, "multisampled: resolved, forced, its resolve copied");

	/* the next buffer */
	p = vi_buffer_next(200, 640 * 230, &n);
	n->fb.m_isMainBuffer = 0;
	n->fb.m_pTexture->realHeight = 200;
	n->fb.m_pTexture->realWidth = 600;
	clear_log();
	render();
	check(presentIn.hasNext == 1 && n->fb.m_isMainBuffer == 1 && copiesMade == 2, "the next buffer copied too");
	check(count_of("pp bind:draw:777 bind:draw:0 ") == 2, "the window bound again after each post-processing");
	check(copies[1].r.srcY0 == 0 && copies[1].r.srcY1 == 200 && copies[1].r.dstY0 == 40 && copies[1].r.dstY1 == 50
		&& copies[1].r.srcWidth == 600 && copies[1].r.srcHeight == 200 && copies[1].tex[0] == n->fb.m_pTexture
		&& copies[1].r.srcX0 == 1 && copies[1].r.dstX1 == 7, "its rows, clamped to its texture");
	n->fb.m_pTexture->realHeight = 400;
	render();
	check(copies[1].r.srcY1 == 300, "its rows within its texture");
	/* through its post-processing, resolved unforced, measured on the buffer */
	memset(&pp, 0, sizeof(pp));
	pp.fb.m_FBO = 444;
	pp.fb.m_resolveFBO = 445;
	pp.fb.m_pTexture = &textures[502];
	pp.fb.m_pResolveTexture = &textures[503];
	textures[502].frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	textures[502].realHeight = 250;
	textures[503].realHeight = 960;
	ppResult = &pp;
	resolvedForce = -1;
	render();
	ppResult = NULL;
	check(resolvedForce == 0 && copies[1].r.srcY1 == 250 && copies[1].r.srcHeight == 960, "the next one resolved unforced, clamped to the buffer's own texture");
	/* the next one the same buffer */
	p = vi_buffer();
	p->fb.m_height = 1000;
	render();
	check(presentIn.hasNext == 0 && copiesMade == 1, "the same buffer again: not next");
	/* the last line's last byte: origin + the stride a line, less one */
	p = vi_buffer_next(200, 640 * 240, &n);
	p->fb.m_height = 300;
	render();
	check(presentIn.hasNext == 0, "past the last line: not next");
	p = vi_buffer_next(200, 640 * 240 - 1, &n);
	p->fb.m_height = 300;
	render();
	check(presentIn.hasNext == 1, "on the last line's last byte: next");
	p = vi_buffer_next(80, 640 * 150, &n);
	p->fb.m_height = 100;
	render();
	check(presentIn.hasNext == 0, "by the buffer's height when shorter: before it");
	p = vi_buffer_next(80, 640 * 100 - 1, &n);
	p->fb.m_height = 100;
	render();
	check(presentIn.hasNext == 1, "by the buffer's height when shorter");
	p = vi_buffer_next(80, 640 * 100, &n);
	p->fb.m_height = 100;
	render();
	check(presentIn.hasNext == 0, "by the buffer's height, past it");

	/* the temporal upscaler */
	p = vi_buffer_next(200, 640 * 230, &n);
	vulkan = 1;
	upscaler = 1;
	clear_log();
	render();
	check(copiesMade == 1 && copies[0].r.srcX0 == 0 && copies[0].r.srcY0 == 0 && copies[0].r.srcX1 == 640 && copies[0].r.srcY1 == 480
		&& copies[0].r.dstX0 == 0 && copies[0].r.dstY0 == 0 && copies[0].r.dstX1 == 640 && copies[0].r.dstY1 == 480,
		"the upscaler's: the whole buffer, no next");
	check(shownSrc[2] == 640 && shownDst[3] == 480 && shownArgs[0] == 0x100000 && shownArgs[3] == 240 && shownScale == 2.0f,
		"shown to Vulkan");
	p->fb.m_pTexture->realWidth = 1000;
	p->fb.m_pTexture->realHeight = 700;
	render();
	check(copies[0].r.srcX1 == 800 && copies[0].r.srcY1 == 600, "cut to the screen");
	upscaler = 0;
	p->fb.m_pTexture->realWidth = 640;
	p->fb.m_pTexture->realHeight = 480;
	render();
	check(copiesMade == 2 && shownSrc[0] == 1 && shownDst[3] == 8, "Vulkan without it: as the VI shows");
	vulkan = 0;
	upscaler = 1;
	shownArgs[0] = 0;
	render();
	check(copiesMade == 2 && copies[0].r.srcX0 == 1 && shownArgs[0] == 0, "the upscaler only on Vulkan; not shown");
	upscaler = 0;

	/* RE2's depth clear */
	p = vi_buffer();
	config.generalEmulation.hacks = hack_RE2;
	clear_log();
	render();
	check(before("swap", "clear-depth"), "RE2: the depth cleared");
	config.frameBufferEmulation.N64DepthCompare = 1;
	clear_log();
	render();
	check(!has("clear-depth"), "not with N64 depth compare");
	config.frameBufferEmulation.N64DepthCompare = 0;
	config.generalEmulation.enableFragmentDepthWrite = 1;
	clear_log();
	render();
	check(!has("clear-depth"), "not with fragment depth write");
	config.frameBufferEmulation.forceDepthBufferClear = 1;
	clear_log();
	render();
	check(has("clear-depth"), "forced");
	config.generalEmulation.hacks = 0;
	config.generalEmulation.enableFragmentDepthWrite = 0;
	config.frameBufferEmulation.forceDepthBufferClear = 0;

	/* none current */
	p = vi_buffer();
	gli_fb_set_current(NULL);
	clear_log();
	render();
	check(has("swap") && !has("swap bind"), "none current: none bound");

	/* both eyes */
	p = vi_buffer_next(200, 640 * 230, &n);
	memset(&eye, 0, sizeof(eye));
	eye.fb.m_FBO = 555;
	eye.fb.m_pTexture = &textures[504];
	textures[504].realWidth = 640;
	textures[504].realHeight = 480;
	leftEye = &eye;
	leftEyeAsks = 0;
	config.stereo.mode = stereoBothEyes;
	ppRuns = 0;
	clear_log();
	render();
	check(copiesMade == 4 && count_of("clear ") == 1 && leftEyeAsks == 2 && leftEyeOf[0] == p && leftEyeOf[1] == n,
		"both eyes: four copies, the window cleared once, left eyes asked for both buffers");
	check(copies[0].tex[0] == &textures[504] && copies[1].tex[0] == &textures[504] && copies[2].tex[0] == p->fb.m_pTexture
		&& copies[3].tex[0] == n->fb.m_pTexture, "the left eye from its copies");
	check(copies[0].r.dstX0 == 5 && copies[2].r.dstX0 == 805 && copies[2].r.dstX1 == 807 && copies[0].r.dstWidth == 1600,
		"each eye into its half");
	check(before("stereo-end", "present") && count_of("swap") == 1, "stereo ended once");
	leftEye = NULL;
	render();
	check(copies[0].tex[0] == p->fb.m_pTexture, "no left eye: the buffer");
	config.stereo.mode = stereoOff;

	/* the overscan buffer */
	start();
	config.frameBufferEmulation.enableOverscan = 1;
	gli_fb_emulation_destroy();
	gli_fb_emulation_init();
	p = colour(0x100000, 320);
	p->fb.m_scale = 2.0f;
	p->fb.m_height = 200;
	p->fb.m_pTexture->realWidth = 640;
	p->fb.m_pTexture->realHeight = 480;
	p->fb.m_pTexture->format = G_IM_FMT_I;
	p->fb.m_pTexture->size = G_IM_SIZ_16b;
	p->fb.m_pTexture->width = 640;
	p->fb.m_pTexture->height = 480;
	viResult.vi_origin = 0x100000;
	viResult.vi_ispal = 0;
	config.frameBufferEmulation.overscanNTSC.left = 1;
	config.frameBufferEmulation.overscanNTSC.right = 2;
	config.frameBufferEmulation.overscanNTSC.top = 3;
	config.frameBufferEmulation.overscanNTSC.bottom = 4;
	config.frameBufferEmulation.overscanPAL.left = 10;
	config.frameBufferEmulation.overscanPAL.right = 20;
	config.frameBufferEmulation.overscanPAL.top = 30;
	config.frameBufferEmulation.overscanPAL.bottom = 40;
	copiesMade = 0;
	clear_log();
	render();
	check(has("attach-tex") && attachedTex == lastAdded && lastAddedTarget == GLI_TT_TEXTURE_2D && attachedFbo == 901
		&& attachedW == 320 && attachedH == 240 && attachedScale == 2.0f && attachedFmt == G_IM_FMT_I && attachedSize == G_IM_SIZ_16b,
		"the overscan buffer's texture made as the buffer's");
	check(presentIn.drawingWidth == 640 && presentIn.bufferSize[0] == 640 && presentIn.bufferSize[1] == 480 && presentIn.offset[0] == 0
		&& presentIn.offset[1] == 0 && presentIn.dstScaleY == 2.0f, "presented into it");
	check(copiesMade == 2 && copies[0].r.invertY == 0 && copies[0].r.dstWidth == 640 && copies[0].r.dstHeight == 480,
		"copied into it, not inverted");
	check(strstr(log_, "bind:draw:901 clear copy") != NULL, "it bound and cleared");
	check(copies[1].r.srcX0 == 2 && copies[1].r.srcY0 == 480 - 8 && copies[1].r.srcX1 == 640 - 4 && copies[1].r.srcY1 == 6,
		"then into the window cut by the NTSC overscan");
	check(copies[1].r.srcWidth == 640 && copies[1].r.srcHeight == 480 && copies[1].r.dstX0 == 80 && copies[1].r.dstY0 == 70
		&& copies[1].r.dstX1 == 720 && copies[1].r.dstY1 == 550 && copies[1].r.dstWidth == 800 && copies[1].r.dstHeight == 610,
		"into the window");
	check(copies[1].filter == GLI_TP_FILTER_LINEAR && copies[1].tex[0] == attachedTex && copies[1].combiner == copyProgram
		&& copies[1].r.invertY == 0, "linear, its texture");
	check(before("bind:read:0", "bind:draw:0") && strstr(log_, "bind:draw:0 clear copy") != NULL, "the window bound and cleared");
	check(scissor[0] == 0, "the scissor at its offset");
	g_window.height = 600;
	render();
	check(presentIn.dstScaleY == 2.0f, "its scale, not the window's");
	g_window.height = 480;
	clear_log();
	render();
	check(!has("attach-tex"), "its texture kept for the same buffer");
	oldTex = attachedTex;
	p->fb.m_scale = 3.0f;
	render();
	check(has("attach-tex") && lastRemoved == oldTex && attachedTex != oldTex && attachedScale == 3.0f,
		"made again for another scale, the old texture removed");
	check(presentIn.drawingWidth == 960 && presentIn.bufferSize[0] == 960 && presentIn.bufferSize[1] == 720,
		"drawn and sized as its texture");
	viResult.vi_ispal = 1;
	render();
	check(copies[1].r.srcX0 == 30 && copies[1].r.srcY0 == (s32)(288 * 3.0f) - 120 && copies[1].r.srcX1 == 960 - 60
		&& copies[1].r.srcY1 == 90, "the PAL overscan");
	p->fb.m_pTexture->width = 960;
	p->fb.m_pTexture->height = 720;
	clear_log();
	render();
	check(!has("attach-tex"), "kept at that scale");
	p->fb.m_pTexture->width = 100;
	clear_log();
	render();
	check(has("attach-tex"), "made again for another width");
	p->fb.m_pTexture->width = 960;
	render();
	clear_log();
	render();
	check(!has("attach-tex"), "kept again");
	p->fb.m_pTexture->height = 100;
	clear_log();
	render();
	check(has("attach-tex"), "made again for another height");
	config.stereo.mode = stereoBothEyes;
	copiesMade = 0;
	clear_log();
	render();
	check(copiesMade == 4 && count_of("clear ") == 3 && copies[0].r.dstWidth == 0 + attachedTex->width + 0
		&& copies[3].r.dstX0 == 80 + 800 && copies[3].r.dstX1 == 80 + 800 + 640 && copies[3].r.dstWidth == 1600
		&& copies[2].r.dstX0 == 5 && copies[2].r.dstX1 == 7, "both eyes through it, each cleared, each half of the window");
	config.stereo.mode = stereoOff;
	viResult.vi_ispal = 0;
	clear_log();
	gli_fb_emulation_destroy();
	check(lastDeletedFbo == 901 && lastRemoved == attachedTex, "stopped: its frame buffer and texture let go");
	config.frameBufferEmulation.enableOverscan = 0;
}

static void test_own_buffers(void)
{
	struct FrameBuffer *p, *eye = NULL, *first, *res;
	start();
	p = colour(0x100000, 320);
	p->fb.m_height = 200;
	p->fb.m_scale = 2.0f;
	p->fb.m_pTexture->size = G_IM_SIZ_16b;
	p->fb.m_pTexture->address = 0x100000;
	p->fb.m_pTexture->clampWidth = 320;
	p->fb.m_pTexture->clampHeight = 240;
	p->fb.m_pTexture->width = 320;
	p->fb.m_pTexture->height = 240;
	p->fb.m_pTexture->realWidth = 630;
	p->fb.m_pTexture->realHeight = 470;
	made = 0;
	clear_log();
	check(gli_fb_stereo_copy(p, &eye) && eye != NULL && made == 1, "the left eye made");
	check(eye->fb.m_startAddress == 0x100000 && eye->fb.m_width == 320 && eye->fb.m_height == 200 && eye->fb.m_size == G_IM_SIZ_16b
		&& eye->fb.m_scale == 2.0f, "as the buffer");
	check(eye->fb.m_pTexture->format == G_IM_FMT_RGBA && eye->fb.m_pTexture->size == G_IM_SIZ_16b && eye->fb.m_pTexture->address == 0x100000
		&& eye->fb.m_pTexture->clampS == 1 && eye->fb.m_pTexture->clampT == 1 && eye->fb.m_pTexture->clampWidth == 320
		&& eye->fb.m_pTexture->clampHeight == 240 && eye->fb.m_pTexture->frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE
		&& eye->fb.m_pTexture->width == 320 && eye->fb.m_pTexture->height == 240 && eye->fb.m_pTexture->realWidth == 630
		&& eye->fb.m_pTexture->realHeight == 470 && eye->fb.m_pTexture->textureBytes == 630 * 470 * 4, "its texture as the buffer's, RGBA");
	check(texInit.handle == eye->fb.m_pTexture->name && texInit.width == 630 && texInit.height == 470 && texInit.internalFormat == GLI_ICF_RGBA8 + 1
		&& texInit.format == GLI_CF_RGBA && texInit.dataType == GLI_DT_UNSIGNED_BYTE && texInit.target == GLI_TT_TEXTURE_2D
		&& texInit.data == NULL, "its texture made");
	check(texParams.handle == eye->fb.m_pTexture->name && texParams.target == GLI_TT_TEXTURE_2D && texParams.minFilter == GLI_TP_FILTER_LINEAR
		&& texParams.magFilter == GLI_TP_FILTER_LINEAR && texParams.wrapS == GLI_PARAM_INVALID, "linear");
	check(rtarget.bufferHandle == eye->fb.m_FBO && rtarget.bufferTarget == GLI_BT_DRAW_FRAMEBUFFER && rtarget.attachment == GLI_BA_COLOR_ATTACHMENT0
		&& rtarget.textureTarget == GLI_TT_TEXTURE_2D && rtarget.textureHandle == eye->fb.m_pTexture->name, "attached");
	check(blit.readBuffer == p->fb.m_FBO && blit.drawBuffer == eye->fb.m_FBO && blit.srcX0 == 0 && blit.srcY0 == 0 && blit.srcX1 == 630
		&& blit.srcY1 == 470 && blit.dstX0 == 0 && blit.dstY0 == 0 && blit.dstX1 == 630 && blit.dstY1 == 470
		&& blit.mask == GLI_BM_COLOR_BUFFER && blit.filter == GLI_TP_FILTER_NEAREST, "blitted whole, nearest");
	check(before("render-target", "blit") && strstr(log_, "blit bind:draw:0") != NULL, "then the screen bound");
	first = eye;
	made = freed = 0;
	blitResult = 0;
	check(!gli_fb_stereo_copy(p, &eye) && eye == first && made == 0, "kept for the same size; the blit's result");
	blitResult = 1;
	p->fb.m_pTexture->frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	p->fb.m_resolveFBO = 66;
	gli_fb_stereo_copy(p, &eye);
	check(resolvedFb == &p->fb && resolvedForce == 1 && blit.readBuffer == 66, "multisampled: from its resolve");
	p->fb.m_pTexture->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	p->fb.m_pTexture->realWidth = 320;
	gli_fb_stereo_copy(p, &eye);
	check(made == 1 && freed == 1 && lastFreed == &first->fb && blit.srcX1 == 320 && eye->fb.m_pTexture->realWidth == 320,
		"made again for another width");
	p->fb.m_pTexture->realHeight = 240;
	gli_fb_stereo_copy(p, &eye);
	check(made == 2 && freed == 2 && blit.srcY1 == 240, "made again for another height");
	freed = 0;
	gli_fb_stereo_delete(eye);
	check(freed == 1, "let go");
	freed = 0;
	gli_fb_stereo_delete(NULL);
	check(freed == 0, "none let go");

	p->fb.m_pTexture->realWidth = 630;
	p->fb.m_pTexture->realHeight = 470;
	made = 0;
	clear_log();
	res = gli_fb_pp_create(p);
	check(res != NULL && made == 1 && res->fb.m_width == 320 && res->fb.m_height == 200 && res->fb.m_scale == 2.0f
		&& res->fb.m_startAddress == 0, "a post-processing buffer the buffer's size");
	check(res->fb.m_pTexture->format == G_IM_FMT_RGBA && res->fb.m_pTexture->clampS == 1 && res->fb.m_pTexture->clampT == 1
		&& res->fb.m_pTexture->frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE && res->fb.m_pTexture->realWidth == 630
		&& res->fb.m_pTexture->realHeight == 470 && res->fb.m_pTexture->textureBytes == 630 * 470 * 4, "its texture");
	check(texInit.width == 630 && texInit.internalFormat == GLI_ICF_RGBA8 + 1 && texParams.minFilter == GLI_TP_FILTER_NEAREST
		&& texParams.magFilter == GLI_TP_FILTER_NEAREST && rtarget.bufferHandle == res->fb.m_FBO, "nearest, attached");
	clear_log();
	copiesMade = 0;
	gli_fb_pp_draw(p, res, copyProgram);
	{
		char want[64];
		sprintf(want, "bind:read:0 bind:draw:%u copy", res->fb.m_FBO);
		check(strstr(log_, want) != NULL && copiesMade == 1, "drawn into it");
	}
	check(copies[0].r.srcX0 == 0 && copies[0].r.srcY0 == 0 && copies[0].r.srcX1 == 630 && copies[0].r.srcY1 == 470 && copies[0].r.srcWidth == 630
		&& copies[0].r.srcHeight == 470 && copies[0].r.dstX0 == 0 && copies[0].r.dstY0 == 0 && copies[0].r.dstX1 == 630 && copies[0].r.dstY1 == 470
		&& copies[0].r.dstWidth == 630 && copies[0].r.dstHeight == 470 && copies[0].tex[0] == p->fb.m_pTexture
		&& copies[0].filter == GLI_TP_FILTER_NEAREST && copies[0].combiner == copyProgram && copies[0].r.invertY == 0, "whole, nearest, by the shader");
	check(strstr(log_, "copy bind:draw:0 reset-shader") != NULL, "then the screen and no program");
	res->fb.m_pTexture->realWidth = 100;
	p->fb.m_pTexture->frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	p->fb.m_pResolveTexture = &textures[505];
	textures[505].realWidth = 77;
	gli_fb_pp_draw(p, res, copyProgram);
	check(resolvedForce == 1 && copies[1].tex[0] == &textures[505] && copies[1].r.srcX1 == 77 && copies[1].r.dstX1 == 100,
		"multisampled: from its resolve, to its size");
	p->fb.m_pTexture->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	freed = 0;
	gli_fb_delete(res);
	check(freed == 1, "let go");
}

int main(void)
{
	setup();
	test_init_destroy();
	test_screen_size();
	test_save_new();
	test_previous();
	test_aux_and_stereo();
	test_reuse();
	test_depth_clear_immersive();
	test_slots_full();
	test_lookups();
	test_changed_fill();
	test_check_texture();
	test_sync();
	test_depth_copy();
	test_vi_resized();
	test_attach_depth();
	test_drawn_scales();
	test_activate();
	test_fill_info();
	test_depth_current();
	test_render_screen_size();
	test_render();
	test_own_buffers();
	gli_fb_emulation_destroy();
	if (failures != 0) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_framebuffer: all passed\n");
	return 0;
}
