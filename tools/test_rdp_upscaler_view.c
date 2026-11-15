/* test_rdp_upscaler_view -- the temporal upscaler's view of the RDP
 * (parallel-rdp/rdp_upscaler_view.c).
 *
 * - a triangle's flags, from the cycle type, the depth state, the
 *   coverage, the command and the perspective bit;
 * - its signature, from the command, the other modes, the combiner, the
 *   words of its own tile and the last load's address, and nothing else;
 * - a triangle handed over from its setup does to the upscaler, and is
 *   jittered back into the setup, as rdp_upscaler_triangle with those
 *   flags and that signature does: the same motion vector pass, the same
 *   edges and attributes, the rest untouched.
 *
 * - the scanout: nothing to show drops the targets, a frame shown again
 *   keeps them, and only a new frame resolves the triangles and draws
 *   its motion vectors and depth, in one submission, at the scale; a
 *   repeated frame or none submits nothing.
 *
 * The VI's scanout, the motion pass and the device are stand-ins here
 * (they are checked in test_rdp_video_interface.c and
 * test_rdp_upscaler_pass.c). Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp_upscaler_view.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_upscaler_view: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define FB 0x100000u
#define SCALE 2u

/* ---- stand-ins for the VI, the motion pass and the device ---- */
static char vi_image_storage, cb_storage, motion_storage, depth_storage;
static granite_image_t *vi_image;
static bool vi_repeat;
static unsigned vi_scale, vi_calls;
static rdp_upscaler_framebuffer_t vi_fb;
static unsigned requests, submits, passes, pass_width, pass_height;
static granite_command_buffer_t *submitted, *passed;
static bool no_command_buffer;
static rdp_upscaler_geometry_t pass_geometry;

granite_image_t *rdp_vi_upscaler_scanout(rdp_vi_t *vi, unsigned scaling_factor,
		rdp_upscaler_framebuffer_t *fb, bool *repeat)
{
	(void)vi;
	vi_calls++;
	vi_scale = scaling_factor;
	*fb = vi_fb;
	*repeat = vi_repeat;
	return vi_image;
}

void rdp_upscaler_motion_pass(granite_device_t *device, granite_command_buffer_t *cb,
		const rdp_upscaler_geometry_t *geometry, unsigned width, unsigned height,
		rdp_upscaler_targets_t *targets)
{
	(void)device;
	passes++;
	passed = cb;
	pass_width = width;
	pass_height = height;
	pass_geometry = *geometry;
	targets->motion_vectors = (granite_image_t *)(void *)&motion_storage;
	targets->depth = (granite_image_t *)(void *)&depth_storage;
	targets->jitter_x = geometry->jitter_x;
	targets->jitter_y = geometry->jitter_y;
	targets->reset = geometry->reset;
}

granite_command_buffer_t *granite_device_request_command_buffer(granite_device_t *d, unsigned type)
{
	(void)d;
	(void)type;
	requests++;
	return no_command_buffer ? NULL : (granite_command_buffer_t *)(void *)&cb_storage;
}

void granite_device_submit(granite_device_t *d, granite_command_buffer_t *cb, granite_fence_t **fence,
		unsigned semaphore_count, granite_semaphore_t **semaphores)
{
	(void)d;
	(void)fence;
	(void)semaphore_count;
	(void)semaphores;
	submits++;
	submitted = cb;
}

static unsigned flags_of(unsigned op, uint32_t raster, uint32_t depth, unsigned z_mode, bool prim_depth)
{
	rdp_static_state_t s;
	rdp_depth_blend_state_t d;
	memset(&s, 0, sizeof(s));
	memset(&d, 0, sizeof(d));
	s.flags = raster;
	d.flags = depth;
	d.z_mode = (uint8_t)z_mode;
	return rdp_upscaler_view_flags(op, &s, &d, prim_depth);
}

