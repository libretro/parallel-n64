/* test_gliden64_texture_objects -- GLideN64's GL textures
 * (Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.c) on a
 * stub GL.
 *
 * A texture is GL's new name. Its storage: without immutable storage, a
 * glTexImage2D of what was asked, bound to its unit first, or the
 * multisampled one; with it, the same for one the texture cache keeps
 * respecifiable, else glTexStorage2D once a texture, its data after as a
 * sub image, and glTexStorage2DMultisample. An update is a sub image of
 * the bound texture. Deleting one deletes it, forgets what the binding
 * cache holds, and its storage and parameters, so they are set again.
 *
 * A parameter is set only when the texture's last one differs, as the
 * C++ map had it: a texture's first set makes its entry with the rest 0,
 * so a later 0 is not set (GL's own default stays); GLES 2 sets no
 * mipmap level; anisotropy only above 0. Over tens of thousands of
 * textures, made and deleted at random, against a model of what each
 * holds, every texture's parameters are found until it is deleted, the
 * table growing past half full, and deletes move back what follows.
 *
 *   ./test_gliden64_texture_objects
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- the cache ---- */

static int bindCache;
static u32 binds, bindUnit, bindTarget, bindName, bindResets;
static int badCache;

void gli_gl_bind_texture(void *_cache, u32 _unit, u32 _target, u32 _name)
{
	if (_cache != &bindCache)
		badCache = 1;
	binds++;
	bindUnit = _unit;
	bindTarget = _target;
	bindName = _name;
}

void gli_gl_bind_texture_reset(void *_cache)
{
	if (_cache != &bindCache)
		badCache = 1;
	bindResets++;
}

/* ---- GL ---- */

static GLuint nextName = 1;
static GLuint deleted;
static void APIENTRY gen_textures(GLsizei _n, GLuint *_t) { check(_n == 1, "one texture"); *_t = nextName++; }
static void APIENTRY delete_textures(GLsizei _n, const GLuint *_t) { check(_n == 1, "one texture deleted"); deleted = *_t; }

typedef struct image_call
{
	u32 count;
	GLenum target;
	GLint level, internalFormat;
	GLsizei width, height, levels, samples;
	GLint x, y;
	GLenum format, type;
	const void *data;
	GLboolean fixed;
	u32 boundName, boundTarget;
} image_call;

static image_call texImage, texStorage, subImage, msImage, msStorage;

static void bound(image_call *_c)
{
	_c->boundName = bindName;
	_c->boundTarget = bindTarget;
	_c->count++;
}

static void APIENTRY tex_image(GLenum _t, GLint _level, GLint _internal, GLsizei _w, GLsizei _h, GLint _border,
	GLenum _f, GLenum _type, const void *_d)
{
	check(_border == 0, "no border");
	texImage.target = _t; texImage.level = _level; texImage.internalFormat = _internal;
	texImage.width = _w; texImage.height = _h; texImage.format = _f; texImage.type = _type; texImage.data = _d;
	bound(&texImage);
}
static void APIENTRY tex_storage(GLenum _t, GLsizei _levels, GLenum _internal, GLsizei _w, GLsizei _h)
{
	texStorage.target = _t; texStorage.levels = _levels; texStorage.internalFormat = (GLint)_internal;
	texStorage.width = _w; texStorage.height = _h;
	bound(&texStorage);
}
static void APIENTRY sub_image(GLenum _t, GLint _level, GLint _x, GLint _y, GLsizei _w, GLsizei _h,
	GLenum _f, GLenum _type, const void *_d)
{
	subImage.target = _t; subImage.level = _level; subImage.x = _x; subImage.y = _y;
	subImage.width = _w; subImage.height = _h; subImage.format = _f; subImage.type = _type; subImage.data = _d;
	bound(&subImage);
}
static void APIENTRY ms_image(GLenum _t, GLsizei _samples, GLenum _internal, GLsizei _w, GLsizei _h, GLboolean _fixed)
{
	msImage.target = _t; msImage.samples = _samples; msImage.internalFormat = (GLint)_internal;
	msImage.width = _w; msImage.height = _h; msImage.fixed = _fixed;
	bound(&msImage);
}
static void APIENTRY ms_storage(GLenum _t, GLsizei _samples, GLenum _internal, GLsizei _w, GLsizei _h, GLboolean _fixed)
{
	msStorage.target = _t; msStorage.samples = _samples; msStorage.internalFormat = (GLint)_internal;
	msStorage.width = _w; msStorage.height = _h; msStorage.fixed = _fixed;
	bound(&msStorage);
}

