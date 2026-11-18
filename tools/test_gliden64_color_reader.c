/* test_gliden64_color_reader -- reading a frame buffer texture's rows back
 * for GLideN64's copy to RDRAM (Graphics/ColorBufferReader.c and the GL and
 * Vulkan readers).
 *
 * A read picks the frame buffer format of a 16- or 32-bit image and the
 * monochrome one of an 8-bit image. Integer rows come back where they lie,
 * the stride the reader's, as many rows as the buffer holds past the
 * offset; float rows come back as bytes in the reader's own memory, as
 * many as it holds. A GL buffer reader has as many buffers as copyToRDRAM
 * asks, one to three, binds them through the binding cache, and a slot is
 * ready once its fence signals: a poll does not wait, a wait does, and
 * the fence goes once seen. The pixel buffer reader maps a slot as it is
 * read and unmaps it on clean-up; the buffer storage reader maps each
 * buffer once. Vulkan's reader has one to eight readbacks. The GL and
 * Vulkan entry points are stubs.
 *
 *   ./test_gliden64_color_reader
 */
#include <stdio.h>
#include <string.h>

#include "Graphics/ColorBufferReader.h"
#include "Graphics/OpenGLContext/opengl_ColorBufferReader.h"
#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/VulkanContext/vk_ColorBufferReader.h"
#include "Graphics/gli_context.h"
#include "Config.h"
#include "GBI.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

Config gliden64Config;
gli_context gli_ctx;

/* ---- a stub reader for the shared logic ---- */

static u8 gpu[4096];
static u32 stubOffset, stubStride, destroyed, cleaned;
static gli_cbr_read lastRead;
static int mapNull;

static void stub_destroy(gli_cbr *_r) { (void)_r; destroyed++; }
static const u8 *stub_read(gli_cbr *_r, const gli_cbr_read *_p, u32 *_off, u32 *_stride)
{
	(void)_r;
	lastRead = *_p;
	*_off = stubOffset;
	*_stride = stubStride;
	return gpu;
}
static void stub_clean(gli_cbr *_r) { (void)_r; cleaned++; }
static const u8 *stub_map(gli_cbr *_r, u32 _slot, u32 _h, u32 _b, u32 *_stride)
{
	(void)_r; (void)_slot; (void)_h; (void)_b;
	*_stride = stubStride;
	return mapNull ? NULL : gpu;
}
static const gli_cbr_ops stub_ops = { stub_destroy, stub_read, stub_clean, NULL, NULL, stub_map };

static void set_formats(u32 _colorType)
{
	memset(&gli_ctx.formats, 0, sizeof(gli_ctx.formats));
	gli_ctx.formats.colorFormat = 0x1908;
	gli_ctx.formats.colorType = _colorType;
	gli_ctx.formats.colorFormatBytes = _colorType == GLI_DT_FLOAT ? 16 : 4;
	gli_ctx.formats.monochromeFormat = 0x1903;
	gli_ctx.formats.monochromeType = 0x1401;
	gli_ctx.formats.monochromeFormatBytes = 1;
}

