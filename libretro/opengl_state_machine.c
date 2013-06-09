#include <assert.h>

#define NO_TRANSLATE
#include "SDL_opengles2.h"

//glEnable, glDisable
static int CapState[SGL_CAP_MAX];
static const int CapTranslate[SGL_CAP_MAX] = 
{
    GL_TEXTURE_2D, GL_DEPTH_TEST, GL_BLEND, GL_POLYGON_OFFSET_FILL, GL_CULL_FACE, GL_SCISSOR_TEST
};

void sglEnable(GLenum cap)
{
    assert(cap < SGL_CAP_MAX);

    if(!CapState[cap])
    {
        glEnable(CapTranslate[cap]);
        CapState[cap] = 1;
    }
}

void sglDisable(GLenum cap)
{
    assert(cap < SGL_CAP_MAX);

    if(CapState[cap])
    {
        glDisable(CapTranslate[cap]);
        CapState[cap] = 0;
    }
}

GLboolean sglIsEnabled(GLenum cap)
{
    assert(cap < SGL_CAP_MAX);
    return CapState[cap] ? GL_TRUE : GL_FALSE;
}

//VERTEX ATTRIB ARRAY
#define MAX_ATTRIB 4
static GLint VertexAttribPointer_enabled[MAX_ATTRIB];
static GLint VertexAttribPointer_size[MAX_ATTRIB];
static GLenum VertexAttribPointer_type[MAX_ATTRIB];
static GLboolean VertexAttribPointer_normalized[MAX_ATTRIB];
static GLsizei VertexAttribPointer_stride[MAX_ATTRIB];
static const GLvoid* VertexAttribPointer_pointer[MAX_ATTRIB];

void sglEnableVertexAttribArray(GLuint index)
{
    VertexAttribPointer_enabled[index] = 1;
    glEnableVertexAttribArray(index);
}

void sglDisableVertexAttribArray(GLuint index)
{
    VertexAttribPointer_enabled[index] = 0;
    glEnableVertexAttribArray(index);
}

void sglVertexAttribPointer(GLuint name, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer)
{
    VertexAttribPointer_size[name] = size;
    VertexAttribPointer_type[name] = type;
    VertexAttribPointer_normalized[name] = normalized;
    VertexAttribPointer_stride[name] = stride;
    VertexAttribPointer_pointer[name] = pointer;
    glVertexAttribPointer(name, size, type, normalized, stride, pointer);
}

//BLEND FUNC
static GLenum BlendFunc_sfactor, BlendFunc_dfactor;
void sglBlendFunc(GLenum sfactor, GLenum dfactor)
{
    BlendFunc_sfactor = sfactor;
    BlendFunc_dfactor = dfactor;
    glBlendFunc(sfactor, dfactor);
}

//CLEAR COLOR
static GLclampf ClearColor_red, ClearColor_green, ClearColor_blue, ClearColor_alpha;
void sglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    ClearColor_red = red;
    ClearColor_green = green;
    ClearColor_blue = blue;
    ClearColor_alpha = alpha;

    glClearColor(red, green, blue, alpha);
}

//CLEAR DEPTH
static GLdouble ClearDepth_value;
void sglClearDepth(GLdouble value)
{
    ClearDepth_value = value;
    glClearDepth(value);
}

//CULL FACE
static GLenum CullFace_mode;
void sglCullFace(GLenum mode)
{
    CullFace_mode = mode;
    glCullFace(mode);
}

//DEPTH FUNC
static GLenum DepthFunc_func;
void sglDepthFunc(GLenum func)
{
    DepthFunc_func = func;
    glDepthFunc(func);
}

//DEPTH MASK
static GLboolean DepthMask_flag;
void sglDepthMask(GLboolean flag)
{
    DepthMask_flag = flag;
    glDepthMask(flag);
}

//DEPTH RANGE
static GLclampd DepthRange_nearVal, DepthRange_farVal;
void sglDepthRange(GLclampd nearVal, GLclampd farVal)
{
    DepthRange_nearVal = nearVal;
    DepthRange_farVal = farVal;
    glDepthRange(nearVal, farVal);
}

