#include <stdlib.h>
#include <string.h>
#include "opengl_TextureManipulationObjectFactory.h"

#ifndef GL_EXT_texture_filter_anisotropic
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#endif

/* GL 4.5's direct state access, off: textures made, given storage,
 * updated and set without binding them */
/*#define ENABLE_GL_4_5*/
/* GL 4.2's immutable storage, where the context has it */
#define ENABLE_GL_4_2

#ifdef ENABLE_GL_4_5
static int has_gl_4_5(const gli_gl_info *_info)
{
	return _info->majorVersion > 4 || (_info->majorVersion == 4 && _info->minorVersion >= 5);
}
#endif

static int has_tex_storage(const gli_gl_info *_info)
{
#ifdef ENABLE_GL_4_2
	return _info->texStorage;
#else
	(void)_info;
	return 0;
#endif
}

/*---------------The parameters' table-------------*/

static u32 slot_of(u32 _name, u32 _mask)
{
	return (_name * 0x9E3779B1u) & _mask;
}

/* the entry of @_name, or NULL */
static gli_gl_tex_entry *find_entry(const gli_gl_textures *_t, u32 _name)
{
	u32 i;
	if (_t->entries == NULL)
		return NULL;
	for (i = slot_of(_name, _t->mask);; i = (i + 1) & _t->mask) {
		gli_gl_tex_entry *e = &_t->entries[i];
		if (e->name == _name)
			return e;
		if (e->name == 0)
			return NULL;
	}
}

static gli_gl_tex_entry *empty_slot(gli_gl_tex_entry *_entries, u32 _mask, u32 _name)
{
	u32 i = slot_of(_name, _mask);
	while (_entries[i].name != 0)
		i = (i + 1) & _mask;
	return &_entries[i];
}

static void grow(gli_gl_textures *_t)
{
	const u32 slots = (_t->mask + 1) * 2;
	gli_gl_tex_entry *entries = (gli_gl_tex_entry *)calloc(slots, sizeof(gli_gl_tex_entry));
	u32 i;
	if (entries == NULL)
		return;
	for (i = 0; i <= _t->mask; ++i)
		if (_t->entries[i].name != 0)
			*empty_slot(entries, slots - 1, _t->entries[i].name) = _t->entries[i];
	free(_t->entries);
	_t->entries = entries;
	_t->mask = slots - 1;
}

/* the entry of @_name, made with its parameters 0 where it had none;
 * NULL where it cannot be had */
static gli_gl_tex_entry *entry_of(gli_gl_textures *_t, u32 _name)
{
	gli_gl_tex_entry *e = find_entry(_t, _name);
	if (e != NULL || _t->entries == NULL)
		return e;
	if (_t->count >= (_t->mask + 1) / 2)
		grow(_t);
	if (_t->count >= _t->mask)
		return NULL;
	e = empty_slot(_t->entries, _t->mask, _name);
	memset(e, 0, sizeof(*e));
	e->name = _name;
	++_t->count;
	return e;
}

/* @_name's entry dropped, the ones after it in its run moved back so
 * every lookup still finds its entry */
static void erase_entry(gli_gl_textures *_t, u32 _name)
{
	gli_gl_tex_entry *e = find_entry(_t, _name);
	u32 hole, i;
	if (e == NULL)
		return;
	hole = (u32)(e - _t->entries);
	_t->entries[hole].name = 0;
	--_t->count;
	for (i = (hole + 1) & _t->mask; _t->entries[i].name != 0; i = (i + 1) & _t->mask) {
		const u32 home = slot_of(_t->entries[i].name, _t->mask);
		/* moves back when its home is not between the hole and it */
		if (((i - home) & _t->mask) >= ((i - hole) & _t->mask)) {
			_t->entries[hole] = _t->entries[i];
			_t->entries[i].name = 0;
			hole = i;
		}
	}
}