static void test_shared(void)
{
	gli_tex tex;
	gli_cbr *r;
	const u8 *p;
	u32 height, stride, i;
	float *f;

	memset(&tex, 0, sizeof(tex));
	tex.width = 8;
	tex.textureBytes = 8 * 4 * 4;   /* 4 rows of 8 RGBA8 pixels */
	r = gli_cbr_new(sizeof(gli_cbr), &stub_ops, &tex);
	check(r != NULL && r->texture == &tex && r->numSlots == 0, "a reader of the texture, no slots");
	for (i = 0; i < tex.textureBytes && r->pixelData[i] == 0; ++i);
	check(i == tex.textureBytes, "its memory the texture's bytes, cleared");

	set_formats(0x1401);
	stubOffset = 1;
	stubStride = 8;
	height = 10;
	p = gli_cbr_read_pixels(r, 2, 3, 6, &height, G_IM_SIZ_16b, 1, &stride);
	check(lastRead.x0 == 2 && lastRead.y0 == 3 && lastRead.width == 6 && lastRead.height == 10 && lastRead.sync == 1,
		"the read asked for as given");
	check(lastRead.colorFormat == 0x1908 && lastRead.colorFormatBytes == 4, "a 16-bit image in the colour format");
	check(p == gpu + 1 * 8 * 4 && stride == 8 && height == 3, "rows where they lie, past the offset, as many as fit");
	height = 2;
	p = gli_cbr_read_pixels(r, 0, 0, 6, &height, G_IM_SIZ_8b, 0, &stride);
	check(lastRead.colorFormat == 0x1903 && lastRead.colorFormatBytes == 1, "an 8-bit image in the monochrome format");
	check(height == 2, "fewer rows asked than fit, all of them");
	stubStride = 0;
	height = 2;
	gli_cbr_read_pixels(r, 0, 0, 6, &height, G_IM_SIZ_16b, 0, &stride);
	check(height == 0, "no stride, no rows");

	/* float rows as bytes */
	set_formats(GLI_DT_FLOAT);
	f = (float *)gpu;
	for (i = 0; i < 64; ++i)
		f[i] = (float)(i % 4) / 3.0f;
	stubOffset = 1;
	stubStride = 4;
	height = 100;
	p = gli_cbr_read_pixels(r, 0, 0, 2, &height, G_IM_SIZ_32b, 0, &stride);
	check(p == r->pixelData && stride == 2, "float rows in the reader's memory, the width apart");
	check(height == tex.textureBytes / 8, "as many rows as it holds");
	check(p[0] == 0 && p[1] == 85 && p[2] == 170 && p[3] == 255, "each channel times 255");
	f[16] = 0.5f;   /* row 1, the first after the offset */
	height = 1;
	p = gli_cbr_read_pixels(r, 0, 0, 2, &height, G_IM_SIZ_32b, 0, &stride);
	check(p[0] == 127, "the rows from the offset, the source's stride apart");
	height = 1;
	p = gli_cbr_read_pixels(r, 0, 0, 2, &height, G_IM_SIZ_8b, 0, &stride);
	check(p == gpu + 1 * 4 * 1 && stride == 4, "but not an 8-bit image's");

	/* slots */
	stubStride = 4;
	height = 1;
	p = gli_cbr_slot_pixels(r, 0, 2, &height, G_IM_SIZ_32b, &stride);
	check(p == r->pixelData && stride == 2, "a slot's float rows as bytes too");
	set_formats(0x1401);
	p = gli_cbr_slot_pixels(r, 0, 2, &height, G_IM_SIZ_32b, &stride);
	check(p == gpu && stride == 4, "a slot's integer rows where they lie");
	mapNull = 1;
	check(gli_cbr_slot_pixels(r, 0, 2, &height, G_IM_SIZ_32b, &stride) == NULL, "none when the slot maps nothing");
	mapNull = 0;
	/* a float monochrome format is read as it is, for both */
	set_formats(GLI_DT_FLOAT);
	gli_ctx.formats.monochromeType = GLI_DT_FLOAT;
	height = 1;
	p = gli_cbr_slot_pixels(r, 0, 2, &height, G_IM_SIZ_8b, &stride);
	check(p == gpu && stride == 4, "an 8-bit slot of floats not turned into bytes");
	height = 1;
	stubOffset = 0;
	p = gli_cbr_read_pixels(r, 0, 0, 2, &height, G_IM_SIZ_8b, 0, &stride);
	check(p == gpu && stride == 4, "nor an 8-bit read");

	destroyed = 0;
	gli_cbr_delete(r);
	check(destroyed == 1, "deleting lets the backend go once");
	gli_cbr_delete(NULL);
}

/* ---- GL ---- */

static GLuint nextName = 1;
static u32 binds, bindName, buffersMade, storageMade, mapsPersistent, maps, unmaps, readPixelsTo, deletes, deletedSyncs;
static int signalAfter;      /* polls before a fence signals */
static uintptr_t nextSync = 0x100;
static void *lastPixels;

