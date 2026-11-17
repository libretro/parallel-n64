/* test_gliden64_depth_buffer -- GLideN64's depth buffers' GPU objects,
 * against a backend that records what it is asked.
 *
 * The depth buffer was a C++ class (DepthBufferGpu.cpp); it is C now, in
 * DepthBuffer.c. A depth buffer of a frame buffer takes its texture's size
 * in the backend's depth format, filtered nearest; one for the screen
 * takes the window's size; without depth textures it is a renderbuffer.
 * Multisampled, it has its own resolve, blitted into once until it is
 * attached again. Its copy blits its depth into a frame buffer of the
 * buffer's color, and its depth images clear to 1. Deleting it gives
 * back every object, and its slot.
 *
 *   ./test_gliden64_depth_buffer
 */
#include <stdio.h>
#include <string.h>

#include "DepthBuffer.h"
#include "gli_tex.h"
#include "gli_fb_rdram.h"
#include "gli_framebuffer.h"
#include "Graphics/gli_context.h"
#include "Config.h"
#include "DisplayWindow.h"
#include "VI.h"
#include "gDP.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- what the depth buffers read ---- */

Config gliden64Config;
VIInfo VI;
gDPInfo gDP;
gli_window g_window;
gli_context gli_ctx;

static gli_fb fb;
static gli_tex fbTex, fbResolve;
static gli_tex texPool[16];
static u32 texUsed, texRemoved, activated;

u16 gli_VI_GetMaxBufferHeight(u16 _width) { (void)_width; return 240; }
void gli_fb_attach_depth_buffer(void) {}
void gli_fb_clear_depth_buffer(struct DepthBuffer *_p) { (void)_p; }
const struct gli_fb *gli_fb_current_info(void) { return &fb; }
struct FrameBuffer *gli_fb_find(u32 _a) { (void)_a; return (struct FrameBuffer *)&fb; }
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return (struct gli_fb *)_p; }
void gli_fb_set_current_draw_buffer(void) {}
void gli_fb_set_is_depth(struct FrameBuffer *_p) { (void)_p; }
u32 gli_fb_start_address(const struct FrameBuffer *_p) { (void)_p; return 0; }
u32 gli_fb_width(const struct FrameBuffer *_p) { (void)_p; return 320; }
void gli_textures_activate(u32 _t, gli_tex *_tex) { (void)_t; (void)_tex; activated++; }
gli_tex *gli_textures_add_fb(u32 _target)
{
	gli_tex *t = &texPool[texUsed++];
	(void)_target;
	memset(t, 0, sizeof(*t));
	t->name = 100 + texUsed;
	return t;
}
void gli_textures_remove_fb(gli_tex *_tex) { if (_tex != NULL) texRemoved++; }

/* ---- the backend ---- */

static gli_context_ops ops;
static u32 fbos, fbosDeleted, renderbuffers, blits, clears, barriers, images, drawBuffers;
static gli_init_texture_params lastInit;
static gli_tex_parameters lastParams;
static gli_fb_render_target targets[8];
static u32 numTargets;
static gli_blit_params lastBlit;
static gli_init_renderbuffer_params lastRb;
static u32 lastBindTarget, lastBindName;

static u32 c_fb(void *_i) { (void)_i; return 200 + ++fbos; }
static void d_fb(void *_i, u32 _n) { (void)_i; if (_n != 0) fbosDeleted++; }
static void b_fb(void *_i, u32 _t, u32 _n) { (void)_i; lastBindTarget = _t; lastBindName = _n; }
static void t_fb(void *_i, const gli_fb_render_target *_p) { (void)_i; if (numTargets < 8) targets[numTargets] = *_p; numTargets++; }
static u32 c_rb(void *_i) { (void)_i; return 300 + ++renderbuffers; }
static void i_rb(void *_i, const gli_init_renderbuffer_params *_p) { (void)_i; lastRb = *_p; }
static bool blit(void *_i, const gli_blit_params *_p) { (void)_i; lastBlit = *_p; blits++; return 1; }
static void init(void *_i, const gli_init_texture_params *_p) { (void)_i; lastInit = *_p; }
static void setp(void *_i, const gli_tex_parameters *_p) { (void)_i; lastParams = *_p; }
static void clr(void *_i, f32 _r, f32 _g, f32 _b, f32 _a) { (void)_i; if (_r == 1.0f && _g == 0.0f && _b == 0.0f && _a == 0.0f) clears++; }
static void clrd(void *_i) { (void)_i; }
static void barrier(void *_i) { (void)_i; barriers++; }
static void image(void *_i, const gli_bind_image_texture_params *_p) { (void)_i; (void)_p; images++; }
static void draws(void *_i, u32 _n) { (void)_i; drawBuffers = _n; }