static void test_flags(void)
{
	const uint32_t test_update = DEPTH_BLEND_DEPTH_TEST_BIT | DEPTH_BLEND_DEPTH_UPDATE_BIT;

	CHECK(flags_of(0x08, 0, 0, 0, false) == 0, "a flat triangle with nothing set has no flags");
	CHECK(flags_of(0x08, 0, DEPTH_BLEND_DEPTH_TEST_BIT, 0, false) == RDP_UPSCALER_DEPTH_TEST, "depth test");
	CHECK(flags_of(0x08, 0, test_update, 0, false) == (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE),
			"depth test and update");
	CHECK(flags_of(0x08, 0, test_update | DEPTH_BLEND_FORCE_BLEND_BIT, 0, false)
			== (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE),
			"blended while writing depth is not translucent");
	CHECK(flags_of(0x08, 0, DEPTH_BLEND_DEPTH_TEST_BIT | DEPTH_BLEND_FORCE_BLEND_BIT, RDP_Z_MODE_TRANSPARENT, false)
			== (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_TRANSLUCENT), "blended without writing depth is translucent");
	CHECK(flags_of(0x08, 0, DEPTH_BLEND_FORCE_BLEND_BIT, RDP_Z_MODE_OPAQUE, false) == RDP_UPSCALER_TRANSLUCENT,
			"in any z mode but decal");
	CHECK(flags_of(0x08, 0, DEPTH_BLEND_DEPTH_TEST_BIT | DEPTH_BLEND_FORCE_BLEND_BIT, RDP_Z_MODE_DECAL, false)
			== RDP_UPSCALER_DEPTH_TEST, "a blended decal moves with its surface");
	CHECK(flags_of(0x08, RASTERIZATION_FILL_BIT, test_update | DEPTH_BLEND_FORCE_BLEND_BIT, 0, false)
			== RDP_UPSCALER_ANY_COVERAGE, "a fill cycle leaves depth alone and covers any pixel it touches");
	CHECK(flags_of(0x08, RASTERIZATION_COPY_BIT, test_update, 0, false) == RDP_UPSCALER_ANY_COVERAGE,
			"so does a copy cycle");
	CHECK(flags_of(0x08, RASTERIZATION_AA_BIT, 0, 0, false) == RDP_UPSCALER_ANY_COVERAGE, "and coverage antialiasing");
	CHECK(flags_of(0x08, 0, 0, 0, true) == RDP_UPSCALER_DEPTH_CONSTANT, "the primitive depth");
	CHECK(flags_of(0x0a, 0, 0, 0, false) == RDP_UPSCALER_TEXTURED, "textured from the command");
	CHECK(flags_of(0x0c, 0, 0, 0, false) == RDP_UPSCALER_SHADED, "shaded from the command");
	CHECK(flags_of(0x09, 0, 0, 0, false) == 0, "a depth block alone is neither");
	CHECK(flags_of(0x0f, RASTERIZATION_PERSPECTIVE_CORRECT_BIT, 0, 0, false)
			== (RDP_UPSCALER_TEXTURED | RDP_UPSCALER_SHADED | RDP_UPSCALER_PERSPECTIVE), "in perspective");
}

/* the signature as the view's words make it */
static uint64_t expected_signature(const rdp_upscaler_view_t *v, unsigned op, unsigned tile)
{
	uint64_t h = rdp_upscaler_hash_init();
	h = rdp_upscaler_hash(h, op);
	h = rdp_upscaler_hash(h, v->other_modes[0]);
	h = rdp_upscaler_hash(h, v->other_modes[1]);
	h = rdp_upscaler_hash(h, v->combine[0]);
	h = rdp_upscaler_hash(h, v->combine[1]);
	h = rdp_upscaler_hash(h, v->tile[tile & 7][0]);
	h = rdp_upscaler_hash(h, v->tile[tile & 7][1]);
	return rdp_upscaler_hash(h, v->load_addr);
}

