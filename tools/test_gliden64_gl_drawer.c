/* test_gliden64_gl_drawer -- GLideN64's GL drawers
 * (Graphics/OpenGLContext/opengl_BufferedDrawer.c and
 * opengl_UnbufferedDrawer.c) on a stub GL that runs each draw: what it
 * reads, through the vertex arrays turned on and where they point, must be
 * the vertices and elements the draw was given.
 *
 * The buffered drawer: triangles (with and without elements, flat colours
 * or not), rects with and without texture coordinates and lines, on rings
 * small enough to go round many times, persistently mapped with buffer
 * storage and mapped a write at a time, unsynchronized, without it; a
 * segment fenced when the writes leave it and waited for, the fence
 * dropped, when they come back; nothing mapped at a draw without buffer
 * storage. Without it a rect drawn again in the same lap is drawn from
 * where it was put, uploading nothing, and not once the ring has come
 * round, nor past the most its table holds. Lighting's count set as the
 * first vertex has it, only where hardware lighting is allowed. Dropped,
 * it leaves no buffer, array or fence.
 *
 * The unbuffered drawer: the arrays the program reads turned on and the
 * rest off, pointed at the vertices only when they move, and turned on or
 * off only when they change.
 *
 *   ./test_gliden64_gl_drawer
 */
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_Attributes.h"
#include "Graphics/OpenGLContext/opengl_Drawer.h"
#include "gli_combiner.h"
#include "gSP.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- what the drawers call beside GL ---- */

static int hwLighting, crcCollide;
bool gli_isHWLightingAllowed(void) { return hwLighting != 0; }

u64 CRC_Calculate(u64 _crc, const void *_buffer, u32 _count)
{
	const u8 *p = (const u8 *)_buffer;
	u32 i;
	for (i = 0; i < _count; ++i)
		_crc = (_crc ^ p[i]) * 1099511628211ull;
	/* every rect to the same slot, the table's chains as long as they go */
	if (crcCollide) {
		const u32 h = (u32)_crc;
		_crc = ((u64)h << 32) | h;
	}
	return _crc;
}

/* ---- a GL that runs the draws ---- */

#define BUFFERS 16
#define ARRAYS 8

typedef struct attrib_state
{
	int on;
	GLuint buffer;
	const void *ptr;
	GLint size;
	GLenum type;
	GLboolean norm;
	GLsizei stride;
} attrib_state;

typedef struct vao_state
{
	attrib_state attribs[MaxAttribIndex];
	GLuint elements;
} vao_state;

static u8 *store[BUFFERS];
static u32 storeSize[BUFFERS];
static int alive[BUFFERS];
static GLuint nextBuffer = 1, nextArray = 1;
static GLuint boundArray, boundElements;
static vao_state vaos[ARRAYS];
static int vaoAlive[ARRAYS];
static GLuint vao;
static u32 persistentMaps, writeMaps, unmaps, mappedNow, storages, datas;
static u32 enables, disables, pointers, flushes;
static u32 fencesMade, fencesLive, waits, timeouts;
static GLintptr mapOffset;
static GLsizeiptr mapLength;
static GLbitfield mapBits;
static void *bindCache = &bindCache;
static int badCache;

void gli_gl_bind_buffer(void *_cache, u32 _target, u32 _name)
{
	if (_cache != bindCache)
		badCache = 1;
	if (_target == GL_ARRAY_BUFFER)
		boundArray = _name;
	else if (_target == GL_ELEMENT_ARRAY_BUFFER) {
		boundElements = _name;
		vaos[vao].elements = _name;
	}
}

static GLuint target_buffer(GLenum _target)
{
	return _target == GL_ARRAY_BUFFER ? boundArray : vaos[vao].elements;
}

