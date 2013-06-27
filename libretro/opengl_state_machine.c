#include <assert.h>

#define NO_TRANSLATE
#include "SDL_opengles2.h"

#ifdef GLES
#define glClearDepth glClearDepthf
#define glDepthRange glDepthRangef
#endif

//glEnable, glDisable
static int CapState[SGL_CAP_MAX];
static const int CapTranslate[SGL_CAP_MAX] = 
{
    GL_TEXTURE_2D, GL_DEPTH_TEST, GL_BLEND, GL_POLYGON_OFFSET_FILL, GL_CULL_FACE, GL_SCISSOR_TEST
};

void sglEnable(GLenum cap)
{
    assert(cap < SGL_CAP_MAX);

    glEnable(CapTranslate[cap]);
    CapState[cap] = 1;
}

void sglDisable(GLenum cap)
{
    assert(cap < SGL_CAP_MAX);

    glDisable(CapTranslate[cap]);
    CapState[cap] = 0;
}

GLboolean sglIsEnabled(GLenum cap)
{
    assert(cap < SGL_CAP_MAX);
    return CapState[cap] ? GL_TRUE : GL_FALSE;
}

//VERTEX ATTRIB ARRAY
#define MAX_ATTRIB 4
#define ATTRIB_INITER(X) { X, X, X, X }

static GLint VertexAttribPointer_enabled[MAX_ATTRIB] = ATTRIB_INITER(0);
static GLint VertexAttribPointer_size[MAX_ATTRIB] = ATTRIB_INITER(4);
static GLenum VertexAttribPointer_type[MAX_ATTRIB] = ATTRIB_INITER(GL_FLOAT);
static GLboolean VertexAttribPointer_normalized[MAX_ATTRIB] = ATTRIB_INITER(GL_TRUE);
static GLsizei VertexAttribPointer_stride[MAX_ATTRIB] = ATTRIB_INITER(0);
static const GLvoid* VertexAttribPointer_pointer[MAX_ATTRIB] = ATTRIB_INITER(0);

void sglEnableVertexAttribArray(GLuint index)
{
    assert(index < MAX_ATTRIB);

    VertexAttribPointer_enabled[index] = 1;
    glEnableVertexAttribArray(index);
}

void sglDisableVertexAttribArray(GLuint index)
{
    assert(index < MAX_ATTRIB);

    VertexAttribPointer_enabled[index] = 0;
    glEnableVertexAttribArray(index);
}

void sglVertexAttribPointer(GLuint name, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer)
{
    assert(name < MAX_ATTRIB);

    VertexAttribPointer_size[name] = size;
    VertexAttribPointer_type[name] = type;
    VertexAttribPointer_normalized[name] = normalized;
    VertexAttribPointer_stride[name] = stride;
    VertexAttribPointer_pointer[name] = pointer;
    glVertexAttribPointer(name, size, type, normalized, stride, pointer);
}

//BLEND FUNC
static GLenum BlendFunc_sfactor = GL_ONE, BlendFunc_dfactor = GL_ZERO;
void sglBlendFunc(GLenum sfactor, GLenum dfactor)
{
    BlendFunc_sfactor = sfactor;
    BlendFunc_dfactor = dfactor;
    glBlendFunc(BlendFunc_sfactor, BlendFunc_dfactor);
}

//CLEAR COLOR
static GLclampf ClearColor_red = 0.0f, ClearColor_green = 0.0f, ClearColor_blue = 0.0f, ClearColor_alpha = 0.0f;
void sglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    ClearColor_red = red;
    ClearColor_green = green;
    ClearColor_blue = blue;
    ClearColor_alpha = alpha;

    glClearColor(ClearColor_red, ClearColor_green, ClearColor_blue, ClearColor_alpha);
}

//CLEAR DEPTH
static GLdouble ClearDepth_value = 1.0;
void sglClearDepth(GLdouble value)
{
    ClearDepth_value = value;
    glClearDepth(ClearDepth_value);
}

//CULL FACE
static GLenum CullFace_mode = GL_BACK;
void sglCullFace(GLenum mode)
{
    CullFace_mode = mode;
    glCullFace(CullFace_mode);
}

//DEPTH FUNC
static GLenum DepthFunc_func = GL_LESS;
void sglDepthFunc(GLenum func)
{
    DepthFunc_func = func;
    glDepthFunc(DepthFunc_func);
}

//DEPTH MASK
static GLboolean DepthMask_flag = GL_TRUE;
void sglDepthMask(GLboolean flag)
{
    DepthMask_flag = flag;
    glDepthMask(DepthMask_flag);
}

//DEPTH RANGE
static GLclampd DepthRange_nearVal = 0.0, DepthRange_farVal = 1.0;
void sglDepthRange(GLclampd nearVal, GLclampd farVal)
{
    DepthRange_nearVal = nearVal;
    DepthRange_farVal = farVal;
    glDepthRange(DepthRange_nearVal, DepthRange_farVal);
}

//FRONTFACE
static GLenum FrontFace_mode = GL_CCW;
void sglFrontFace(GLenum mode)
{
    FrontFace_mode = mode;
    glFrontFace(FrontFace_mode);
}

//POLYGON OFFSET
static GLfloat PolygonOffset_factor = 0.0f, PolygonOffset_units = 0.0f;
void sglPolygonOffset(GLfloat factor, GLfloat units)
{
    PolygonOffset_factor = factor;
    PolygonOffset_units = units;
    glPolygonOffset(PolygonOffset_factor, PolygonOffset_units);
}

//SCISSOR
static GLint Scissor_x = 0, Scissor_y = 0;
static GLsizei Scissor_width = 640, Scissor_height = 480;
void sglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    Scissor_x = x;
    Scissor_y = y;
    Scissor_width = width;
    Scissor_height = height;
    glScissor(Scissor_x, Scissor_y, Scissor_width, Scissor_height);
}

//USE PROGRAM
static GLuint UseProgram_program = 0;
void sglUseProgram(GLuint program)
{
    UseProgram_program = program;
    glUseProgram(program);
}

//VIEWPORT
static GLint Viewport_x = 0, Viewport_y = 0;
static GLsizei Viewport_width = 640, Viewport_height = 480;
void sglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    Viewport_x = x;
    Viewport_y = y;
    Viewport_width = width;
    Viewport_height = height;
    glViewport(Viewport_x, Viewport_y, Viewport_width, Viewport_height);
}

//ACTIVE TEXTURE
#define MAX_TEXTURE 4
static GLenum ActiveTexture_texture = 0;
void sglActiveTexture(GLenum texture)
{
    assert((texture - GL_TEXTURE0) < MAX_TEXTURE);

    ActiveTexture_texture = texture - GL_TEXTURE0;
    glActiveTexture(texture);
}

//BIND TEXTURE
static GLuint BindTexture_ids[MAX_TEXTURE];
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
        if (VertexAttribPointer_enabled[i]) glEnableVertexAttribArray(i);
        else                                glDisableVertexAttribArray(i);

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
    glScissor(Scissor_x, Scissor_y, Scissor_width, Scissor_height);
    glUseProgram(UseProgram_program);
    glViewport(Viewport_x, Viewport_y, Viewport_width, Viewport_height);

    for(int i = 0; i != SGL_CAP_MAX; i ++)
    {
        if(CapState[i]) glEnable(CapTranslate[i]);
        else            glDisable(CapTranslate[i]);
    }

    for (int i = 0; i < MAX_TEXTURE; i ++)
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
    for (int i = 0; i < MAX_TEXTURE; i ++)
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