void gli_gl_bind_buffer(void *_cache, u32 _target, u32 _name)
{
	check(_cache == (void *)&binds && _target == GL_PIXEL_PACK_BUFFER, "binds through the cache, the pack buffer");
	binds++;
	bindName = _name;
}
static void APIENTRY gen_buffers(GLsizei _n, GLuint *_b) { GLsizei i; for (i = 0; i < _n; ++i) _b[i] = nextName++; buffersMade += _n; }
static void APIENTRY delete_buffers(GLsizei _n, const GLuint *_b) { (void)_b; deletes += _n; }
static void APIENTRY buffer_storage(GLenum _t, GLsizeiptr _s, const void *_d, GLbitfield _f)
{
	(void)_t; (void)_s; (void)_d;
	check((_f & GL_MAP_PERSISTENT_BIT) != 0 && (_f & GL_MAP_READ_BIT) != 0, "storage persistent and readable");
	storageMade++;
}
static void APIENTRY buffer_data(GLenum _t, GLsizeiptr _s, const void *_d, GLenum _u) { (void)_t; (void)_s; (void)_d; (void)_u; }
static u8 mapped[4][4096];
static void *APIENTRY map_range(GLenum _t, GLintptr _o, GLsizeiptr _l, GLbitfield _a)
{
	(void)_t; (void)_o; (void)_l;
	if (_a & GL_MAP_PERSISTENT_BIT)
		mapsPersistent++;
	return mapped[maps++ & 3];
}
static GLboolean APIENTRY unmap(GLenum _t) { (void)_t; unmaps++; return GL_TRUE; }
static void APIENTRY read_pixels(GLint _x, GLint _y, GLsizei _w, GLsizei _h, GLenum _f, GLenum _t, void *_p)
{
	(void)_x; (void)_y; (void)_w; (void)_h; (void)_f; (void)_t;
	readPixelsTo++;
	lastPixels = _p;
}
static GLsync APIENTRY fence_sync(GLenum _c, GLbitfield _f) { (void)_c; (void)_f; return (GLsync)(nextSync++); }
static void APIENTRY delete_sync(GLsync _s) { (void)_s; deletedSyncs++; }
static u32 clientWaits;
static GLenum APIENTRY client_wait(GLsync _s, GLbitfield _f, GLuint64 _t)
{
	(void)_s; (void)_f; (void)_t;
	clientWaits++;
	if (signalAfter > 0) {
		signalAfter--;
		return GL_TIMEOUT_EXPIRED;
	}
	return GL_ALREADY_SIGNALED;
}

PFNGLGENBUFFERSPROC g_glGenBuffers = gen_buffers;
PFNGLDELETEBUFFERSPROC g_glDeleteBuffers = delete_buffers;
PFNGLBUFFERSTORAGEPROC g_glBufferStorage = buffer_storage;
PFNGLBUFFERDATAPROC g_glBufferData = buffer_data;
PFNGLMAPBUFFERRANGEPROC g_glMapBufferRange = map_range;
PFNGLUNMAPBUFFERPROC g_glUnmapBuffer = unmap;
PFNGLREADPIXELSPROC g_glReadPixels = read_pixels;
PFNGLFENCESYNCPROC g_glFenceSync = fence_sync;
PFNGLDELETESYNCPROC g_glDeleteSync = delete_sync;
PFNGLCLIENTWAITSYNCPROC g_glClientWaitSync = client_wait;

static void test_gl(void)
{
	gli_tex tex;
	gli_cbr *r;
	u32 height, stride, n;
	const u8 *p;

	memset(&tex, 0, sizeof(tex));
	tex.width = 8;
	tex.textureBytes = 1024;
	set_formats(0x1401);

	for (n = 0; n <= 4; ++n) {
		config.frameBufferEmulation.copyToRDRAM = n;
		r = gli_gl_cbr_new_pixel_buffer(&tex, &binds);
		check(r->numSlots == (n < 1 ? 1 : (n > 3 ? 3 : n)), "one to three pixel buffers, as copyToRDRAM asks");
		gli_cbr_delete(r);
	}

	config.frameBufferEmulation.copyToRDRAM = 2;
	buffersMade = deletes = 0;
	r = gli_gl_cbr_new_buffer_storage(&tex, &binds);
	check(buffersMade == 2 && storageMade == 2 && mapsPersistent == 2 && bindName == 0,
		"buffer storage: each buffer made, mapped once, and let go of");
	r->ops->read_to_slot(r, 1, 0, 0, 4, G_IM_SIZ_32b);
	check(readPixelsTo == 1 && lastPixels == NULL && bindName == 0, "a slot read into its buffer, which is let go of");
	signalAfter = 4;
	clientWaits = 0;
	check(r->ops->slot_ready(r, 1, 0) == 0 && clientWaits == 1, "a poll before the fence signals: not ready, no wait");
	deletedSyncs = 0;
	check(r->ops->slot_ready(r, 1, 1) == 1 && signalAfter == 0 && clientWaits == 5, "a wait: until it does, however long");
	check(deletedSyncs == 1, "the fence deleted once seen");
	clientWaits = 0;
	check(r->ops->slot_ready(r, 1, 0) == 1 && clientWaits == 0 && deletedSyncs == 1, "then ready with no fence to ask");
	height = 4;
	p = gli_cbr_slot_pixels(r, 1, 8, &height, G_IM_SIZ_32b, &stride);
	check(p == mapped[1] && stride == 8 && maps == 2, "a slot's rows in its persistent mapping, mapped no more");
	signalAfter = 1;
	height = 4;
	p = gli_cbr_read_pixels(r, 0, 0, 8, &height, G_IM_SIZ_32b, 1, &stride);
	check(p == mapped[0] && signalAfter == 0, "a read waits for its fence, in buffer 0's mapping");
	gli_cbr_delete(r);
	check(deletes == 2, "its buffers deleted");

	maps = unmaps = 0;
	r = gli_gl_cbr_new_pixel_buffer(&tex, &binds);
	r->ops->read_to_slot(r, 0, 0, 0, 4, G_IM_SIZ_32b);
	height = 4;
	p = gli_cbr_slot_pixels(r, 0, 8, &height, G_IM_SIZ_32b, &stride);
	check(p == mapped[0] && maps == 1 && bindName != 0, "pixel buffers: a slot mapped as it is read, bound");
	r->ops->clean_up(r);
	check(unmaps == 1 && bindName == 0, "unmapped and let go of on clean-up");
	r->ops->clean_up(r);
	check(unmaps == 1, "but only once");
	gli_cbr_delete(r);

	r = gli_gl_cbr_new_read_pixels(&tex);
	check(r->numSlots == 0, "glReadPixels: no slots");
	height = 4;
	p = gli_cbr_read_pixels(r, 0, 0, 8, &height, G_IM_SIZ_32b, 1, &stride);
	check(p == r->pixelData && lastPixels == r->pixelData && stride == 8, "read into the reader's own memory");
	gli_cbr_delete(r);
}