bool gli_gl_textures_init(gli_gl_textures *_textures, const gli_gl_info *_info, void *_bindTexture)
{
	_textures->info = _info;
	_textures->bindTexture = _bindTexture;
	_textures->storageHandle = 0;
	if (_textures->entries != NULL)
		return 1;
	_textures->count = 0;
	_textures->entries = (gli_gl_tex_entry *)calloc(GLI_GL_TEX_SLOTS, sizeof(gli_gl_tex_entry));
	_textures->mask = _textures->entries != NULL ? GLI_GL_TEX_SLOTS - 1 : 0;
	return _textures->entries != NULL;
}

void gli_gl_textures_free(gli_gl_textures *_textures)
{
	free(_textures->entries);
	_textures->entries = NULL;
	_textures->mask = 0;
	_textures->count = 0;
}

/*---------------Textures-------------*/

u32 gli_gl_create_texture(const gli_gl_textures *_textures, u32 _target)
{
	GLuint name;
#ifdef ENABLE_GL_4_5
	if (has_gl_4_5(_textures->info)) {
		glCreateTextures((GLenum)_target, 1, &name);
		return name;
	}
#else
	(void)_textures;
	(void)_target;
#endif
	glGenTextures(1, &name);
	return name;
}

void gli_gl_delete_texture(gli_gl_textures *_textures, u32 _name)
{
	GLuint name = _name;
	glDeleteTextures(1, &name);
#ifdef ENABLE_GL_4_5
	if (!has_gl_4_5(_textures->info))
#endif
		gli_gl_bind_texture_reset(_textures->bindTexture);
	if (_textures->storageHandle == _name)
		_textures->storageHandle = 0;
	erase_entry(_textures, _name);
}

void gli_gl_init_texture(gli_gl_textures *_textures, const gli_init_texture_params *_params)
{
#ifdef ENABLE_GL_4_5
	if (has_gl_4_5(_textures->info)) {
		if (_params->msaaLevel != 0) {
			glTexStorage2DMultisample(_params->handle, _params->msaaLevel, (GLenum)_params->internalFormat,
				_params->width, _params->height, GL_FALSE);
			return;
		}
		if (_textures->storageHandle != _params->handle) {
			_textures->storageHandle = _params->handle;
			glTextureStorage2D(_params->handle, _params->mipMapLevels, (GLenum)_params->internalFormat,
				_params->width, _params->height);
		}
		if (_params->data != NULL)
			glTextureSubImage2D(_params->handle, _params->mipMapLevel, 0, 0, _params->width, _params->height,
				(GLenum)_params->format, (GLenum)_params->dataType, _params->data);
		return;
	}
#endif
	if (_params->msaaLevel != 0) {
		gli_gl_bind_texture(_textures->bindTexture, _params->textureUnitIndex, GLI_TT_TEXTURE_2D_MULTISAMPLE, _params->handle);
		if (has_tex_storage(_textures->info))
			glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, _params->msaaLevel, (GLenum)_params->internalFormat,
				_params->width, _params->height, GL_FALSE);
		else
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, _params->msaaLevel, (GLenum)_params->internalFormat,
				_params->width, _params->height, GL_FALSE);
		return;
	}

	gli_gl_bind_texture(_textures->bindTexture, _params->textureUnitIndex, GLI_TT_TEXTURE_2D, _params->handle);
	if (!has_tex_storage(_textures->info) || _params->mutableStorage) {
		glTexImage2D(GL_TEXTURE_2D, _params->mipMapLevel, (GLint)_params->internalFormat, _params->width, _params->height,
			0, (GLenum)_params->format, (GLenum)_params->dataType, _params->data);
		return;
	}
	if (_textures->storageHandle != _params->handle) {
		_textures->storageHandle = _params->handle;
		glTexStorage2D(GL_TEXTURE_2D, _params->mipMapLevels, (GLenum)_params->internalFormat,
			_params->width, _params->height);
	}
	if (_params->data != NULL)
		glTexSubImage2D(GL_TEXTURE_2D, _params->mipMapLevel, 0, 0, _params->width, _params->height,
			(GLenum)_params->format, (GLenum)_params->dataType, _params->data);
}

