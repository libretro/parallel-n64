/* test_gliden64_texrect_gpu -- the texrect drawer's GPU objects, against a
 * backend that records what it is asked.
 *
 * The drawer's GPU side was C++ (TexrectDrawerGpu.cpp); it is C now, in
 * TexrectDrawer.c, and its two shaders are reached through their C face
 * (gli_cprog). Made, it has a frame buffer drawing into a 640x580 texture
 * of the backend's color format, filtered linearly, bound and then let
 * go on the target a frame buffer is drawn to (on GLES2, the one target),
 * and its draw shader told that texture's size. Destroyed, the frame
 * buffer, the texture and each shader go, once.
 *
 *   ./test_gliden64_texrect_gpu
 */
#include <stdio.h>
#include <string.h>

#include "TexrectDrawer.h"
#include "gli_combiner.h"
#include "gli_tex.h"
#include "gli_framebuffer.h"
#include "Graphics/gli_context.h"
#include "N64.h"
#include "GBI.h"
#include "RDP.h"
#include "RSP.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- what the drawer reads ---- */

RDPInfo RDP;
RSPInfo RSP;
VIInfo VI;
gDPInfo gDP;
gSPInfo gSP;
u8 *RDRAM;
u32 RDRAMSize;
const MicrocodeInfo *gli_GBI;
gli_combiner_info gli_cmb;
gli_context gli_ctx;

static RectVertex rect[4];
static gli_tex fbTexture;
static u32 added, removed;

u16 gli_VI_GetMaxBufferHeight(u16 _width) { (void)_width; return 240; }
void gli_combiner_update(void) {}
void gli_combiner_update_parameters(void) {}
RectVertex *gli_drawer_rect(void) { return rect; }
void gli_drawer_screen_viewport(const struct FrameBuffer *_p) { (void)_p; }
void gli_drawer_set_blend_mode(void) {}
void gli_drawer_texrect_states(void) {}
void gli_drawer_update_depth_compare(void) {}
void gli_drawer_update_scissor(struct FrameBuffer *_p) { (void)_p; }
void gli_fb_bind_draw(const struct FrameBuffer *_p) { (void)_p; }
void gli_fb_coords_scales(const struct FrameBuffer *_p, f32 *_x, f32 *_y) { (void)_p; *_x = *_y = 1.0f; }
struct FrameBuffer *gli_fb_current(void) { return NULL; }
void gli_fb_set_current_draw_buffer(void) {}
u32 gli_fb_width(const struct FrameBuffer *_p) { (void)_p; return 320; }
void gli_textures_activate(u32 _t, gli_tex *_tex) { (void)_t; (void)_tex; }
gli_tex *gli_textures_add_fb(u32 _target)
{
	(void)_target;
	added++;
	memset(&fbTexture, 0, sizeof(fbTexture));
	fbTexture.name = 77;
	return &fbTexture;
}
void gli_textures_remove_fb(gli_tex *_tex) { if (_tex == &fbTexture) removed++; }

/* ---- the shaders ---- */

typedef struct fake_shader
{
	gli_cprog face;
	u32 width, height, freed;
} fake_shader;

static fake_shader drawShader, clearShader;

static void sh_activate(void *_p) { (void)_p; }
static void sh_update(void *_p, bool _f) { (void)_p; (void)_f; }
static bool sh_lod(const void *_p) { (void)_p; return 0; }
static void sh_free(void *_p) { ((fake_shader *)_p)->freed++; }
static void sh_size(void *_p, u32 _w, u32 _h) { ((fake_shader *)_p)->width = _w; ((fake_shader *)_p)->height = _h; }
static void sh_alpha(void *_p, int _e) { (void)_p; (void)_e; }

static const gli_cprog_ops drawOps = { sh_activate, sh_update, sh_lod, sh_free, NULL, NULL, NULL, NULL, sh_size, sh_alpha };
static const gli_cprog_ops clearOps = { sh_activate, sh_update, sh_lod, sh_free, NULL, NULL, NULL, NULL, NULL, NULL };

/* ---- the backend ---- */

static gli_context_ops ops;
static u32 fbos, fboDeleted;
static gli_init_texture_params lastInit;
static gli_tex_parameters lastParams;
static gli_fb_render_target lastTarget;
static u32 binds, lastBindTarget, lastBindName;

