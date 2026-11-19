#ifndef GLI_GLSL_COMBINER_PROGRAM_IMPL_H
#define GLI_GLSL_COMBINER_PROGRAM_IMPL_H
/* A GL combiner program (glsl_CombinerProgramImpl.c) and its uniforms
 * (glsl_CombinerProgramUniformFactory.c). C89. */
#include <Types.h>
#include <gli_combiner.h>
#include <Graphics/CombinerValues.h>
#include <Graphics/OpenGLContext/GLFunctions.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the context's program cache (opengl_CachedFunctions.h) and what GL has
 * (opengl_GLInfo.h) */
struct gli_gl_cache1;
struct gli_gl_info;

/* how a uniform is sent: glUniform1i, 2i, 4i, 1f, 2f, 3fv, 4fv */
enum { GLI_GLSL_I1, GLI_GLSL_I2, GLI_GLSL_I4, GLI_GLSL_F1, GLI_GLSL_F2, GLI_GLSL_F3, GLI_GLSL_F4 };

/* a uniform: where it is, where its value sits in CombinerValues, of
 * what kind and size */
typedef struct gli_glsl_uniform
{
	GLint loc;
	u16 offset;
	u8 kind;
	u8 bytes;
} gli_glsl_uniform;

/* a sampler and the texture unit it reads */
typedef struct gli_glsl_sampler
{
	GLint loc;
	GLint unit;
} gli_glsl_sampler;

/* the most a program has: every named uniform, and 8 lights' direction
 * and colour */
#define GLI_GLSL_UNIFORMS_MAX (62 + 16)
#define GLI_GLSL_SAMPLERS_MAX 6

/* A program's uniforms: this combiner's values, and what of them the
 * program has, each sent only when it differs from what it last got. */
typedef struct gli_glsl_uniforms
{
	u32 groups;
	u32 count;
	u32 samplerCount;
	gli_glsl_uniform entries[GLI_GLSL_UNIFORMS_MAX];
	gli_glsl_sampler samplers[GLI_GLSL_SAMPLERS_MAX];
	CombinerValues values;
	CombinerValues uploaded;
} gli_glsl_uniforms;

/* @_uniforms, which starts zeroed, filled with what @_program of the key
 * @_key and CombinerStages.c's @_inputs reads */
void gli_glsl_uniforms_build(gli_glsl_uniforms *_uniforms, GLuint _program, u32 _inputs, u64 _key,
	const struct gli_gl_info *_glinfo);
/* the values worked out and those changed sent, all of them when @_force,
 * which also sets the samplers' units (glsl_CombinerProgramImpl.c) */
void gli_glsl_uniforms_update(gli_glsl_uniforms *_uniforms, int _force);

/* A GL program for a combine key: its C face, the GL program, the cache
 * it is used through, what it reads and its uniforms. */
typedef struct gli_glsl_cprog
{
	gli_cprog base;
	int needUpdate;
	GLuint program;
	struct gli_gl_cache1 *useProgram;
	u32 inputs;
	gli_glsl_uniforms uniforms;
} gli_glsl_cprog;

extern const gli_cprog_ops gli_glsl_cprog_ops;

/* the program for @_key from linked GL @_program, which it owns from here,
 * its uniforms found; NULL, the GL program deleted, without memory */
gli_cprog *gli_glsl_cprog_new(u64 _key, GLuint _program, struct gli_gl_cache1 *_useProgram, u32 _inputs,
	const struct gli_gl_info *_glinfo);

/* A program's stored form: its key, inputs, binary format and length, then
 * the binary. How much room it takes, 0 when it has none; and it written
 * to @_out, its length returned, 0 when it is not got or @_room is short. */
size_t gli_glsl_cprog_binary_size(const gli_cprog *_program);
size_t gli_glsl_cprog_binary_write(const gli_cprog *_program, char *_out, size_t _room);

#ifdef __cplusplus
}
#endif

#endif