/* ---- Vulkan ---- */

static u32 readbacks, readbacksDeleted, readbackWaits;
static u8 rbData[8][64];
struct vkr_readback { int index; };
static struct vkr_readback rbs[8];

vkr_readback *vkr_readback_new(vkr_renderer *_r, size_t _bytes) { (void)_r; (void)_bytes; rbs[readbacks].index = (int)readbacks; return &rbs[readbacks++]; }
void vkr_readback_delete(vkr_renderer *_r, vkr_readback *_rb) { (void)_r; (void)_rb; readbacksDeleted++; }
void vkr_readback_tex(vkr_renderer *_r, vkr_readback *_rb, unsigned _t, int _x, int _y, unsigned _w, unsigned _h)
{
	(void)_r; (void)_rb; (void)_t; (void)_x; (void)_y; (void)_w; (void)_h;
}
int vkr_readback_ready(vkr_renderer *_r, vkr_readback *_rb, int _wait) { (void)_r; (void)_rb; readbackWaits += _wait; return 1; }
const void *vkr_readback_data(vkr_readback *_rb) { return rbData[_rb->index]; }

static void test_vk(void)
{
	gli_tex tex;
	gli_cbr *r;
	u32 height, stride;
	const u8 *p;
	memset(&tex, 0, sizeof(tex));
	tex.width = 8;
	tex.textureBytes = 64;
	set_formats(0x1401);
	config.frameBufferEmulation.copyToRDRAM = 12;
	readbacks = readbacksDeleted = 0;
	r = gli_vk_cbr_new((vkr_renderer *)&tex, &tex);
	check(r->numSlots == 8 && readbacks == 8, "Vulkan: at most eight readbacks");
	height = 1;
	p = gli_cbr_slot_pixels(r, 5, 8, &height, G_IM_SIZ_32b, &stride);
	check(p == rbData[5] && stride == 8, "a slot's rows in its readback");
	readbackWaits = 0;
	height = 1;
	p = gli_cbr_read_pixels(r, 0, 0, 8, &height, G_IM_SIZ_32b, 1, &stride);
	check(p == rbData[0] && readbackWaits == 1, "a read waits for readback 0");
	gli_cbr_delete(r);
	check(readbacksDeleted == 8, "each readback deleted");
	config.frameBufferEmulation.copyToRDRAM = 0;
	readbacks = 0;
	r = gli_vk_cbr_new((vkr_renderer *)&tex, &tex);
	check(r->numSlots == 1, "at least one");
	gli_cbr_delete(r);
}

int main(void)
{
	test_shared();
	test_gl();
	test_vk();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("color buffer readers: ok\n");
	return 0;
}
