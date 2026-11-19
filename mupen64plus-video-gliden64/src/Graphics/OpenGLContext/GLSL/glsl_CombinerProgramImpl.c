/* A GL combiner program: the program a combine key draws with, its
 * uniforms, and its stored form. C89. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/opengl_CachedFunctions.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include "glsl_CombinerProgramImpl.h"

#define PROG(_p) ((gli_glsl_cprog *)(_p))
#define CPROG(_p) ((const gli_glsl_cprog *)(_p))

static void cprog_activate(void *_p)
{
	gli_gl_use_program(PROG(_p)->useProgram, PROG(_p)->program);
}

/* the values worked out and those changed sent, all of them when @_force,
 * which also sets the samplers' units: each draw's, so the entries are
 * walked with what they say read before anything is written */
/* what was sent sits right after the values, reached from them */
#define SENT ((size_t)sizeof(CombinerValues))
typedef char gli_glsl_sent_after_values[offsetof(gli_glsl_uniforms, uploaded) ==
	offsetof(gli_glsl_uniforms, values) + SENT ? 1 : -1];

static INLINE void uniforms_update(gli_glsl_uniforms *_u, int _force)
{
	u8 *const values = (u8 *)&_u->values;
	const gli_glsl_uniform *e = _u->entries;
	const gli_glsl_uniform *const end = e + _u->count;

	updateCombinerValues(&_u->values, _u->groups);

	for (; e != end; ++e) {
		const GLint loc = e->loc;
		const u32 offset = e->offset;
		const u32 bytes = e->bytes;
		const u32 kind = e->kind;
		u8 *const src = values + offset;
		const GLint *const iv = (const GLint *)src;
		const GLfloat *const fv = (const GLfloat *)src;
		if (!_force && memcmp(src + SENT, src, bytes) == 0)
			continue;
		memcpy(src + SENT, src, bytes);
		switch (kind) {
		case GLI_GLSL_I1: glUniform1i(loc, iv[0]); break;
		case GLI_GLSL_I2: glUniform2i(loc, iv[0], iv[1]); break;
		case GLI_GLSL_I4: glUniform4i(loc, iv[0], iv[1], iv[2], iv[3]); break;
		case GLI_GLSL_F1: glUniform1f(loc, fv[0]); break;
		case GLI_GLSL_F2: glUniform2f(loc, fv[0], fv[1]); break;
		case GLI_GLSL_F3: glUniform3fv(loc, 1, fv); break;
		case GLI_GLSL_F4: glUniform4fv(loc, 1, fv); break;
		}
	}

	if (!_force)
		return;
	{
		/* which texture unit each sampler reads */
		const gli_glsl_sampler *s = _u->samplers;
		const gli_glsl_sampler *const send = s + _u->samplerCount;
		for (; s != send; ++s)
			glUniform1i(s->loc, s->unit);
	}
}

void gli_glsl_uniforms_update(gli_glsl_uniforms *_u, int _force)
{
	uniforms_update(_u, _force);
}

static void cprog_update(void *_p, bool _force)
{
	gli_glsl_cprog *c = PROG(_p);
	const int force = _force || c->needUpdate;
	c->needUpdate = 0;
	gli_gl_use_program(c->useProgram, c->program);
	uniforms_update(&c->uniforms, force);
}

static bool cprog_uses_lod(const void *_p) { return gli_inputs_lod(CPROG(_p)->inputs); }

static void cprog_free(void *_p)
{
	gli_glsl_cprog *c = PROG(_p);
	gli_gl_use_program(c->useProgram, 0);
	glDeleteProgram(c->program);
	free(c);
}

static bool cprog_uses_texture(const void *_p) { return gli_inputs_texture(CPROG(_p)->inputs); }
static bool cprog_uses_tile(const void *_p, u32 _t) { return gli_inputs_tile(CPROG(_p)->inputs, _t); }
static bool cprog_uses_shade(const void *_p) { return gli_inputs_shade(CPROG(_p)->inputs); }
static bool cprog_uses_hw_lighting(const void *_p) { return gli_inputs_hw_lighting(CPROG(_p)->inputs); }

const gli_cprog_ops gli_glsl_cprog_ops = {
	cprog_activate, cprog_update, cprog_uses_lod, cprog_free,
	cprog_uses_texture, cprog_uses_tile, cprog_uses_shade, cprog_uses_hw_lighting,
	NULL, NULL
};

gli_cprog *gli_glsl_cprog_new(u64 _key, GLuint _program, struct gli_gl_cache1 *_useProgram, u32 _inputs,
	const struct gli_gl_info *_glinfo)
{
	gli_glsl_cprog *c = (gli_glsl_cprog *)calloc(1, sizeof(*c));
	if (c == NULL) {
		glDeleteProgram(_program);
		return NULL;
	}
	c->base.ops = &gli_glsl_cprog_ops;
	c->base.key = _key;
	c->needUpdate = 1;
	c->program = _program;
	c->useProgram = _useProgram;
	c->inputs = _inputs;
	gli_glsl_uniforms_build(&c->uniforms, _program, _inputs, _key, _glinfo);
	return &c->base;
}

/* key, inputs, binary format and length */
#define HEAD (sizeof(u64) + sizeof(int) + sizeof(GLenum) + sizeof(GLint))

size_t gli_glsl_cprog_binary_size(const gli_cprog *_p)
{
	GLint binaryLength = 0;
	glGetProgramiv(CPROG(_p)->program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
	if (binaryLength < 1)
		return 0;
	return HEAD + (size_t)binaryLength;
}

size_t gli_glsl_cprog_binary_write(const gli_cprog *_p, char *_out, size_t _room)
{
	const gli_glsl_cprog *c = CPROG(_p);
	GLint binaryLength;
	GLenum binaryFormat;
	const u64 key = c->base.key;
	const int inputs = (int)c->inputs;

	if (_room <= HEAD)
		return 0;
	/* GL writes the binary where it is kept */
	glGetProgramBinary(c->program, (GLsizei)(_room - HEAD), &binaryLength, &binaryFormat, _out + HEAD);
	if (gli_gl_has_error() || binaryLength < 1)
		return 0;

	memcpy(_out, &key, sizeof(key));
	_out += sizeof(key);
	memcpy(_out, &inputs, sizeof(inputs));
	_out += sizeof(inputs);
	memcpy(_out, &binaryFormat, sizeof(binaryFormat));
	_out += sizeof(binaryFormat);
	memcpy(_out, &binaryLength, sizeof(binaryLength));
	return HEAD + (size_t)binaryLength;
}