static void APIENTRY gen_buffers(GLsizei _n, GLuint *_b)
{
	GLsizei i;
	for (i = 0; i < _n; ++i) {
		alive[nextBuffer] = 1;
		_b[i] = nextBuffer++;
	}
}
static void APIENTRY delete_buffers(GLsizei _n, const GLuint *_b)
{
	GLsizei i;
	for (i = 0; i < _n; ++i) {
		alive[_b[i]] = 0;
		free(store[_b[i]]);
		store[_b[i]] = NULL;
	}
}
static void make_store(GLenum _target, GLsizeiptr _size)
{
	const GLuint b = target_buffer(_target);
	free(store[b]);
	store[b] = (u8 *)calloc(1, (size_t)_size);
	storeSize[b] = (u32)_size;
}
static void APIENTRY buffer_storage(GLenum _t, GLsizeiptr _s, const void *_d, GLbitfield _f)
{
	(void)_d;
	check(_f == (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT), "storage written, persistent, coherent");
	make_store(_t, _s);
	storages++;
}
static void APIENTRY buffer_data(GLenum _t, GLsizeiptr _s, const void *_d, GLenum _u)
{
	check(_d == NULL && _u == GL_DYNAMIC_DRAW, "a dynamic buffer, its data to come");
	make_store(_t, _s);
	datas++;
}
static void *APIENTRY map_range(GLenum _t, GLintptr _o, GLsizeiptr _l, GLbitfield _a)
{
	const GLuint b = target_buffer(_t);
	if (_a & GL_MAP_PERSISTENT_BIT) {
		check(_o == 0 && (u32)_l == storeSize[b], "the whole buffer mapped for good");
		persistentMaps++;
	} else {
		check(_o >= 0 && (u32)(_o + _l) <= storeSize[b], "a write's range in the buffer");
		writeMaps++;
		mappedNow++;
	}
	mapOffset = _o;
	mapLength = _l;
	mapBits = _a;
	return store[b] + _o;
}
static GLboolean APIENTRY unmap(GLenum _t) { (void)_t; unmaps++; mappedNow--; return GL_TRUE; }
static void APIENTRY flush_range(GLenum _t, GLintptr _o, GLsizeiptr _l) { (void)_t; (void)_o; (void)_l; flushes++; }
static GLsync APIENTRY fence_sync(GLenum _c, GLbitfield _f)
{
	check(_c == GL_SYNC_GPU_COMMANDS_COMPLETE && _f == 0, "a fence on the commands so far");
	fencesMade++;
	fencesLive++;
	return (GLsync)(size_t)(0x100 + fencesMade);
}
static u8 timedOut[0x10000];  /* a fence's last wait timed out */
#define SYNC_SLOT(_s) (((size_t)(_s) - 0x100) & 0xFFFF)
static void APIENTRY delete_sync(GLsync _s)
{
	check(_s != 0, "a fence deleted is one");
	check(!timedOut[SYNC_SLOT(_s)], "a fence waited for deleted once it signals");
	fencesLive--;
}
static GLenum APIENTRY client_wait(GLsync _s, GLbitfield _f, GLuint64 _t)
{
	GLenum res;
	waits++;
	/* the first poll flushes and does not wait; then it times out once */
	if (_f == GL_SYNC_FLUSH_COMMANDS_BIT && _t == 0)
		res = GL_TIMEOUT_EXPIRED;
	else
		res = timeouts++ % 2 == 0 ? GL_TIMEOUT_EXPIRED : GL_CONDITION_SATISFIED;
	timedOut[SYNC_SLOT(_s)] = res == GL_TIMEOUT_EXPIRED;
	return res;
}
static void APIENTRY gen_arrays(GLsizei _n, GLuint *_a)
{
	GLsizei i;
	for (i = 0; i < _n; ++i) {
		memset(&vaos[nextArray], 0, sizeof(vaos[0]));
		vaoAlive[nextArray] = 1;
		_a[i] = nextArray++;
	}
}
static u32 vaoBinds;
static void APIENTRY bind_array(GLuint _a) { vao = _a; vaoBinds++; }
static void APIENTRY delete_arrays(GLsizei _n, const GLuint *_a)
{
	GLsizei i;
	for (i = 0; i < _n; ++i)
		vaoAlive[_a[i]] = 0;
}
static void APIENTRY enable_attrib(GLuint _i) { vaos[vao].attribs[_i].on = 1; enables++; }
static void APIENTRY disable_attrib(GLuint _i) { vaos[vao].attribs[_i].on = 0; disables++; }
static void APIENTRY attrib_pointer(GLuint _i, GLint _size, GLenum _type, GLboolean _norm, GLsizei _stride, const void *_ptr)
{
	attrib_state *a = &vaos[vao].attribs[_i];
	a->buffer = boundArray;
	a->ptr = _ptr;
	a->size = _size;
	a->type = _type;
	a->norm = _norm;
	a->stride = _stride;
	pointers++;
}
static GLfloat numLights = -1;
static u32 numLightsSet;
static void APIENTRY attrib1f(GLuint _i, GLfloat _v)
{
	check(_i == GLI_TRI_ATTRIB_NUMLIGHTS, "the one attribute set is lighting's count");
	numLights = _v;
	numLightsSet++;
}
static GLfloat lineWidth;
static void APIENTRY line_width(GLfloat _w) { lineWidth = _w; }

/* ---- what a draw read ---- */

#define MAX_READ 4096
static u8 got[MAX_READ][MaxAttribIndex][16];
static int gotOn[MaxAttribIndex];
static u32 gotCount, draws;
static GLint gotFirst;
static GLenum gotMode;

static size_t attrib_bytes(const attrib_state *_a)
{
	return (size_t)_a->size * (_a->type == GL_FLOAT ? 4 : 1);
}

static const u8 *attrib_at(const attrib_state *_a, GLint _vertex)
{
	const u8 *base;
	if (_a->buffer != 0) {
		check(alive[_a->buffer] && store[_a->buffer] != NULL, "an array in a live buffer");
		check((size_t)_a->ptr + (size_t)_vertex * (size_t)_a->stride + attrib_bytes(_a) <= storeSize[_a->buffer],
			"an array read within its buffer");
		base = store[_a->buffer] + (size_t)_a->ptr;
	} else
		base = (const u8 *)_a->ptr;
	return base + (size_t)_vertex * (size_t)_a->stride;
}