static void fill_view(rdp_upscaler_view_t *v)
{
	unsigned i;
	memset(v, 0, sizeof(*v));
	v->other_modes[0] = 0xef000cffu;
	v->other_modes[1] = 0x0f0a4000u;
	v->combine[0] = 0xfc121824u;
	v->combine[1] = 0xff33ffffu;
	for (i = 0; i < 8; i++)
	{
		v->tile[i][0] = 0xf5100000u | i;
		v->tile[i][1] = 0x01000000u * i | 0x9000u;
	}
	v->load_addr = 0x123456;
	v->prim_z = 0x10000;
}

static void test_signature(void)
{
	rdp_upscaler_view_t v, w;
	uint64_t base;
	unsigned i;

	fill_view(&v);
	base = rdp_upscaler_view_signature(&v, 0x0e, 3);
	CHECK(base == expected_signature(&v, 0x0e, 3), "the command, the other modes, the combiner, the tile, the load");
	CHECK(rdp_upscaler_view_signature(&v, 0x0e, 3 + 8) == base, "the tile index is 3 bits");
	CHECK(rdp_upscaler_view_signature(&v, 0x0f, 3) != base, "another command is another signature");
	CHECK(rdp_upscaler_view_signature(&v, 0x0e, 4) != base, "and another tile");

#define CHANGED(field, what) \
	do \
	{ \
		w = v; \
		w.field ^= 0x40u; \
		CHECK(rdp_upscaler_view_signature(&w, 0x0e, 3) != base, what); \
	} while (0)
	CHANGED(other_modes[0], "the other modes' first word");
	CHANGED(other_modes[1], "the other modes' second word");
	CHANGED(combine[0], "the combiner's first word");
	CHANGED(combine[1], "the combiner's second word");
	CHANGED(tile[3][0], "the tile's first word");
	CHANGED(tile[3][1], "the tile's second word");
	CHANGED(load_addr, "the load's address");
#undef CHANGED

	for (i = 0; i < 8; i++)
		if (i != 3)
		{
			w = v;
			w.tile[i][0] ^= 1;
			w.tile[i][1] ^= 1;
			CHECK(rdp_upscaler_view_signature(&w, 0x0e, 3) == base, "the other tiles are not in it");
		}
	w = v;
	w.prim_z ^= 0x7fff0000;
	w.prim_depth = !w.prim_depth;
	CHECK(rdp_upscaler_view_signature(&w, 0x0e, 3) == base, "nor the primitive depth");
}

/* ---- the triangle, beside rdp_upscaler_triangle ---- */

/* corners (100, 40), (60, 90), (150, 140), plus @dx native pixels */
static void make_setup(rdp_triangle_setup_t *setup, rdp_attribute_setup_t *attr, int dx)
{
	int32_t *a = (int32_t *)attr;
	unsigned i;
	memset(setup, 0, sizeof(*setup));
	setup->yh = 40 * 4;
	setup->ym = 90 * 4;
	setup->yl = 140 * 4;
	setup->dxhdy = (int32_t)((150 - 100) * 32768 / (100 * 4));
	setup->dxmdy = (int32_t)((60 - 100) * 32768 / (50 * 4));
	setup->dxldy = (int32_t)((150 - 60) * 32768 / (50 * 4));
	setup->xh = (100 + dx) * 32768;
	setup->xm = (100 + dx) * 32768;
	setup->xl = (60 + dx) * 32768;
	setup->flags = TRIANGLE_SETUP_FLIP_BIT;
	setup->tile = 3;
	for (i = 0; i < sizeof(*attr) / sizeof(int32_t); i++)
		a[i] = (int32_t)(0x10000 * (i + 1) + 0x1234 * i);
}

