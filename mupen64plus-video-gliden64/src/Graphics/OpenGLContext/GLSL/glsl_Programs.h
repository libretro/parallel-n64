#ifndef GLI_GLSL_PROGRAMS_H
#define GLI_GLSL_PROGRAMS_H

/* The GLSL programs' C face, for the GL context: the combiner program
 * builder, the special shaders and the shader storage, which are still
 * C++ (glsl_CombinerProgramBuilder.cpp). C89. */

#include <stddef.h>
#include <Types.h>
#include <boolean.h>
#include "../opengl_GLInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Combiner;
struct gli_cprog;
struct gli_gl_cache1;
struct gli_combiner_table;

typedef struct gli_glsl_builder gli_glsl_builder;   /* glsl::CombinerProgramBuilder */
typedef struct gli_glsl_special gli_glsl_special;   /* glsl::SpecialShadersFactory */

/* programs for @_info, used through the program cache @_useProgram */
gli_glsl_builder *gli_glsl_builder_new(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram);
void gli_glsl_builder_delete(gli_glsl_builder *_builder);
/* whether the combiner options it was made for have changed */
bool gli_glsl_builder_obsolete(const gli_glsl_builder *_builder);
bool gli_glsl_builder_uber_only(const gli_glsl_builder *_builder);


/* the special shaders, with @_builder's shader headers */
gli_glsl_special *gli_glsl_special_new(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	const gli_glsl_builder *_builder);
void gli_glsl_special_delete(gli_glsl_special *_special);

/* The programs a context holds, first in it, so its table's program
 * entries are these, taking the context itself (gli_context.h) */
typedef struct gli_glsl_programs
{
	gli_glsl_builder *builder;
	gli_glsl_special *special;
} gli_glsl_programs;

/* @_key's program, compiled now, or left to the uber shader until it is
 * (NULL where it cannot defer) */
struct gli_cprog *gli_glsl_ops_build(void *_programs, struct Combiner *_color, struct Combiner *_alpha, u64 _key);
struct gli_cprog *gli_glsl_ops_build_async(void *_programs, struct Combiner *_color, struct Combiner *_alpha, u64 _key);
/* the special shaders */
struct gli_cprog *gli_glsl_ops_depth_fog_shader(void *_programs);
struct gli_cprog *gli_glsl_ops_texrect_draw_shader(void *_programs);
struct gli_cprog *gli_glsl_ops_texrect_clear_shader(void *_programs);
struct gli_cprog *gli_glsl_ops_texrect_copy_shader(void *_programs);
struct gli_cprog *gli_glsl_ops_gamma_correction_shader(void *_programs);
struct gli_cprog *gli_glsl_ops_fxaa_shader(void *_programs);

/* the shader storage on disk */
bool gli_glsl_save_storage(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	const struct gli_combiner_table *_combiners);
bool gli_glsl_load_storage(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	struct gli_combiner_table *_combiners);
/* the combiner keys' journal into @_path, @_size bytes at most */
void gli_glsl_journal_path(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	char *_path, size_t _size, u32 *_version);

#ifdef __cplusplus
}
#endif

#endif