static void run_vertex(u32 _k, GLint _vertex)
{
	u32 i;
	for (i = 0; i < MaxAttribIndex; ++i) {
		const attrib_state *a = &vaos[vao].attribs[i];
		if (!a->on)
			continue;
		memcpy(got[_k][i], attrib_at(a, _vertex), attrib_bytes(a));
	}
}

static void begin_draw(GLenum _mode, u32 _count)
{
	u32 i;
	check(mappedNow == 0, "no buffer mapped at a draw");
	check(_count <= MAX_READ, "a draw within what the stub reads");
	for (i = 0; i < MaxAttribIndex; ++i)
		gotOn[i] = vaos[vao].attribs[i].on;
	gotMode = _mode;
	gotCount = _count;
	draws++;
}

static void APIENTRY draw_arrays(GLenum _mode, GLint _first, GLsizei _count)
{
	GLsizei k;
	begin_draw(_mode, (u32)_count);
	check(_first >= 0, "a draw from a vertex in the ring");
	gotFirst = _first;
	for (k = 0; k < _count; ++k)
		run_vertex((u32)k, _first + k);
}

static void draw_indexed(GLenum _mode, GLsizei _count, const GLushort *_indices, GLint _base)
{
	GLsizei k;
	begin_draw(_mode, (u32)_count);
	for (k = 0; k < _count; ++k)
		run_vertex((u32)k, _base + _indices[k]);
}

static const GLushort *element_ptr(const void *_indices, GLsizei _count)
{
	const GLuint b = vaos[vao].elements;
	if (b == 0)
		return (const GLushort *)_indices;
	check((size_t)_indices % 2 == 0 && (size_t)_indices + (size_t)_count * 2 <= storeSize[b], "elements within their buffer");
	return (const GLushort *)(store[b] + (size_t)_indices);
}

static void APIENTRY draw_elements(GLenum _mode, GLsizei _count, GLenum _type, const void *_indices)
{
	check(_type == GL_UNSIGNED_SHORT, "elements as shorts");
	draw_indexed(_mode, _count, element_ptr(_indices, _count), 0);
}

static GLuint rangeStart, rangeEnd;
static void APIENTRY draw_range_base(GLenum _mode, GLuint _start, GLuint _end, GLsizei _count, GLenum _type,
	const void *_indices, GLint _base)
{
	check(_type == GL_UNSIGNED_SHORT, "elements as shorts");
	rangeStart = _start;
	rangeEnd = _end;
	draw_indexed(_mode, _count, element_ptr(_indices, _count), _base);
}

PFNGLGENBUFFERSPROC g_glGenBuffers = gen_buffers;
PFNGLDELETEBUFFERSPROC g_glDeleteBuffers = delete_buffers;
PFNGLBUFFERSTORAGEPROC g_glBufferStorage = buffer_storage;
PFNGLBUFFERDATAPROC g_glBufferData = buffer_data;
PFNGLMAPBUFFERRANGEPROC g_glMapBufferRange = map_range;
PFNGLUNMAPBUFFERPROC g_glUnmapBuffer = unmap;
PFNGLFLUSHMAPPEDBUFFERRANGEPROC g_glFlushMappedBufferRange = flush_range;
PFNGLFENCESYNCPROC g_glFenceSync = fence_sync;
PFNGLDELETESYNCPROC g_glDeleteSync = delete_sync;
PFNGLCLIENTWAITSYNCPROC g_glClientWaitSync = client_wait;
PFNGLGENVERTEXARRAYSPROC g_glGenVertexArrays = gen_arrays;
PFNGLBINDVERTEXARRAYPROC g_glBindVertexArray = bind_array;
PFNGLDELETEVERTEXARRAYSPROC g_glDeleteVertexArrays = delete_arrays;
PFNGLENABLEVERTEXATTRIBARRAYPROC g_glEnableVertexAttribArray = enable_attrib;
PFNGLDISABLEVERTEXATTRIBARRAYPROC g_glDisableVertexAttribArray = disable_attrib;
PFNGLVERTEXATTRIBPOINTERPROC g_glVertexAttribPointer = attrib_pointer;
PFNGLVERTEXATTRIB1FPROC g_glVertexAttrib1f = attrib1f;
PFNGLLINEWIDTHPROC g_glLineWidth = line_width;
PFNGLDRAWARRAYSPROC g_glDrawArrays = draw_arrays;
PFNGLDRAWELEMENTSPROC g_glDrawElements = draw_elements;
PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC g_glDrawRangeElementsBaseVertex = draw_range_base;

/* ---- a program ---- */

static int usesShade, usesTexture, usesTile0, usesTile1;
static bool p_shade(const void *_p) { (void)_p; return usesShade != 0; }
static bool p_texture(const void *_p) { (void)_p; return usesTexture != 0; }
static bool p_tile(const void *_p, u32 _t) { (void)_p; return (_t == 0 ? usesTile0 : usesTile1) != 0; }
static const gli_cprog_ops programOps = { NULL, NULL, NULL, NULL, p_texture, p_tile, p_shade, NULL, NULL, NULL };
static const gli_cprog program = { &programOps, 0 };