static void to_triangle(const rdp_triangle_setup_t *s, const rdp_attribute_setup_t *a, rdp_upscaler_triangle_t *t)
{
	t->xh = s->xh; t->xm = s->xm; t->xl = s->xl;
	t->dxhdy = s->dxhdy; t->dxmdy = s->dxmdy; t->dxldy = s->dxldy;
	t->yh = s->yh; t->ym = s->ym; t->yl = s->yl;
	t->attr[0] = a->r; t->attr[1] = a->g; t->attr[2] = a->b; t->attr[3] = a->a;
	t->attr[4] = a->s; t->attr[5] = a->t; t->attr[6] = a->w; t->attr[7] = a->z;
	t->attr_de[0] = a->drde; t->attr_de[1] = a->dgde; t->attr_de[2] = a->dbde; t->attr_de[3] = a->dade;
	t->attr_de[4] = a->dsde; t->attr_de[5] = a->dtde; t->attr_de[6] = a->dwde; t->attr_de[7] = a->dzde;
	t->attr_dx[0] = a->drdx; t->attr_dx[1] = a->dgdx; t->attr_dx[2] = a->dbdx; t->attr_dx[3] = a->dadx;
	t->attr_dx[4] = a->dsdx; t->attr_dx[5] = a->dtdx; t->attr_dx[6] = a->dwdx; t->attr_dx[7] = a->dzdx;
}

static void scanout(rdp_upscaler_t *u, rdp_upscaler_geometry_t *g)
{
	rdp_upscaler_framebuffer_t fb;
	fb.origin = FB;
	fb.stride = 320;
	fb.size_log2 = 1;
	fb.width = 320;
	fb.height = 240;
	rdp_upscaler_resolve(u, &fb, SCALE, g);
	rdp_upscaler_configure(u, true, 8, false);
}

static rdp_upscaler_t *shown(void)
{
	rdp_upscaler_t *u = rdp_upscaler_new();
	rdp_upscaler_geometry_t g;
	rdp_upscaler_set_color_image(u, FB, 320, 1);
	rdp_upscaler_configure(u, true, 8, false);
	scanout(u, &g);
	return u;
}

static bool same_geometry(const rdp_upscaler_geometry_t *a, const rdp_upscaler_geometry_t *b)
{
	return a->num_vertices == b->num_vertices && a->num_draws == b->num_draws && a->jitter_x == b->jitter_x
			&& a->jitter_y == b->jitter_y && a->reset == b->reset && a->farthest == b->farthest
			&& !memcmp(a->vertices, b->vertices, a->num_vertices * sizeof(*a->vertices))
			&& !memcmp(a->draws, b->draws, a->num_draws * sizeof(*a->draws));
}

/* Two frames of the triangle, moved by 6 pixels, through the view on
 * one upscaler and as rdp_upscaler_triangle on the other, its signature
 * changed by @sig_xor in the second frame, its flags by @flags_xor and
 * the primitive depth by @z_xor:
 * whether the passes are the same */
