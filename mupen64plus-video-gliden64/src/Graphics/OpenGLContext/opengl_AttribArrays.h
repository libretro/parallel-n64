#ifndef GLI_GL_ATTRIB_ARRAYS_H
#define GLI_GL_ATTRIB_ARRAYS_H

/* Which vertex attribute arrays are on, so a draw turns one on or off only
 * when it changes. C89, inline for the drawers. */

#include <Types.h>
#include <retro_inline.h>
#include "GLFunctions.h"
#include "opengl_Attributes.h"

#define GLI_ATTRIB_UNKNOWN 0xFFFFFFFFu

typedef struct gli_gl_attribs
{
	u32 state[MaxAttribIndex];   /* 0 off, 1 on, GLI_ATTRIB_UNKNOWN */
} gli_gl_attribs;

static INLINE void gli_gl_attribs_reset(gli_gl_attribs *_attribs)
{
	u32 i;
	for (i = 0; i < MaxAttribIndex; ++i)
		_attribs->state[i] = GLI_ATTRIB_UNKNOWN;
}

static INLINE void gli_gl_attribs_enable(gli_gl_attribs *_attribs, u32 _index, int _enable)
{
	const u32 on = _enable ? 1u : 0u;
	if (_attribs->state[_index] == on)
		return;
	_attribs->state[_index] = on;
	if (on)
		glEnableVertexAttribArray(_index);
	else
		glDisableVertexAttribArray(_index);
}

#endif