/* ---- the vertices ---- */

static unsigned rng = 1;
static u32 next_rand(u32 _n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % _n; }
static f32 rand_f(void) { return (f32)next_rand(100000) / 7.0f; }

static void make_vertices(SPVertex *_v, u32 _count)
{
	u32 i;
	for (i = 0; i < _count; ++i) {
		SPVertex *v = &_v[i];
		v->x = rand_f(); v->y = rand_f(); v->z = rand_f(); v->w = rand_f();
		v->r = rand_f(); v->g = rand_f(); v->b = rand_f(); v->a = rand_f();
		v->flat_r = rand_f(); v->flat_g = rand_f(); v->flat_b = rand_f(); v->flat_a = rand_f();
		v->s = rand_f(); v->t = rand_f();
		v->modify = next_rand(0xFFFFFFu) * 251u;
		v->HWLight = (u8)next_rand(8);
	}
}

static void make_rect(RectVertex *_v, u32 _count)
{
	u32 i;
	for (i = 0; i < _count; ++i) {
		_v[i].x = rand_f(); _v[i].y = rand_f(); _v[i].z = rand_f(); _v[i].w = rand_f();
		_v[i].s0 = rand_f(); _v[i].t0 = rand_f(); _v[i].s1 = rand_f(); _v[i].t1 = rand_f();
	}
}

/* whether draw item @_k read vertex @_v */
static int read_vertex(u32 _k, const SPVertex *_v, int _flat, int _color, int _tex, int _modify)
{
	if (memcmp(got[_k][GLI_TRI_ATTRIB_POSITION], &_v->x, 16) != 0)
		return 0;
	if (_color && memcmp(got[_k][GLI_TRI_ATTRIB_COLOR], _flat ? &_v->flat_r : &_v->r, 16) != 0)
		return 0;
	if (_tex && memcmp(got[_k][GLI_TRI_ATTRIB_TEXCOORD], &_v->s, 8) != 0)
		return 0;
	if (_modify && memcmp(got[_k][GLI_TRI_ATTRIB_MODIFY], &_v->modify, 4) != 0)
		return 0;
	return 1;
}

static int read_rect(const RectVertex *_v, u32 _count, int _tex)
{
	u32 k;
	if (gotCount != _count)
		return 0;
	for (k = 0; k < _count; ++k) {
		if (memcmp(got[k][GLI_RECT_ATTRIB_POSITION], &_v[k].x, 16) != 0)
			return 0;
		if (_tex && (memcmp(got[k][GLI_RECT_ATTRIB_TEXCOORD0], &_v[k].s0, 8) != 0 ||
			memcmp(got[k][GLI_RECT_ATTRIB_TEXCOORD1], &_v[k].s1, 8) != 0))
			return 0;
	}
	return 1;
}

#define BIT(_a) (1u << (_a))
#define TRIS_ALL (BIT(GLI_TRI_ATTRIB_POSITION) | BIT(GLI_TRI_ATTRIB_COLOR) | BIT(GLI_TRI_ATTRIB_TEXCOORD) | BIT(GLI_TRI_ATTRIB_MODIFY))

/* whether the arrays on at the draw were @_mask's */
static int on(u32 _mask)
{
	u32 i;
	for (i = 0; i < MaxAttribIndex; ++i)
		if (gotOn[i] != ((_mask & BIT(i)) != 0))
			return 0;
	return 1;
}

static SPVertex verts[4096];
static u16 elems[1024];

static void reset_gl(void)
{
	vao = 0;
	boundArray = boundElements = 0;
	memset(&vaos[0], 0, sizeof(vaos[0]));
	persistentMaps = writeMaps = unmaps = mappedNow = storages = datas = 0;
	enables = disables = pointers = flushes = 0;
	fencesMade = waits = 0;
	numLightsSet = 0;
}

/* a run of triangle draws, of @_n vertices at most, each checked */
static void draw_tris_run(gli_gl_drawer *_d, u32 _draws, u32 _n, const char *_what)
{
	u32 j, k, bad = 0;
	for (j = 0; j < _draws; ++j) {
		gli_draw_triangles_params p;
		const u32 count = 3 + next_rand(_n - 2);
		const int indexed = next_rand(2) == 0;
		memset(&p, 0, sizeof(p));
		make_vertices(verts, count);
		p.mode = GL_TRIANGLES;
		p.verticesCount = count;
		p.flatColors = next_rand(2) == 0;
		p.vertices = verts;
		p.combiner = &program;
		if (indexed) {
			p.elementsCount = 3 + next_rand(300);
			for (k = 0; k < p.elementsCount; ++k)
				elems[k] = (u16)next_rand(count);
			p.elements = elems;
		}
		_d->ops->draw_triangles(_d, &p);
		if (gotCount != (indexed ? p.elementsCount : count) || gotMode != GL_TRIANGLES)
			bad++;
		else
			for (k = 0; k < gotCount; ++k)
				if (!read_vertex(k, &verts[indexed ? elems[k] : k], p.flatColors, 1, 1, 1)) {
					bad++;
					break;
				}
		if (indexed && (rangeStart != 0 || rangeEnd != count - 1))
			bad++;
	}
	check(bad == 0, _what);
}