/* the parameters set, by name */
static u32 paramCalls;
static GLenum paramTarget, paramName;
static GLint paramValue;
static GLfloat paramValueF;
static u32 paramBoundName;
static void APIENTRY tex_parameteri(GLenum _t, GLenum _p, GLint _v)
{
	paramTarget = _t; paramName = _p; paramValue = _v; paramBoundName = bindName;
	paramCalls++;
}
static void APIENTRY tex_parameterf(GLenum _t, GLenum _p, GLfloat _v)
{
	paramTarget = _t; paramName = _p; paramValueF = _v; paramBoundName = bindName;
	paramCalls++;
}

PFNGLGENTEXTURESPROC g_glGenTextures = gen_textures;
PFNGLDELETETEXTURESPROC g_glDeleteTextures = delete_textures;
PFNGLTEXIMAGE2DPROC g_glTexImage2D = tex_image;
PFNGLTEXSTORAGE2DPROC g_glTexStorage2D = tex_storage;
PFNGLTEXSUBIMAGE2DPROC g_glTexSubImage2D = sub_image;
PFNGLTEXIMAGE2DMULTISAMPLEPROC g_glTexImage2DMultisample = ms_image;
PFNGLTEXSTORAGE2DMULTISAMPLEPROC g_glTexStorage2DMultisample = ms_storage;
PFNGLTEXPARAMETERIPROC g_glTexParameteri = tex_parameteri;
PFNGLTEXPARAMETERFPROC g_glTexParameterf = tex_parameterf;

static gli_gl_info info;
static gli_gl_textures textures;
static const u8 pixels[16];

static gli_init_texture_params init_params(u32 _handle)
{
	static const gli_init_texture_params def = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_init_texture_params p = def;
	p.handle = _handle;
	p.textureUnitIndex = 2;
	p.width = 64;
	p.height = 32;
	p.mipMapLevel = 1;
	p.mipMapLevels = 3;
	p.format = GL_RGBA;
	p.internalFormat = GL_RGBA8;
	p.dataType = GL_UNSIGNED_BYTE;
	p.data = pixels;
	return p;
}

static void clear_calls(void)
{
	memset(&texImage, 0, sizeof(texImage));
	memset(&texStorage, 0, sizeof(texStorage));
	memset(&subImage, 0, sizeof(subImage));
	memset(&msImage, 0, sizeof(msImage));
	memset(&msStorage, 0, sizeof(msStorage));
}

static void test_create(void)
{
	const u32 a = gli_gl_create_texture(&textures, GL_TEXTURE_2D);
	const u32 b = gli_gl_create_texture(&textures, GL_TEXTURE_2D);
	check(a != 0 && b == a + 1, "textures are GL's new names");
}

static void test_storage_without(void)
{
	gli_init_texture_params p = init_params(5);
	info.texStorage = 0;
	clear_calls();
	gli_gl_init_texture(&textures, &p);
	check(texImage.count == 1 && texStorage.count == 0 && subImage.count == 0, "without storage, an image");
	check(texImage.boundName == 5 && texImage.boundTarget == GL_TEXTURE_2D && bindUnit == 2, "bound to its unit first");
	check(texImage.target == GL_TEXTURE_2D && texImage.level == 1 && texImage.internalFormat == GL_RGBA8 && texImage.width == 64
		&& texImage.height == 32 && texImage.format == GL_RGBA && texImage.type == GL_UNSIGNED_BYTE && texImage.data == pixels,
		"of what was asked");
	gli_gl_init_texture(&textures, &p);
	check(texImage.count == 2, "and again each time");

	p.msaaLevel = 4;
	clear_calls();
	gli_gl_init_texture(&textures, &p);
	check(msImage.count == 1 && texImage.count == 0 && msStorage.count == 0, "multisampled, its image");
	check(msImage.boundTarget == GL_TEXTURE_2D_MULTISAMPLE && msImage.boundName == 5 && msImage.target == GL_TEXTURE_2D_MULTISAMPLE
		&& msImage.samples == 4 && msImage.internalFormat == GL_RGBA8 && msImage.width == 64 && msImage.height == 32
		&& msImage.fixed == GL_FALSE, "bound and sized as asked");
}