static void reset(int _depthTextures, int _msaa)
{
	static const gli_fb_formats unset = GLI_FB_FORMATS_DEFAULT;
	memset(&ops, 0, sizeof(ops));
	ops.createFramebuffer = c_fb;
	ops.deleteFramebuffer = d_fb;
	ops.bindFramebuffer = b_fb;
	ops.addFrameBufferRenderTarget = t_fb;
	ops.createRenderbuffer = c_rb;
	ops.initRenderbuffer = i_rb;
	ops.blitFramebuffers = blit;
	ops.init2DTexture = init;
	ops.setTextureParameters = setp;
	ops.clearColorBuffer = clr;
	ops.clearDepthBuffer = clrd;
	ops.textureBarrier = barrier;
	ops.bindImageTexture = image;
	ops.setDrawBuffers = draws;
	gli_ctx.ops = &ops;
	gli_ctx.formats = unset;
	gli_fb_formats_gl(&gli_ctx.formats, GLI_FB_GL, 0);
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	gli_ctx.depthFramebufferTextures = _depthTextures != 0;
	gli_ctx.imageTextures = 0;
	gli_ctx.framebufferFetch = 0;
	config.video.multisampling = _msaa ? 4 : 0;
	config.frameBufferEmulation.N64DepthCompare = 0;
	config.frameBufferEmulation.nativeResFactor = 0;
	config.frameBufferEmulation.enable = 1;
	g_window.width = 640;
	g_window.scaleX = 2.0f;
	VI.width = 320;
	fbos = fbosDeleted = renderbuffers = blits = clears = barriers = images = drawBuffers = 0;
	texUsed = texRemoved = activated = numTargets = 0;

	memset(&fb, 0, sizeof(fb));
	memset(&fbTex, 0, sizeof(fbTex));
	memset(&fbResolve, 0, sizeof(fbResolve));
	fbTex.name = 11;
	fbTex.width = 640;
	fbTex.height = 480;
	fbResolve.name = 12;
	fb.m_FBO = 21;
	fb.m_resolveFBO = 22;
	fb.m_pTexture = &fbTex;
	fb.m_pResolveTexture = &fbResolve;
	fb.m_startAddress = 0x100000;
	fb.m_width = 320;
	fb.m_height = 240;
}

static void test_texture(void)
{
	DepthBuffer *d;
	u32 i;
	reset(1, 0);
	d = gli_depth_new(0x200000, &fb);
	check(d != NULL && d->m_address == 0x200000 && d->m_width == 320, "a depth buffer of its address and the buffer's width");
	check(fbos == 1 && d->m_copyFBO == 201, "it has its copy frame buffer");
	check(d->m_pDepthBufferTexture != NULL && d->m_pDepthBufferTexture->width == 640 &&
		d->m_pDepthBufferTexture->height == 480 && d->m_pDepthBufferTexture->clampHeight == 240 &&
		d->m_pDepthBufferTexture->address == 0x100000, "its texture is the buffer's size");
	check(lastInit.internalFormat == GLI_ICF_DEPTH && lastInit.format == GLI_CF_DEPTH &&
		lastInit.dataType == GLI_DT_FLOAT && lastInit.msaaLevel == 0, "in the backend's depth format");
	check(lastParams.minFilter == GLI_TP_FILTER_NEAREST && d->m_pDepthBufferTexture->frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE,
		"single-sampled, filtered nearest");
	check(d->m_pDepthBufferTexture->textureBytes == 640 * 480 * 4, "its bytes");

	gli_depth_set_attachment(d, 21, GLI_BT_DRAW_FRAMEBUFFER);
	check(targets[0].attachment == GLI_BA_DEPTH_ATTACHMENT && targets[0].bufferHandle == 21 &&
		targets[0].textureHandle == d->m_pDepthBufferTexture->name && targets[0].textureTarget == GLI_TT_TEXTURE_2D,
		"attached as the frame buffer's depth");

	check(gli_depth_resolve_texture(d, &fb) == d->m_pDepthBufferTexture && blits == 0, "single-sampled needs no resolve");

	numTargets = 0;
	{
		gli_tex *copy = gli_depth_copy_texture(d, &fb);
		check(copy != NULL && copy == d->m_pDepthBufferCopyTexture && blits == 1, "the copy is blitted");
		check(numTargets == 2 && targets[0].bufferHandle == 201 && targets[0].attachment == GLI_BA_COLOR_ATTACHMENT0 &&
			targets[0].textureHandle == 11 && targets[1].attachment == GLI_BA_DEPTH_ATTACHMENT &&
			targets[1].textureHandle == copy->name, "into a frame buffer of the buffer's color and the copy");
		check(lastBlit.readBuffer == 21 && lastBlit.drawBuffer == 201 && lastBlit.mask == GLI_BM_DEPTH_BUFFER &&
			lastBlit.srcX1 == 640 && lastBlit.dstY1 == 480 && lastBlit.filter == GLI_TP_FILTER_NEAREST,
			"depth alone, the texture's size, nearest");
		check(lastBindTarget == GLI_BT_DRAW_FRAMEBUFFER && lastBindName == 21, "drawing back into the buffer after");
		check(gli_depth_copy_texture(d, &fb) == copy && blits == 1, "copied once until attached again");
		gli_depth_set_attachment(d, 21, GLI_BT_DRAW_FRAMEBUFFER);
		gli_depth_copy_texture(d, &fb);
		check(blits == 2, "attached again, copied again");
	}

	gli_depth_activate_texture(d, &fb);
	check(activated == 1 && barriers == 1, "activating samples it behind a texture barrier");

	i = texUsed;
	{
		DepthBuffer *const old = d;
		gli_depth_delete(d);
		check(texRemoved == i && fbosDeleted == 1, "deleting gives back every texture and frame buffer");
		d = gli_depth_new(0x300000, &fb);
		check(d == old, "its slot is free again");
	}
	check(d != NULL && d->m_address == 0x300000 && d->m_pDepthImageZTexture == NULL && !d->m_copied,
		"a slot given back is used again with no state");
	gli_depth_delete(d);
}