static void draw_rects_run(gli_gl_drawer *_d, u32 _draws, const char *_what)
{
	static RectVertex rv[4];
	u32 j, bad = 0;
	for (j = 0; j < _draws; ++j) {
		gli_draw_rects_params p;
		make_rect(rv, 4);
		p.mode = GL_TRIANGLE_STRIP;
		p.texrect = next_rand(2) == 0;
		p.verticesCount = 4;
		p.vertices = rv;
		p.combiner = &program;
		_d->ops->draw_rects(_d, &p);
		if (!read_rect(rv, 4, p.texrect) || gotMode != GL_TRIANGLE_STRIP)
			bad++;
		if (gotOn[GLI_RECT_ATTRIB_TEXCOORD0] != p.texrect || gotOn[GLI_RECT_ATTRIB_TEXCOORD1] != p.texrect)
			bad++;
	}
	check(bad == 0, _what);
}

static void check_released(void)
{
	u32 i;
	int live = 0;
	for (i = 1; i < nextBuffer; ++i)
		live |= alive[i];
	check(!live, "no buffer left");
	live = 0;
	for (i = 1; i < nextArray; ++i)
		live |= vaoAlive[i];
	check(!live, "no vertex array left");
	check(fencesLive == 0, "no fence left");
	check(boundArray == 0 && boundElements == 0, "the buffers unbound");
	check(vao == 0, "the vertex arrays unbound");
}

static gli_gl_info info;
static gli_gl_attribs attribs;