void gli_gl_update_texture(const gli_gl_textures *_textures, const gli_update_texture_params *_params)
{
#ifdef ENABLE_GL_4_5
	if (has_gl_4_5(_textures->info)) {
		glTextureSubImage2D(_params->handle, _params->mipMapLevel, _params->x, _params->y, _params->width,
			_params->height, (GLenum)_params->format, (GLenum)_params->dataType, _params->data);
		return;
	}
#endif
	gli_gl_bind_texture(_textures->bindTexture, _params->textureUnitIndex, GLI_TT_TEXTURE_2D, _params->handle);
	glTexSubImage2D(GL_TEXTURE_2D, _params->mipMapLevel, _params->x, _params->y, _params->width, _params->height,
		(GLenum)_params->format, (GLenum)_params->dataType, _params->data);
}

/*---------------Parameters-------------*/

#define GLI_TEX_PARAM(_field, _pname, _value) \
	do { \
		const GLint v = (GLint)(_value); \
		if (!(found != NULL && found->_field == v)) { \
			glTexParameteri(target, _pname, v); \
			e = e != NULL ? e : entry_of(_textures, _params->handle); \
			if (e != NULL) \
				e->_field = v; \
		} \
	} while (0)

/* A parameter is set unless the texture's entry, as it was before this
 * call, holds it already; a texture with no entry has one made, its other
 * parameters 0. */
void gli_gl_set_texture_parameters(gli_gl_textures *_textures, const gli_tex_parameters *_params)
{
	const GLenum target = (GLenum)_params->target;
	const gli_gl_tex_entry *found;
	gli_gl_tex_entry *e;
#ifdef ENABLE_GL_4_5
	if (has_gl_4_5(_textures->info)) {
		const u32 handle = _params->handle;
		if (_params->magFilter != GLI_PARAM_INVALID)
			glTextureParameteri(handle, GL_TEXTURE_MAG_FILTER, (GLint)_params->magFilter);
		if (_params->minFilter != GLI_PARAM_INVALID)
			glTextureParameteri(handle, GL_TEXTURE_MIN_FILTER, (GLint)_params->minFilter);
		if (_params->wrapS != GLI_PARAM_INVALID)
			glTextureParameteri(handle, GL_TEXTURE_WRAP_S, (GLint)_params->wrapS);
		if (_params->wrapT != GLI_PARAM_INVALID)
			glTextureParameteri(handle, GL_TEXTURE_WRAP_T, (GLint)_params->wrapT);
		if (_params->maxMipmapLevel != GLI_PARAM_INVALID)
			glTextureParameteri(handle, GL_TEXTURE_MAX_LEVEL, (GLint)_params->maxMipmapLevel);
		if (_params->maxAnisotropy > 0.0f)
			glTextureParameterf(handle, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat)_params->maxAnisotropy);
		return;
	}
#endif
	found = find_entry(_textures, _params->handle);
	e = (gli_gl_tex_entry *)found;
	gli_gl_bind_texture(_textures->bindTexture, _params->textureUnitIndex, _params->target, _params->handle);

	if (_params->magFilter != GLI_PARAM_INVALID)
		GLI_TEX_PARAM(magFilter, GL_TEXTURE_MAG_FILTER, _params->magFilter);
	if (_params->minFilter != GLI_PARAM_INVALID)
		GLI_TEX_PARAM(minFilter, GL_TEXTURE_MIN_FILTER, _params->minFilter);
	if (_params->wrapS != GLI_PARAM_INVALID)
		GLI_TEX_PARAM(wrapS, GL_TEXTURE_WRAP_S, _params->wrapS);
	if (_params->wrapT != GLI_PARAM_INVALID)
		GLI_TEX_PARAM(wrapT, GL_TEXTURE_WRAP_T, _params->wrapT);
	if (!_textures->info->isGLES2 && _params->maxMipmapLevel != GLI_PARAM_INVALID)
		GLI_TEX_PARAM(maxMipmapLevel, GL_TEXTURE_MAX_LEVEL, _params->maxMipmapLevel);
	if (_params->maxAnisotropy > 0.0f && !(found != NULL && found->maxAnisotropy == (GLfloat)_params->maxAnisotropy)) {
		glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat)_params->maxAnisotropy);
		e = e != NULL ? e : entry_of(_textures, _params->handle);
		if (e != NULL)
			e->maxAnisotropy = (GLfloat)_params->maxAnisotropy;
	}
}