static void test_storage_with(void)
{
	gli_init_texture_params p = init_params(7);
	info.texStorage = 1;
	textures.storageHandle = 0;

	p.mutableStorage = 1;
	clear_calls();
	gli_gl_init_texture(&textures, &p);
	check(texImage.count == 1 && texStorage.count == 0 && texImage.boundName == 7, "a texture kept respecifiable: an image");

	p.mutableStorage = 0;
	clear_calls();
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 1 && texStorage.boundName == 7 && texStorage.target == GL_TEXTURE_2D && texStorage.levels == 3
		&& texStorage.internalFormat == GL_RGBA8 && texStorage.width == 64 && texStorage.height == 32, "storage, its levels");
	check(subImage.count == 1 && subImage.level == 1 && subImage.x == 0 && subImage.y == 0 && subImage.width == 64
		&& subImage.height == 32 && subImage.format == GL_RGBA && subImage.type == GL_UNSIGNED_BYTE && subImage.data == pixels,
		"its data after, a sub image");
	check(texImage.count == 0, "no image");
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 1 && subImage.count == 2, "storage once a texture, data each time");
	p.data = NULL;
	gli_gl_init_texture(&textures, &p);
	check(subImage.count == 2, "no data, no sub image");

	p = init_params(8);
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 2 && texStorage.boundName == 8, "another texture its own storage");
	p = init_params(7);
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 3, "and back again: storage for the last given it only is skipped");

	p.msaaLevel = 2;
	clear_calls();
	gli_gl_init_texture(&textures, &p);
	check(msStorage.count == 1 && msImage.count == 0 && msStorage.boundTarget == GL_TEXTURE_2D_MULTISAMPLE
		&& msStorage.samples == 2 && msStorage.width == 64 && msStorage.fixed == GL_FALSE, "multisampled storage");

	/* deleting forgets the storage */
	p = init_params(9);
	clear_calls();
	gli_gl_init_texture(&textures, &p);
	bindResets = 0;
	gli_gl_delete_texture(&textures, 9);
	check(deleted == 9 && bindResets == 1, "deleted, the binding cache reset");
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 2, "a name made again given storage again");
	gli_gl_delete_texture(&textures, 3);
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 2, "deleting another keeps it");
	/* a context made again: its textures are new to it */
	gli_gl_textures_init(&textures, &info, &bindCache);
	gli_gl_init_texture(&textures, &p);
	check(texStorage.count == 3, "storage again once the context is made again");
}

static void test_update(void)
{
	static const gli_update_texture_params def = GLI_UPDATE_TEXTURE_PARAMS_DEFAULT;
	gli_update_texture_params p = def;
	p.handle = 11;
	p.textureUnitIndex = 1;
	p.x = 3; p.y = 4; p.width = 5; p.height = 6;
	p.mipMapLevel = 2;
	p.format = GL_RED;
	p.dataType = GL_FLOAT;
	p.data = pixels;
	clear_calls();
	gli_gl_update_texture(&textures, &p);
	check(subImage.count == 1 && subImage.boundName == 11 && subImage.boundTarget == GL_TEXTURE_2D && bindUnit == 1,
		"an update binds the texture to its unit");
	check(subImage.target == GL_TEXTURE_2D && subImage.level == 2 && subImage.x == 3 && subImage.y == 4 && subImage.width == 5
		&& subImage.height == 6 && subImage.format == GL_RED && subImage.type == GL_FLOAT && subImage.data == pixels,
		"a sub image as asked");
}