static void test_buffered(int _storage)
{
	gli_gl_drawer *d;
	u32 i, prevWrites;

	reset_gl();
	nextBuffer = nextArray = 1;
	memset(&info, 0, sizeof(info));
	info.bufferStorage = _storage != 0;
	gli_gl_attribs_reset(&attribs);
	d = gli_gl_drawer_new_buffered(&info, &attribs, bindCache);
	check(d != NULL, "a buffered drawer");
	check(!badCache, "binds through the binding cache");
	if (_storage) {
		check(storages == 3 && persistentMaps == 3 && datas == 0, "three rings in storage, mapped once");
	} else {
		check(datas == 3 && storages == 0 && persistentMaps == 0, "three rings of buffer data, not mapped");
	}
	for (i = 1; i <= 3; ++i)
		check(storeSize[i] == 49152, "rings as large as GLIDEN64_RING_BYTES says");

	/* the attributes each array set up with */
	check(vaos[1].attribs[GLI_RECT_ATTRIB_POSITION].size == 4 && vaos[1].attribs[GLI_RECT_ATTRIB_POSITION].type == GL_FLOAT
		&& vaos[1].attribs[GLI_RECT_ATTRIB_POSITION].stride == (GLsizei)sizeof(RectVertex)
		&& vaos[1].attribs[GLI_RECT_ATTRIB_POSITION].buffer == 1, "the rects' positions in their ring");
	check(vaos[2].attribs[GLI_TRI_ATTRIB_MODIFY].size == 4 && vaos[2].attribs[GLI_TRI_ATTRIB_MODIFY].type == GL_BYTE
		&& vaos[2].attribs[GLI_TRI_ATTRIB_MODIFY].norm == GL_TRUE && vaos[2].attribs[GLI_TRI_ATTRIB_MODIFY].stride == 44,
		"the triangles' modify bytes normalized, 44 bytes a vertex");
	check(vaos[2].elements == 3, "the triangles' elements in their ring");
	check(!vaos[2].attribs[GLI_TRI_ATTRIB_NUMLIGHTS].on && vaos[2].attribs[GLI_TRI_ATTRIB_COLOR].on
		&& vaos[2].attribs[GLI_TRI_ATTRIB_TEXCOORD].on, "the triangles' arrays on but lighting's count");

	/* many laps of a 48K ring: draws of up to 256 vertices, 11 KB a draw */
	vaoBinds = 0;
	draw_tris_run(d, 400, 256, _storage ? "triangles read as drawn, round the ring" :
		"triangles read as drawn, round the ring, mapped a write at a time");
	check(vaoBinds == 1, "the triangles' array bound once for a run of them");
	check(fencesMade > 10 && waits > 10, "segments fenced and waited for as the writes go round");
	check(fencesLive <= 9, "a fence a segment at most");
	check(timeouts > 0, "a wait goes on while the fence times out");
	if (!_storage) {
		check(writeMaps > 400 && unmaps == writeMaps, "a map a write, each unmapped");
		check((mapBits & GL_MAP_UNSYNCHRONIZED_BIT) && (mapBits & GL_MAP_WRITE_BIT) && !(mapBits & GL_MAP_PERSISTENT_BIT),
			"written unsynchronized");
	} else
		check(writeMaps == 0, "nothing mapped again with storage");
	check(on(TRIS_ALL), "the triangles' arrays read");

	vaoBinds = 0;
	draw_rects_run(d, 1000, "rects read as drawn, round the ring");
	check(vao == 1 && vaoBinds == 1, "rects from their array, bound once");
	draw_tris_run(d, 3, 100, "triangles after rects");
	check(vao == 2, "triangles from theirs");

	/* lighting's count */
	{
		gli_draw_triangles_params p;
		memset(&p, 0, sizeof(p));
		make_vertices(verts, 3);
		verts[0].HWLight = 5;
		verts[1].HWLight = 2;
		p.mode = GL_TRIANGLES;
		p.verticesCount = 3;
		p.vertices = verts;
		p.combiner = &program;
		hwLighting = 0;
		numLightsSet = 0;
		d->ops->draw_triangles(d, &p);
		check(numLightsSet == 0, "no light count without hardware lighting");
		hwLighting = 1;
		d->ops->draw_triangles(d, &p);
		check(numLightsSet == 1 && numLights == 5.0f, "the first vertex's light count");
		hwLighting = 0;
	}

	/* a line: two vertices, their own colours */
	{
		u32 k, bad = 0;
		make_vertices(verts, 2);
		for (i = 0; i < 100; ++i) {
			d->ops->draw_line(d, 3.5f, verts);
			if (gotMode != GL_LINES || gotCount != 2 || lineWidth != 3.5f)
				bad++;
			for (k = 0; k < 2; ++k)
				if (!read_vertex(k, &verts[k], 0, 1, 1, 1))
					bad++;
		}
		check(bad == 0, "lines read as drawn");
	}

	/* without storage, a rect again in the lap comes from where it was */
	if (!_storage) {
		static RectVertex a[4], b[4];
		gli_draw_rects_params p;
		make_rect(a, 4);
		make_rect(b, 4);
		p.mode = GL_TRIANGLE_STRIP;
		p.texrect = 1;
		p.verticesCount = 4;
		p.combiner = &program;
		p.vertices = a;
		d->ops->draw_rects(d, &p);
		check(mapLength == 4 * (GLsizeiptr)sizeof(RectVertex) && mapOffset == gotFirst * (GLintptr)sizeof(RectVertex),
			"a rect written where it is drawn from, and no more");
		p.vertices = b;
		d->ops->draw_rects(d, &p);
		prevWrites = writeMaps;
		p.vertices = a;
		d->ops->draw_rects(d, &p);
		check(writeMaps == prevWrites && read_rect(a, 4, 1), "a rect drawn again from where it was, uploading nothing");
		p.vertices = b;
		d->ops->draw_rects(d, &p);
		check(writeMaps == prevWrites && read_rect(b, 4, 1), "and the one after it");
		/* round the ring: the rects drawn before are written again */
		draw_rects_run(d, 400, "rects round the ring");
		p.vertices = a;
		prevWrites = writeMaps;
		d->ops->draw_rects(d, &p);
		check(writeMaps == prevWrites + 1 && read_rect(a, 4, 1), "a rect from the last lap uploaded again");
	}

	d->ops->destroy(d);
	check_released();

	/* the rects' table, from a fresh ring: past half full, all dropped,
	 * well before a lap of single vertex rects */
	if (!_storage) {
		static RectVertex a[4], c[1];
		gli_draw_rects_params p;
		u32 bad = 0;
		gli_gl_attribs_reset(&attribs);
		nextBuffer = nextArray = 1;
		d = gli_gl_drawer_new_buffered(&info, &attribs, bindCache);
		make_rect(a, 4);
		p.mode = GL_TRIANGLE_STRIP;
		p.texrect = 0;
		p.verticesCount = 4;
		p.combiner = &program;
		p.vertices = a;
		d->ops->draw_rects(d, &p);
		/* a lap of rects, fewer than the table drops at: once the ring is
		 * round, the rects of the lap before are gone, and the one that
		 * went round is kept, though every rect is in one chain */
		crcCollide = 1;
		{
			static RectVertex lap[4];
			int round = 0, kept = 1;
			for (i = 0; i < 390; ++i) {
				make_rect(lap, 4);
				p.vertices = lap;
				d->ops->draw_rects(d, &p);
				bad += !read_rect(lap, 4, 0);
				if (!round && mapOffset == 0) {
					round = 1;
					prevWrites = writeMaps;
					d->ops->draw_rects(d, &p);
					kept = writeMaps == prevWrites && read_rect(lap, 4, 0);
				}
			}
			check(round && kept, "the rect that went round drawn again from where it was");
		}
		crcCollide = 0;
		p.vertices = a;
		prevWrites = writeMaps;
		d->ops->draw_rects(d, &p);
		check(writeMaps == prevWrites + 1 && read_rect(a, 4, 0), "a rect from the lap before uploaded again");
		p.verticesCount = 1;
		p.vertices = c;
		for (i = 0; i < 600; ++i) {
			c[0].x = (f32)i;
			d->ops->draw_rects(d, &p);
			bad += !read_rect(c, 1, 0);
		}
		p.verticesCount = 4;
		p.vertices = a;
		prevWrites = writeMaps;
		d->ops->draw_rects(d, &p);
		check(writeMaps == prevWrites + 1 && read_rect(a, 4, 0), "a full table drops the rects");
		prevWrites = writeMaps;
		d->ops->draw_rects(d, &p);
		check(writeMaps == prevWrites && read_rect(a, 4, 0), "a rect kept past a drop");
		/* more rects in a lap than the table has slots */
		p.verticesCount = 1;
		p.vertices = c;
		for (i = 0; i < 1100; ++i) {
			c[0].x = (f32)(1000 + i);
			d->ops->draw_rects(d, &p);
			bad += !read_rect(c, 1, 0);
		}
		check(bad == 0, "single vertex rects read as drawn");
		d->ops->destroy(d);
		check_released();
	}
}
static void test_unbuffered(void)
{
	gli_gl_drawer *d;
	gli_draw_triangles_params p;
	gli_draw_rects_params r;
	static RectVertex rv[4];
	static SPVertex other[8];
	u32 k, bad, prevPointers, prevToggles;

	reset_gl();
	gli_gl_attribs_reset(&attribs);
	d = gli_gl_drawer_new_unbuffered(&info, &attribs);
	check(d != NULL, "an unbuffered drawer");
	check(disables == MaxAttribIndex && enables == 0, "every array off to start");

	memset(&p, 0, sizeof(p));
	make_vertices(verts, 200);
	p.mode = GL_TRIANGLES;
	p.verticesCount = 200;
	p.vertices = verts;
	p.combiner = &program;
	usesShade = usesTexture = 1;
	p.flatColors = 1;
	d->ops->draw_triangles(d, &p);
	bad = gotCount != 200;
	for (k = 0; k < gotCount; ++k)
		bad += !read_vertex(k, &verts[k], 1, 1, 1, 1);
	check(bad == 0, "triangles read where they lie, flat colours");
	check(on(TRIS_ALL), "position, colour, texture and modify on");
	check(vaos[0].attribs[GLI_TRI_ATTRIB_MODIFY].norm == GL_FALSE && vaos[0].attribs[GLI_TRI_ATTRIB_MODIFY].type == GL_BYTE,
		"modify as bytes, not normalized");

	p.flatColors = 0;
	prevPointers = pointers;
	prevToggles = enables + disables;
	d->ops->draw_triangles(d, &p);
	check(pointers == prevPointers + 1 && enables + disables == prevToggles, "only the colour pointer moves");
	for (k = 0, bad = 0; k < gotCount; ++k)
		bad += !read_vertex(k, &verts[k], 0, 1, 1, 1);
	check(bad == 0, "smooth colours");

	prevPointers = pointers;
	d->ops->draw_triangles(d, &p);
	check(pointers == prevPointers && enables + disables == prevToggles, "the same draw again sets nothing");

	/* elements */
	for (k = 0; k < 90; ++k)
		elems[k] = (u16)next_rand(200);
	p.elements = elems;
	p.elementsCount = 90;
	d->ops->draw_triangles(d, &p);
	for (k = 0, bad = gotCount != 90; k < gotCount; ++k)
		bad += !read_vertex(k, &verts[elems[k]], 0, 1, 1, 1);
	check(bad == 0, "elements read where they lie");
	p.elements = NULL;

	/* the program reads no shade nor texture: those arrays off */
	usesShade = usesTexture = 0;
	make_vertices(other, 8);
	p.vertices = other;
	p.verticesCount = 8;
	hwLighting = 1;
	numLightsSet = 0;
	d->ops->draw_triangles(d, &p);
	hwLighting = 0;
	check(on(BIT(GLI_TRI_ATTRIB_POSITION) | BIT(GLI_TRI_ATTRIB_MODIFY)), "no colour nor texture read");
	for (k = 0, bad = 0; k < gotCount; ++k)
		bad += !read_vertex(k, &other[k], 0, 0, 0, 1);
	check(bad == 0, "the other vertices read");
	check(numLightsSet == 1 && numLights == (GLfloat)other[0].HWLight, "the light count set where allowed");
	usesShade = 1;
	usesTexture = 0;
	d->ops->draw_triangles(d, &p);
	check(on(BIT(GLI_TRI_ATTRIB_POSITION) | BIT(GLI_TRI_ATTRIB_COLOR) | BIT(GLI_TRI_ATTRIB_MODIFY)), "shade alone");
	usesShade = 0;
	usesTexture = 1;
	d->ops->draw_triangles(d, &p);
	check(on(BIT(GLI_TRI_ATTRIB_POSITION) | BIT(GLI_TRI_ATTRIB_TEXCOORD) | BIT(GLI_TRI_ATTRIB_MODIFY)), "texture alone");
	for (k = 0, bad = 0; k < gotCount; ++k)
		bad += !read_vertex(k, &other[k], 0, 0, 1, 1);
	check(bad == 0, "texture coordinates read");

	/* rects: the tiles the program reads, on a texrect */
	make_rect(rv, 4);
	r.mode = GL_TRIANGLE_STRIP;
	r.texrect = 1;
	r.verticesCount = 4;
	r.vertices = rv;
	r.combiner = &program;
	usesTile0 = 1;
	usesTile1 = 0;
	d->ops->draw_rects(d, &r);
	check(read_rect(rv, 4, 0) && memcmp(got[3][GLI_RECT_ATTRIB_TEXCOORD0], &rv[3].s0, 8) == 0, "a rect and its first tile read");
	check(on(BIT(GLI_RECT_ATTRIB_POSITION) | BIT(GLI_RECT_ATTRIB_TEXCOORD0)), "the triangles' arrays off for a rect");
	usesTile1 = 1;
	d->ops->draw_rects(d, &r);
	check(read_rect(rv, 4, 1) && on(BIT(GLI_RECT_ATTRIB_POSITION) | BIT(GLI_RECT_ATTRIB_TEXCOORD0) | BIT(GLI_RECT_ATTRIB_TEXCOORD1)), "both tiles");
	usesTile0 = 0;
	d->ops->draw_rects(d, &r);
	check(on(BIT(GLI_RECT_ATTRIB_POSITION) | BIT(GLI_RECT_ATTRIB_TEXCOORD1)), "the second tile alone");
	r.texrect = 0;
	usesTile0 = 1;
	d->ops->draw_rects(d, &r);
	check(on(BIT(GLI_RECT_ATTRIB_POSITION)), "no tile on a fill rect");

	/* triangles after rects: the rects' arrays off again */
	usesShade = usesTexture = 1;
	numLightsSet = 0;
	d->ops->draw_triangles(d, &p);
	check(on(TRIS_ALL), "the rects' arrays off for triangles");
	check(numLightsSet == 0, "no light count without hardware lighting");

	/* a line */
	make_vertices(other, 2);
	d->ops->draw_line(d, 2.0f, other);
	check(gotMode == GL_LINES && gotCount == 2 && lineWidth == 2.0f, "a line of two vertices");
	check(on(BIT(GLI_TRI_ATTRIB_POSITION) | BIT(GLI_TRI_ATTRIB_COLOR)), "a line's position and colour");
	for (k = 0, bad = 0; k < 2; ++k)
		bad += !read_vertex(k, &other[k], 0, 1, 0, 0);
	check(bad == 0, "a line read where it lies");

	d->ops->destroy(d);
}