//FRONTFACE
static GLenum FrontFace_mode = GL_CCW;
void sglFrontFace(GLenum mode)
{
    FrontFace_mode = mode;
    glFrontFace(mode);
}

//POLYGON OFFSET
static GLfloat PolygonOffset_factor, PolygonOffset_units;
void sglPolygonOffset(GLfloat factor, GLfloat units)
{
    PolygonOffset_factor = factor;
    PolygonOffset_units = units;
    glPolygonOffset(factor, units);
}

//SCISSOR
static GLint Scissor_x, Scissor_y;
static GLsizei Scissor_width, Scissor_height;
void sglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    Scissor_x = x;
    Scissor_y = y;
    Scissor_width = width;
    Scissor_height = height;
    glScissor(x, y, width, height);
}

//USE PROGRAM
static GLuint UseProgram_program;
void sglUseProgram(GLuint program)
{
    UseProgram_program = program;
    glUseProgram(program);
}

//VIEWPORT
static GLint Viewport_x, Viewport_y;
static GLsizei Viewport_width, Viewport_height;
void sglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    Viewport_x = x;
    Viewport_y = y;
    Viewport_width = width;
    Viewport_height = height;
    glViewport(x, y, width, height);
}

#define TEXTURE_MAX 4

//ACTIVE TEXTURE
static GLenum ActiveTexture_texture;
void sglActiveTexture(GLenum texture)
{
    assert((texture - GL_TEXTURE0) < TEXTURE_MAX);

    ActiveTexture_texture = texture - GL_TEXTURE0;
    glActiveTexture(texture);
}

//BIND TEXTURE
static GLuint BindTexture_ids[TEXTURE_MAX];
void sglBindTexture(GLenum target, GLuint texture)
{
    assert(target == GL_TEXTURE_2D);

    BindTexture_ids[ActiveTexture_texture] = texture;
    glBindTexture(target, texture);
}

//ENTER/EXIT

void sglEnter()
{
    for (int i = 0; i < MAX_ATTRIB; i ++)
    {
        if (VertexAttribPointer_enabled[i])
            glEnableVertexAttribArray(i);
        else
            glDisableVertexAttribArray(i);

        glVertexAttribPointer(i, VertexAttribPointer_size[i], VertexAttribPointer_type[i], VertexAttribPointer_normalized[i],
                                 VertexAttribPointer_stride[i], VertexAttribPointer_pointer[i]);        
    }

    glBlendFunc(BlendFunc_sfactor, BlendFunc_dfactor);
    glClearColor(ClearColor_red, ClearColor_green, ClearColor_blue, ClearColor_alpha);
    glClearDepth(ClearDepth_value);
    glCullFace(CullFace_mode);
    glDepthFunc(DepthFunc_func);
    glDepthMask(DepthMask_flag);
    glDepthRange(DepthRange_nearVal, DepthRange_farVal);
    glFrontFace(FrontFace_mode);
    glPolygonOffset(PolygonOffset_factor, PolygonOffset_units);
    glViewport(Viewport_x, Viewport_y, Viewport_width, Viewport_height);
    glScissor(Scissor_x, Scissor_y, Scissor_width, Scissor_height);
    glUseProgram(UseProgram_program);

    for(int i = 0; i != SGL_CAP_MAX; i ++)
    {
        if(CapState[i])
            glEnable(CapTranslate[i]);
        else
            glDisable(CapTranslate[i]);
    }

    for (int i = 0; i < TEXTURE_MAX; i ++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, BindTexture_ids[i]);
        glEnable(GL_TEXTURE_2D);
    }

    glActiveTexture(GL_TEXTURE0 + ActiveTexture_texture);

    //
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void sglExit()
{
    for (int i = 0; i < SGL_CAP_MAX; i ++)
        glDisable(CapTranslate[i]);

    glBlendFunc(GL_ONE, GL_ZERO);
    glCullFace(GL_BACK);
    glDepthRange(0, 1);
    glFrontFace(GL_CCW);
    glPolygonOffset(0, 0);
    glUseProgram(0);

    // Clear textures
    for (int i = 0; i < TEXTURE_MAX; i ++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < MAX_ATTRIB; i ++)
    {
        glDisableVertexAttribArray(i);
    }
}