static gli_tex_parameters tex_params(u32 _handle)
{
	gli_tex_parameters p;
	p.handle = _handle;
	p.textureUnitIndex = 0;
	p.target = GL_TEXTURE_2D;
	p.magFilter = GLI_PARAM_INVALID;
	p.minFilter = GLI_PARAM_INVALID;
	p.wrapS = GLI_PARAM_INVALID;
	p.wrapT = GLI_PARAM_INVALID;
	p.maxMipmapLevel = GLI_PARAM_INVALID;
	p.maxAnisotropy = 0.0f;
	return p;
}

static void test_parameters(void)
{
	gli_tex_parameters p = tex_params(20);
	p.magFilter = GL_LINEAR;
	p.minFilter = GL_NEAREST;
	p.wrapS = GL_REPEAT;
	p.wrapT = GL_CLAMP_TO_EDGE;
	p.maxMipmapLevel = 4;
	p.maxAnisotropy = 8.0f;
	p.textureUnitIndex = 3;
	paramCalls = 0;
	binds = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 6 && binds == 1 && bindName == 20 && bindUnit == 3 && bindTarget == GL_TEXTURE_2D,
		"a new texture's parameters all set, bound");
	check(paramName == GL_TEXTURE_MAX_ANISOTROPY_EXT && paramValueF == 8.0f && paramTarget == GL_TEXTURE_2D && paramBoundName == 20,
		"anisotropy last, as a float");
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 0 && binds == 2, "the same again sets nothing, still bound");

	p.wrapT = GL_MIRRORED_REPEAT;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 1 && paramName == GL_TEXTURE_WRAP_T && paramValue == GL_MIRRORED_REPEAT, "only what changed");
	p.magFilter = GL_NEAREST;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 2 && paramName == GL_TEXTURE_MAG_FILTER && paramValue == GL_NEAREST, "mag filter");
	p.minFilter = GL_LINEAR_MIPMAP_LINEAR;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 3 && paramName == GL_TEXTURE_MIN_FILTER, "min filter");
	p.wrapS = GL_MIRRORED_REPEAT;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 4 && paramName == GL_TEXTURE_WRAP_S, "wrap s");
	p.maxMipmapLevel = 2;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 5 && paramName == GL_TEXTURE_MAX_LEVEL && paramValue == 2, "mipmap level");
	p.maxAnisotropy = 4.0f;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 6 && paramName == GL_TEXTURE_MAX_ANISOTROPY_EXT && paramValueF == 4.0f, "anisotropy");
	p.maxAnisotropy = 0.0f;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 6, "anisotropy 0 left as it is");
	gli_gl_set_texture_parameters(&textures, &p);

	/* unset fields left alone */
	p = tex_params(20);
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 0, "nothing asked, nothing set");

	/* a texture's first set makes its entry, the rest 0 */
	p = tex_params(21);
	p.magFilter = GL_LINEAR;
	gli_gl_set_texture_parameters(&textures, &p);
	p.maxMipmapLevel = 0;
	p.wrapS = 0;
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 0, "a 0 after a texture's first set is taken as set");
	p = tex_params(22);
	p.maxMipmapLevel = 0;
	p.magFilter = GL_LINEAR;
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 2, "a 0 in the first set is set");

	/* GLES 2: no mipmap level */
	info.isGLES2 = 1;
	p = tex_params(23);
	p.maxMipmapLevel = 3;
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 0, "GLES 2 sets no mipmap level");
	info.isGLES2 = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 1 && paramValue == 3, "the rest do");

	/* deleting forgets them */
	p = tex_params(20);
	p.magFilter = GL_NEAREST;
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 0, "kept");
	gli_gl_delete_texture(&textures, 20);
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 1, "set again once the texture is deleted");

	/* a name made again after it is deleted starts with nothing kept */
	p = tex_params(25);
	p.magFilter = GL_LINEAR;
	p.wrapS = GL_REPEAT;
	p.maxMipmapLevel = 5;
	gli_gl_set_texture_parameters(&textures, &p);
	gli_gl_delete_texture(&textures, 25);
	p = tex_params(25);
	p.magFilter = GL_NEAREST;
	gli_gl_set_texture_parameters(&textures, &p);
	p.wrapS = GL_REPEAT;
	p.maxMipmapLevel = 5;
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 2, "its parameters from before not kept");

	/* another target */
	p = tex_params(24);
	p.target = GL_TEXTURE_2D_MULTISAMPLE;
	p.wrapS = GL_REPEAT;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramTarget == GL_TEXTURE_2D_MULTISAMPLE && bindTarget == GL_TEXTURE_2D_MULTISAMPLE, "on the target asked");
}