/* the rings' size, from GLIDEN64_RING_BYTES within 48K and 8M */
static void test_ring_sizes(void)
{
	static char small[] = "GLIDEN64_RING_BYTES=1000";
	static char large[] = "GLIDEN64_RING_BYTES=100000000";
	static char hex[] = "GLIDEN64_RING_BYTES=0x10000";
	static char none[] = "GLIDEN64_RING_BYTES=";
	static char *envs[] = { small, large, hex, none };
	static const u32 sizes[] = { 49152, 8u << 20, 0x10000, 8u << 20 };
	u32 i;
	memset(&info, 0, sizeof(info));
	info.bufferStorage = 1;
	for (i = 0; i < 4; ++i) {
		gli_gl_drawer *d;
		u32 first;
		putenv(envs[i]);
		gli_gl_attribs_reset(&attribs);
		nextBuffer = nextArray = 1;
		first = nextBuffer;
		d = gli_gl_drawer_new_buffered(&info, &attribs, bindCache);
		check(storeSize[first] == sizes[i] && storeSize[first + 2] == sizes[i], "rings of GLIDEN64_RING_BYTES, within 48K and 8M");
		d->ops->destroy(d);
	}

	/* the full ring's table holds the most rects a lap has: one drawn
	 * again after 20000 others is drawn from where it was */
	{
		static RectVertex a[4], other[4];
		gli_draw_rects_params p;
		gli_gl_drawer *d;
		u32 prevWrites, bad = 0;
		info.bufferStorage = 0;
		gli_gl_attribs_reset(&attribs);
		nextBuffer = nextArray = 1;
		d = gli_gl_drawer_new_buffered(&info, &attribs, bindCache);
		make_rect(a, 4);
		p.mode = GL_TRIANGLE_STRIP;
		p.texrect = 1;
		p.verticesCount = 4;
		p.combiner = &program;
		p.vertices = a;
		d->ops->draw_rects(d, &p);
		p.vertices = other;
		for (i = 0; i < 20000; ++i) {
			make_rect(other, 1);
			d->ops->draw_rects(d, &p);
		}
		p.vertices = a;
		prevWrites = writeMaps;
		d->ops->draw_rects(d, &p);
		bad += !read_rect(a, 4, 1);
		check(bad == 0 && writeMaps == prevWrites, "the full ring's table keeps a lap of rects");
		d->ops->destroy(d);
		check_released();
	}
}

int main(void)
{
	/* each run goes round its rings many times */
	static char env[] = "GLIDEN64_RING_BYTES=49152";
	test_ring_sizes();
	putenv(env);
	test_buffered(1);
	test_buffered(0);
	test_unbuffered();
	if (failures) {
		printf("test_gliden64_gl_drawer: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_gl_drawer: %u draws ok\n", draws);
	return 0;
}