static u32 fake_create_fb(void *_i) { (void)_i; return 500 + ++fbos; }
static void fake_delete_fb(void *_i, u32 _n) { (void)_i; if (_n == 501) fboDeleted++; }
static void fake_bind_fb(void *_i, u32 _t, u32 _n) { (void)_i; binds++; lastBindTarget = _t; lastBindName = _n; }
static void fake_target(void *_i, const gli_fb_render_target *_p) { (void)_i; lastTarget = *_p; }
static void fake_init(void *_i, const gli_init_texture_params *_p) { (void)_i; lastInit = *_p; }
static void fake_set(void *_i, const gli_tex_parameters *_p) { (void)_i; lastParams = *_p; }
static bool fake_fb_error(const void *_i) { (void)_i; return 0; }
static struct gli_cprog *fake_draw_shader(void *_i) { (void)_i; memset(&drawShader, 0, sizeof(drawShader)); drawShader.face.ops = &drawOps; return &drawShader.face; }
static struct gli_cprog *fake_clear_shader(void *_i) { (void)_i; memset(&clearShader, 0, sizeof(clearShader)); clearShader.face.ops = &clearOps; return &clearShader.face; }

static void run(u32 _drawTarget)
{
	char what[96];
	memset(&ops, 0, sizeof(ops));
	ops.createFramebuffer = fake_create_fb;
	ops.deleteFramebuffer = fake_delete_fb;
	ops.bindFramebuffer = fake_bind_fb;
	ops.addFrameBufferRenderTarget = fake_target;
	ops.init2DTexture = fake_init;
	ops.setTextureParameters = fake_set;
	ops.isFramebufferError = fake_fb_error;
	ops.createTexrectDrawerDrawShader = fake_draw_shader;
	ops.createTexrectDrawerClearShader = fake_clear_shader;
	gli_ctx.ops = &ops;
	gli_fb_formats_gl(&gli_ctx.formats, GLI_FB_GL, 0);
	gli_ctx.drawFramebuffer = _drawTarget;
	gli_ctx.readFramebuffer = _drawTarget;
	fbos = fboDeleted = added = removed = binds = 0;

	gli_texrect_init();
	sprintf(what, "target %#x: ", _drawTarget);
	check(fbos == 1 && added == 1, "init makes a frame buffer and a texture");
	check(lastInit.handle == 77 && lastInit.width == 640 && lastInit.height == 580 &&
		lastInit.textureUnitIndex == GLI_TU_TEX0 && lastInit.internalFormat == GLI_ICF_RGBA8 &&
		lastInit.format == GLI_CF_RGBA && lastInit.dataType == GLI_DT_UNSIGNED_BYTE,
		"a 640x580 texture of the color format, on unit 0");
	check(fbTexture.frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE && fbTexture.textureBytes == 640 * 580 * 4 &&
		fbTexture.clampS && fbTexture.clampT, "the texture is a clamped frame buffer texture of its bytes");
	check(lastParams.handle == 77 && lastParams.minFilter == GLI_TP_FILTER_LINEAR &&
		lastParams.magFilter == GLI_TP_FILTER_LINEAR, "filtered linearly");
	check(lastTarget.bufferHandle == 501 && lastTarget.textureHandle == 77 &&
		lastTarget.attachment == GLI_BA_COLOR_ATTACHMENT0 && lastTarget.textureTarget == GLI_TT_TEXTURE_2D,
		"the texture is the frame buffer's color");
	if (lastTarget.bufferTarget != _drawTarget || lastBindTarget != _drawTarget) {
		printf("FAIL: %sattached and let go on the draw target\n", what);
		failures++;
	}
	check(lastBindName == 0, "the frame buffer is let go after");
	check(drawShader.width == 640 && drawShader.height == 580, "the draw shader is told the texture's size");

	gli_texrect_destroy();
	check(fboDeleted == 1 && removed == 1, "destroy deletes the frame buffer and removes the texture");
	check(drawShader.freed == 1 && clearShader.freed == 1, "each shader is freed once");
}

int main(void)
{
	run(GLI_BT_DRAW_FRAMEBUFFER);
	run(GLI_BT_FRAMEBUFFER);
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("texrect gpu: ok\n");
	return 0;
}