/* ---- the table, against a model ---- */

#define NAMES 60000
static GLint model[NAMES + 1];  /* each texture's mag filter, 0 none */

static unsigned rng = 7;
static u32 next_rand(u32 _n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % _n; }

static void test_table(void)
{
	gli_tex_parameters p;
	u32 i, k, bad = 0, live = 0, maxLive = 0;
	memset(model, 0, sizeof(model));
	gli_gl_textures_free(&textures);
	check(gli_gl_textures_init(&textures, &info, &bindCache), "a table");
	for (k = 0; k < 400000; ++k) {
		/* names from a range, and spread ones, which crowd the table */
		const u32 name = 1 + (k & 1 ? next_rand(NAMES) : next_rand(NAMES / 8) * 8);
		const u32 op = next_rand(10);
		if (op < 6) {
			const GLint want = (GLint)(1 + next_rand(3));
			p = tex_params(name);
			p.magFilter = (u32)want;
			paramCalls = 0;
			gli_gl_set_texture_parameters(&textures, &p);
			if (paramCalls != (model[name] == want ? 0u : 1u))
				bad++;
			if (model[name] == 0)
				live++;
			model[name] = want;
		} else if (op < 8) {
			if (model[name] != 0)
				live--;
			gli_gl_delete_texture(&textures, name);
			model[name] = 0;
		} else {
			/* a lookup: the same filter again sets nothing */
			p = tex_params(name);
			p.magFilter = model[name] != 0 ? (u32)model[name] : 1u;
			paramCalls = 0;
			gli_gl_set_texture_parameters(&textures, &p);
			if (paramCalls != (model[name] != 0 ? 0u : 1u))
				bad++;
			if (model[name] == 0)
				live++;
			model[name] = (GLint)p.magFilter;
		}
		if (live > maxLive)
			maxLive = live;
	}
	check(bad == 0, "every texture's parameters found until it is deleted");
	check(textures.count == live, "as many entries as live textures");
	check(maxLive > GLI_GL_TEX_SLOTS / 2 && textures.mask + 1 > GLI_GL_TEX_SLOTS, "past half full, the table grew");
	for (i = 1, bad = 0; i <= NAMES; ++i) {
		p = tex_params(i);
		p.magFilter = model[i] != 0 ? (u32)model[i] : 1u;
		paramCalls = 0;
		gli_gl_set_texture_parameters(&textures, &p);
		bad += paramCalls != (model[i] != 0 ? 0u : 1u);
	}
	check(bad == 0, "and all of them at the end");

	/* made again before it is freed, it keeps what it has */
	gli_gl_textures_init(&textures, &info, &bindCache);
	p = tex_params(1);
	p.magFilter = (u32)model[1];
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	check(paramCalls == 0, "a context made again keeps its textures' parameters");
	gli_gl_textures_free(&textures);
	check(textures.entries == NULL, "freed");
	/* with no table, every set is made */
	p = tex_params(1);
	p.magFilter = GL_LINEAR;
	paramCalls = 0;
	gli_gl_set_texture_parameters(&textures, &p);
	gli_gl_set_texture_parameters(&textures, &p);
	gli_gl_delete_texture(&textures, 1);
	check(paramCalls == 2, "with no table, set each time");
}

int main(void)
{
	memset(&info, 0, sizeof(info));
	memset(&textures, 0, sizeof(textures));
	check(gli_gl_textures_init(&textures, &info, &bindCache), "the textures' table made");
	test_create();
	test_storage_without();
	test_storage_with();
	test_update();
	test_parameters();
	test_table();
	check(!badCache, "binds through the cache");
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("texture objects: ok\n");
	return 0;
}
