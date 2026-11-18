#ifndef GLI_GL_TEXTURE_OBJECTS_H
#define GLI_GL_TEXTURE_OBJECTS_H

/* GL's textures: making and deleting them, their storage, updates and
 * parameters. C89. */

#include <Graphics/gli_context.h>
#include "GLFunctions.h"
#include "opengl_GLInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the parameters last set on a texture, so a set that changes nothing
 * calls nothing: name 0 is an empty slot */
typedef struct gli_gl_tex_entry
{
	u32 name;
	GLint magFilter;
	GLint minFilter;
	GLint wrapS;
	GLint wrapT;
	GLint maxMipmapLevel;
	GLfloat maxAnisotropy;
} gli_gl_tex_entry;

typedef struct gli_gl_textures
{
	const gli_gl_info *info;
	void *bindTexture;       /* the context's texture binding cache */
	u32 storageHandle;       /* the texture given storage last */
	/* open addressing by name, made at init for more textures than a
	 * game keeps; past half full (no game comes near) it doubles */
	gli_gl_tex_entry *entries;
	u32 mask;
	u32 count;
} gli_gl_textures;

#define GLI_GL_TEX_SLOTS 16384

/* binding through @_bindTexture; whether the table was made. @_textures
 * starts zeroed; made again before it is freed, it keeps its table, as the
 * context keeps its caches. */
bool gli_gl_textures_init(gli_gl_textures *_textures, const gli_gl_info *_info, void *_bindTexture);
void gli_gl_textures_free(gli_gl_textures *_textures);

u32 gli_gl_create_texture(const gli_gl_textures *_textures, u32 _target);
void gli_gl_delete_texture(gli_gl_textures *_textures, u32 _name);
void gli_gl_init_texture(gli_gl_textures *_textures, const gli_init_texture_params *_params);
void gli_gl_update_texture(const gli_gl_textures *_textures, const gli_update_texture_params *_params);
void gli_gl_set_texture_parameters(gli_gl_textures *_textures, const gli_tex_parameters *_params);

/* glActiveTexture and glBindTexture through the context's cache, which
 * skips a bind that changes nothing; reset forgets what is bound
 * (opengl_CachedFunctions.cpp) */
void gli_gl_bind_texture(void *_cache, u32 _unit, u32 _target, u32 _name);
void gli_gl_bind_texture_reset(void *_cache);

#ifdef __cplusplus
}
#endif

#endif