static bool same_as_direct(unsigned op, bool prim_depth, uint64_t sig_xor, unsigned flags_xor, int32_t z_xor)
{
	rdp_upscaler_t *u = shown(), *direct = shown();
	rdp_upscaler_geometry_t g, h;
	rdp_upscaler_view_t v;
	rdp_static_state_t s;
	rdp_depth_blend_state_t d;
	rdp_triangle_setup_t setup, before;
	rdp_attribute_setup_t attr, attr_before;
	rdp_upscaler_triangle_t tri;
	bool same = true;
	unsigned frame;

	fill_view(&v);
	v.prim_depth = prim_depth;
	memset(&s, 0, sizeof(s));
	memset(&d, 0, sizeof(d));
	s.flags = RASTERIZATION_PERSPECTIVE_CORRECT_BIT;
	d.flags = DEPTH_BLEND_DEPTH_TEST_BIT | DEPTH_BLEND_DEPTH_UPDATE_BIT;

	for (frame = 0; frame < 2; frame++)
	{
		make_setup(&setup, &attr, (int)frame * 6);
		before = setup;
		attr_before = attr;
		rdp_upscaler_view_triangle(u, &v, op, &s, &d, SCALE, &setup, &attr);

		to_triangle(&before, &attr_before, &tri);
		rdp_upscaler_triangle(direct, &tri, expected_signature(&v, op, before.tile) ^ (frame ? sig_xor : 0),
				rdp_upscaler_view_flags(op, &s, &d, prim_depth) ^ flags_xor, v.prim_z ^ z_xor, SCALE);

		same = same && setup.xh == tri.xh && setup.xm == tri.xm && setup.xl == tri.xl
				&& attr.r == tri.attr[0] && attr.g == tri.attr[1] && attr.b == tri.attr[2] && attr.a == tri.attr[3]
				&& attr.s == tri.attr[4] && attr.t == tri.attr[5] && attr.w == tri.attr[6] && attr.z == tri.attr[7];
		CHECK(setup.yh == before.yh && setup.ym == before.ym && setup.yl == before.yl
				&& setup.dxhdy == before.dxhdy && setup.dxmdy == before.dxmdy && setup.dxldy == before.dxldy
				&& setup.flags == before.flags && setup.tile == before.tile,
				"the jitter leaves the slopes, heights, flags and tile");
		CHECK(!memcmp(&attr.drdx, &attr_before.drdx, 12 * sizeof(int32_t))
				&& !memcmp(&attr.dsdx, &attr_before.dsdx, 12 * sizeof(int32_t)),
				"and the attributes' slopes");
		CHECK(frame == 0 || setup.xh != before.xh, "(the triangle is jittered)");

		scanout(u, &g);
		scanout(direct, &h);
		same = same && same_geometry(&g, &h);
	}
	CHECK(g.num_vertices >= 3, "(the triangle is in the pass)");
	rdp_upscaler_free(u);
	rdp_upscaler_free(direct);
	return same;
}

static void test_triangle(void)
{
	rdp_upscaler_t *u = rdp_upscaler_new();
	rdp_upscaler_view_t v;
	rdp_static_state_t s;
	rdp_depth_blend_state_t d;
	rdp_triangle_setup_t setup, before;
	rdp_attribute_setup_t attr, attr_before;

	fill_view(&v);
	memset(&s, 0, sizeof(s));
	memset(&d, 0, sizeof(d));
	make_setup(&setup, &attr, 0);
	before = setup;
	attr_before = attr;
	rdp_upscaler_set_color_image(u, FB, 320, 1);
	rdp_upscaler_view_triangle(u, &v, 0x0e, &s, &d, SCALE, &setup, &attr);
	CHECK(!memcmp(&setup, &before, sizeof(setup)) && !memcmp(&attr, &attr_before, sizeof(attr)),
			"while the upscaler is off a triangle is left as it is");
	rdp_upscaler_view_triangle(NULL, &v, 0x0e, &s, &d, SCALE, &setup, &attr);
	CHECK(!memcmp(&setup, &before, sizeof(setup)), "and without one");
	rdp_upscaler_free(u);

	CHECK(same_as_direct(0x0e, false, 0, 0, 0), "a textured, shaded triangle is what rdp_upscaler_triangle makes of it");
	CHECK(same_as_direct(0x08, false, 0, 0, 0), "and a flat one");
	CHECK(same_as_direct(0x0f, true, 0, 0, 0), "and one at the primitive depth");
	/* (that the comparison sees the signature, the flags and the depth) */
	CHECK(!same_as_direct(0x0e, false, 1, 0, 0), "with another signature it would not be");
	CHECK(!same_as_direct(0x0e, false, 0, RDP_UPSCALER_DEPTH_WRITE, 0), "nor with other flags");
	CHECK(!same_as_direct(0x0f, true, 0, 0, 0x01000000), "nor at another primitive depth");
}


/* ---- the scanout ---- */
static granite_image_t *view_scanout(rdp_upscaler_t *u, bool reset, rdp_upscaler_targets_t *targets,
		enum rdp_upscaler_frame *frame)
{
	requests = submits = passes = vi_calls = 0;
	submitted = passed = NULL;
	memset(targets, 0, sizeof(*targets));
	return rdp_upscaler_view_scanout(u, (rdp_vi_t *)(void *)&vi_image_storage,
			(granite_device_t *)(void *)&cb_storage, SCALE, 8, reset, targets, frame);
}

