#include <stdlib.h>
#include <Config.h>
#include <gSP.h>
#include <gli_combiner.h>
#include "GLFunctions.h"
#include "opengl_Attributes.h"
#include "opengl_Drawer.h"

/* The vertices drawn from where they lie, GL pointed at them: again only
 * when a pointer moves, and the arrays turned on or off only when they
 * change. */

typedef struct gli_gl_unbuffered
{
	gli_gl_drawer base;
	gli_gl_attribs *attribs;
	const void *pointers[MaxAttribIndex];
} gli_gl_unbuffered;

static void attrib(gli_gl_unbuffered *_d, u32 _index, GLint _size, GLenum _type, GLsizei _stride, const void *_ptr)
{
	gli_gl_attribs_enable(_d->attribs, _index, 1);
	if (_d->pointers[_index] == _ptr)
		return;
	_d->pointers[_index] = _ptr;
	glVertexAttribPointer(_index, _size, _type, GL_FALSE, _stride, _ptr);
}

static void draw_triangles(gli_gl_drawer *_drawer, const gli_draw_triangles_params *_params)
{
	gli_gl_unbuffered *d = (gli_gl_unbuffered *)_drawer;
	const gli_cprog *combiner = _params->combiner;
	const SPVertex *v = _params->vertices;

	attrib(d, GLI_TRI_ATTRIB_POSITION, 4, GL_FLOAT, sizeof(SPVertex), &v->x);
	if (combiner->ops->usesShade(combiner))
		attrib(d, GLI_TRI_ATTRIB_COLOR, 4, GL_FLOAT, sizeof(SPVertex), _params->flatColors ? &v->flat_r : &v->r);
	else
		gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_COLOR, 0);
	if (combiner->ops->usesTexture(combiner))
		attrib(d, GLI_TRI_ATTRIB_TEXCOORD, 2, GL_FLOAT, sizeof(SPVertex), &v->s);
	else
		gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_TEXCOORD, 0);
	attrib(d, GLI_TRI_ATTRIB_MODIFY, 4, GL_BYTE, sizeof(SPVertex), &v->modify);

	if (gli_isHWLightingAllowed())
		glVertexAttrib1f(GLI_TRI_ATTRIB_NUMLIGHTS, (GLfloat)v->HWLight);

	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_POSITION, 0);
	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD0, 0);
	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD1, 0);

	if (_params->elements == NULL)
		glDrawArrays((GLenum)_params->mode, 0, _params->verticesCount);
	else
		glDrawElements((GLenum)_params->mode, _params->elementsCount, GL_UNSIGNED_SHORT, _params->elements);
}

static void draw_rects(gli_gl_drawer *_drawer, const gli_draw_rects_params *_params)
{
	gli_gl_unbuffered *d = (gli_gl_unbuffered *)_drawer;
	const gli_cprog *combiner = _params->combiner;
	const RectVertex *v = _params->vertices;

	attrib(d, GLI_RECT_ATTRIB_POSITION, 4, GL_FLOAT, sizeof(RectVertex), &v->x);
	if (_params->texrect && combiner->ops->usesTile(combiner, 0))
		attrib(d, GLI_RECT_ATTRIB_TEXCOORD0, 2, GL_FLOAT, sizeof(RectVertex), &v->s0);
	else
		gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD0, 0);
	if (_params->texrect && combiner->ops->usesTile(combiner, 1))
		attrib(d, GLI_RECT_ATTRIB_TEXCOORD1, 2, GL_FLOAT, sizeof(RectVertex), &v->s1);
	else
		gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD1, 0);

	gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_POSITION, 0);
	gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_COLOR, 0);
	gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_TEXCOORD, 0);
	gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_MODIFY, 0);

	glDrawArrays((GLenum)_params->mode, 0, _params->verticesCount);
}

static void draw_line(gli_gl_drawer *_drawer, f32 _width, SPVertex *_vertices)
{
	gli_gl_unbuffered *d = (gli_gl_unbuffered *)_drawer;

	attrib(d, GLI_TRI_ATTRIB_POSITION, 4, GL_FLOAT, sizeof(SPVertex), &_vertices->x);
	attrib(d, GLI_TRI_ATTRIB_COLOR, 4, GL_FLOAT, sizeof(SPVertex), &_vertices->r);
	gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_TEXCOORD, 0);
	gli_gl_attribs_enable(d->attribs, GLI_TRI_ATTRIB_MODIFY, 0);

	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_POSITION, 0);
	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD0, 0);
	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD1, 0);

	glLineWidth(_width);
	glDrawArrays(GL_LINES, 0, 2);
}

static void destroy(gli_gl_drawer *_drawer)
{
	free(_drawer);
}

static const gli_gl_drawer_ops unbuffered_ops = { draw_triangles, draw_rects, draw_line, destroy };

gli_gl_drawer *gli_gl_drawer_new_unbuffered(const gli_gl_info *_info, gli_gl_attribs *_attribs)
{
	static const u32 all[] = {
		GLI_TRI_ATTRIB_POSITION, GLI_TRI_ATTRIB_COLOR, GLI_TRI_ATTRIB_TEXCOORD,
		GLI_TRI_ATTRIB_NUMLIGHTS, GLI_TRI_ATTRIB_MODIFY,
		GLI_RECT_ATTRIB_POSITION, GLI_RECT_ATTRIB_TEXCOORD0, GLI_RECT_ATTRIB_TEXCOORD1
	};
	gli_gl_unbuffered *d = (gli_gl_unbuffered *)calloc(1, sizeof(*d));
	u32 i;
	(void)_info;
	if (d == NULL)
		return NULL;
	d->base.ops = &unbuffered_ops;
	d->attribs = _attribs;
	for (i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
		gli_gl_attribs_enable(_attribs, all[i], 0);
	return &d->base;
}