static void test_screen_and_renderbuffer(void)
{
	DepthBuffer *d;
	reset(1, 0);
	d = gli_depth_new(0x200000, NULL);
	check(d->m_width == 320 && d->m_pDepthBufferTexture->width == 640 && d->m_pDepthBufferTexture->height == 480 &&
		d->m_pDepthBufferTexture->clampWidth == 320, "the screen's: the window's width, the scaled height");
	gli_depth_delete(d);

	reset(0, 0);
	d = gli_depth_new(0x200000, &fb);
	check(d->m_pDepthBufferTexture == NULL && renderbuffers == 1 && lastRb.width == 640 && lastRb.height == 480 &&
		lastRb.target == GLI_TT_RENDERBUFFER && lastRb.format == GLI_ICF_DEPTH, "without depth textures, a renderbuffer");
	gli_depth_set_attachment(d, 21, GLI_BT_READ_FRAMEBUFFER);
	check(targets[0].textureHandle == d->m_depthRenderbuffer && targets[0].textureTarget == GLI_TT_RENDERBUFFER &&
		targets[0].bufferTarget == GLI_BT_READ_FRAMEBUFFER, "attached as a renderbuffer, to the target asked");
	gli_depth_delete(d);
}

static void test_multisampled(void)
{
	DepthBuffer *d;
	reset(1, 1);
	d = gli_depth_new(0x200000, &fb);
	check(d->m_pDepthBufferTexture->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE && d->m_pResolveDepthBufferTexture != NULL,
		"multisampled, with a resolve");
	gli_depth_set_attachment(d, 21, GLI_BT_DRAW_FRAMEBUFFER);
	check(targets[0].textureTarget == GLI_TT_TEXTURE_2D_MULTISAMPLE, "attached multisampled");
	numTargets = 0;
	check(gli_depth_resolve_texture(d, &fb) == d->m_pResolveDepthBufferTexture && blits == 1 &&
		targets[0].bufferHandle == 22 && lastBlit.readBuffer == 21 && lastBlit.drawBuffer == 22, "resolved into the buffer's resolve");
	gli_depth_resolve_texture(d, &fb);
	check(blits == 1, "resolved once until attached again");
	gli_depth_set_attachment(d, 21, GLI_BT_DRAW_FRAMEBUFFER);
	gli_depth_resolve_texture(d, &fb);
	check(blits == 2, "attached again, resolved again");
	gli_depth_delete(d);
}

static void test_images(void)
{
	DepthBuffer *d;
	reset(1, 0);
	config.frameBufferEmulation.N64DepthCompare = 1;
	d = gli_depth_new(0x200000, &fb);
	fb.m_pDepthBuffer = d;
	gli_depth_init_image_texture(d, &fb);
	check(d->m_pDepthImageZTexture != NULL && d->m_pDepthImageDeltaZTexture != NULL && fbos == 3,
		"depth compare: two images, each with a clear frame buffer");
	check(clears == 2, "made, the images are cleared");
	clears = 0;
	check(lastInit.internalFormat == GLI_ICF_R32F && d->m_pDepthImageZTexture->width == 640, "the images in their format and size");
	gli_depth_gpu_clear();
	check(clears == 2, "the images clear to 1");
	gli_ctx.framebufferFetch = 1;
	numTargets = 0;
	gli_depth_bind_image_texture(d, 21);
	check(numTargets == 2 && targets[0].attachment == GLI_BA_COLOR_ATTACHMENT1 && targets[1].attachment == GLI_BA_COLOR_ATTACHMENT2 &&
		drawBuffers == 3, "with framebuffer fetch, the images are attachments 1 and 2");
	gli_ctx.imageTextures = 1;
	gli_depth_bind_image_texture(d, 21);
	check(images == 2, "with image textures, two image units");
	gli_depth_delete(d);
	check(fbosDeleted == 3, "the clear frame buffers go with it");
}

int main(void)
{
	test_texture();
	test_screen_and_renderbuffer();
	test_multisampled();
	test_images();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("depth buffer: ok\n");
	return 0;
}