static void test_scanout(void)
{
	rdp_upscaler_t *u = rdp_upscaler_new();
	rdp_upscaler_targets_t targets;
	rdp_upscaler_view_t v;
	rdp_static_state_t s;
	rdp_depth_blend_state_t d;
	rdp_triangle_setup_t setup;
	rdp_attribute_setup_t attr;
	enum rdp_upscaler_frame frame;
	granite_image_t *image;

	vi_fb.origin = FB;
	vi_fb.stride = 320;
	vi_fb.size_log2 = 1;
	vi_fb.width = 320;
	vi_fb.height = 240;
	rdp_upscaler_set_color_image(u, FB, 320, 1);

	/* nothing to show */
	vi_image = NULL;
	vi_repeat = false;
	image = view_scanout(u, false, &targets, &frame);
	CHECK(!image && frame == RDP_UPSCALER_FRAME_NONE && vi_calls == 1 && vi_scale == SCALE,
			"with nothing to show there is no frame");
	CHECK(requests == 0 && submits == 0 && passes == 0, "and nothing is submitted");
	CHECK(rdp_upscaler_is_active(u), "the upscaler is on from the first scanout");

	/* a new frame */
	vi_image = (granite_image_t *)(void *)&vi_image_storage;
	image = view_scanout(u, false, &targets, &frame);
	CHECK(image == vi_image && frame == RDP_UPSCALER_FRAME_NEW, "a new frame");
	CHECK(requests == 1 && passes == 1 && submits == 1 && passed == submitted && submitted != NULL,
			"draws its motion vectors and depth in one submission");
	CHECK(pass_width == 320 * SCALE && pass_height == 240 * SCALE, "at the scale");
	CHECK(targets.motion_vectors == (granite_image_t *)(void *)&motion_storage
			&& targets.depth == (granite_image_t *)(void *)&depth_storage, "into the targets");

	/* the triangles of the next are in its pass */
	fill_view(&v);
	memset(&s, 0, sizeof(s));
	memset(&d, 0, sizeof(d));
	make_setup(&setup, &attr, 0);
	rdp_upscaler_view_triangle(u, &v, 0x0c, &s, &d, SCALE, &setup, &attr);
	image = view_scanout(u, false, &targets, &frame);
	CHECK(frame == RDP_UPSCALER_FRAME_NEW && pass_geometry.num_vertices >= 3 && !pass_geometry.reset,
			"the frame's triangles are resolved into its pass");
	CHECK(pass_geometry.jitter_x != 0.0f || pass_geometry.jitter_y != 0.0f, "with its jitter");

	/* shown again */
	vi_repeat = true;
	image = view_scanout(u, false, &targets, &frame);
	CHECK(image == vi_image && frame == RDP_UPSCALER_FRAME_REPEAT, "a frame shown again");
	CHECK(requests == 0 && submits == 0 && passes == 0 && !targets.motion_vectors,
			"submits nothing and leaves the targets to the caller");
	vi_repeat = false;

	/* a reset */
	image = view_scanout(u, true, &targets, &frame);
	CHECK(frame == RDP_UPSCALER_FRAME_NEW && pass_geometry.reset && targets.reset, "a reset starts the history over");

	/* out of memory for the command buffer */
	no_command_buffer = true;
	image = view_scanout(u, false, &targets, &frame);
	CHECK(frame == RDP_UPSCALER_FRAME_NEW && requests == 1 && passes == 0 && submits == 0 && !targets.motion_vectors,
			"without a command buffer a frame has no targets");
	no_command_buffer = false;
	rdp_upscaler_free(u);
}

int main(void)
{
	test_flags();
	test_signature();
	test_triangle();
	test_scanout();
	if (failures)
	{
		fprintf(stderr, "test_rdp_upscaler_view: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_upscaler_view: ok\n");
	return 0;
}
