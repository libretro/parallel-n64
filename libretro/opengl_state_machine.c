#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define NO_TRANSLATE
#include "SDL_opengles2.h"

#ifdef GLES
#define glClearDepth glClearDepthf
#define glDepthRange glDepthRangef
#endif

// mupen64plus HACK: Fix potential crash at shutdown for
extern int stop;

// Glitch64 hacks ... :D
void vbo_draw();

//glEnable, glDisable
static int CapState[SGL_CAP_MAX];
static const int CapTranslate[SGL_CAP_MAX] = 
{
    GL_TEXTURE_2D, GL_DEPTH_TEST, GL_BLEND, GL_POLYGON_OFFSET_FILL, GL_CULL_FACE, GL_SCISSOR_TEST
};

void sglEnable(GLenum cap)
{
    vbo_draw();
    assert(cap < SGL_CAP_MAX);

    glEnable(CapTranslate[cap]);
    CapState[cap] = 1;
}

void sglDisable(GLenum cap)
{
    vbo_draw();
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
#define MAX_ATTRIB 8
#define ATTRIB_INITER(X) { X, X, X, X, X, X, X, X }

static GLint VertexAttribPointer_enabled[MAX_ATTRIB] = ATTRIB_INITER(0);
static GLint VertexAttribPointer_is4f[MAX_ATTRIB] = ATTRIB_INITER(0);
static GLint VertexAttribPointer_size[MAX_ATTRIB] = ATTRIB_INITER(4);
static GLenum VertexAttribPointer_type[MAX_ATTRIB] = ATTRIB_INITER(GL_FLOAT);
static GLboolean VertexAttribPointer_normalized[MAX_ATTRIB] = ATTRIB_INITER(GL_TRUE);
static GLsizei VertexAttribPointer_stride[MAX_ATTRIB] = ATTRIB_INITER(0);
static const GLvoid* VertexAttribPointer_pointer[MAX_ATTRIB] = ATTRIB_INITER(0);
static GLfloat VertexAttribPointer_4f[MAX_ATTRIB][4];

void sglEnableVertexAttribArray(GLuint index)
{
    vbo_draw();
    assert(index < MAX_ATTRIB);

    VertexAttribPointer_enabled[index] = 1;
    glEnableVertexAttribArray(index);
}

void sglDisableVertexAttribArray(GLuint index)
{
    vbo_draw();
    assert(index < MAX_ATTRIB);

    VertexAttribPointer_enabled[index] = 0;
    glDisableVertexAttribArray(index);
}

void sglVertexAttribPointer(GLuint name, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer)
{
    assert(name < MAX_ATTRIB);

    VertexAttribPointer_is4f[name] = 0;
    VertexAttribPointer_size[name] = size;
    VertexAttribPointer_type[name] = type;
    VertexAttribPointer_normalized[name] = normalized;
    VertexAttribPointer_stride[name] = stride;
    VertexAttribPointer_pointer[name] = pointer;
    glVertexAttribPointer(name, size, type, normalized, stride, pointer);
}

void sglVertexAttrib4f(GLuint name, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    assert(name < MAX_ATTRIB);

    VertexAttribPointer_is4f[name] = 1;
    VertexAttribPointer_4f[name][0] = x;
    VertexAttribPointer_4f[name][1] = y;
    VertexAttribPointer_4f[name][2] = z;
    VertexAttribPointer_4f[name][3] = w;
    glVertexAttrib4f(name, x, y, z, w);
}

void sglVertexAttrib4fv(GLuint name, GLfloat* v)
{
    assert(name < MAX_ATTRIB);

    VertexAttribPointer_is4f[name] = 1;
    memcpy(VertexAttribPointer_4f[name], v, sizeof(GLfloat) * 4);
    glVertexAttrib4fv(name, v);
}


// BIND FRAME BUFFER
extern GLuint retro_get_fbo_id();
static GLuint Framebuffer_framebuffer = 0;
void sglBindFramebuffer(GLenum target, GLuint framebuffer)
{
    vbo_draw();
    assert(target == GL_FRAMEBUFFER);

   if (!stop)
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer ? framebuffer : retro_get_fbo_id());
}

//BLEND FUNC
static GLenum BlendFunc_srcRGB = GL_ONE,  BlendFunc_srcAlpha = GL_ONE;
static GLenum BlendFunc_dstRGB = GL_ZERO, BlendFunc_dstAlpha = GL_ZERO;
void sglBlendFunc(GLenum sfactor, GLenum dfactor)
{
    vbo_draw();
    BlendFunc_srcRGB = BlendFunc_srcAlpha = sfactor;
    BlendFunc_dstRGB = BlendFunc_dstAlpha = dfactor;
    glBlendFunc(BlendFunc_srcRGB, BlendFunc_dstRGB);
}

void sglBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    vbo_draw();
    BlendFunc_srcRGB = srcRGB;
    BlendFunc_dstRGB = dstRGB;
    BlendFunc_srcAlpha = srcAlpha;
    BlendFunc_dstAlpha = dstAlpha;
    glBlendFuncSeparate(BlendFunc_srcRGB, BlendFunc_dstRGB, BlendFunc_srcAlpha, BlendFunc_dstAlpha);
}


//CLEAR COLOR
static GLclampf ClearColor_red = 0.0f, ClearColor_green = 0.0f, ClearColor_blue = 0.0f, ClearColor_alpha = 0.0f;
void sglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    vbo_draw();
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
    vbo_draw();
    ClearDepth_value = value;
    glClearDepth(ClearDepth_value);
}

//COLOR MASK
static GLboolean ColorMask_red = GL_TRUE;
static GLboolean ColorMask_green = GL_TRUE;
static GLboolean ColorMask_blue = GL_TRUE;
static GLboolean ColorMask_alpha = GL_TRUE;
void sglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    vbo_draw();
    ColorMask_red = red;
    ColorMask_green = green;
    ColorMask_blue = blue;
    ColorMask_alpha = alpha;
    glColorMask(ColorMask_red, ColorMask_green, ColorMask_blue, ColorMask_alpha);
}

//CULL FACE
static GLenum CullFace_mode = GL_BACK;
void sglCullFace(GLenum mode)
{
    vbo_draw();
    CullFace_mode = mode;
    glCullFace(CullFace_mode);
}

//DEPTH FUNC
static GLenum DepthFunc_func = GL_LESS;
void sglDepthFunc(GLenum func)
{
    vbo_draw();
    DepthFunc_func = func;
    glDepthFunc(DepthFunc_func);
}

//DEPTH MASK
static GLboolean DepthMask_flag = GL_TRUE;
void sglDepthMask(GLboolean flag)
{
    vbo_draw();
    DepthMask_flag = flag;
    glDepthMask(DepthMask_flag);
}

//DEPTH RANGE
static GLclampd DepthRange_nearVal = 0.0, DepthRange_farVal = 1.0;
void sglDepthRange(GLclampd nearVal, GLclampd farVal)
{
    vbo_draw();
    DepthRange_nearVal = nearVal;
    DepthRange_farVal = farVal;
    glDepthRange(DepthRange_nearVal, DepthRange_farVal);
}

//FRONTFACE
static GLenum FrontFace_mode = GL_CCW;
void sglFrontFace(GLenum mode)
{
    vbo_draw();
    FrontFace_mode = mode;
    glFrontFace(FrontFace_mode);
}

//POLYGON OFFSET
static GLfloat PolygonOffset_factor = 0.0f, PolygonOffset_units = 0.0f;
void sglPolygonOffset(GLfloat factor, GLfloat units)
{
    vbo_draw();
    PolygonOffset_factor = factor;
    PolygonOffset_units = units;
    glPolygonOffset(PolygonOffset_factor, PolygonOffset_units);
}

//SCISSOR
static GLint Scissor_x = 0, Scissor_y = 0;
static GLsizei Scissor_width = 640, Scissor_height = 480;
void sglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    vbo_draw();
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
    vbo_draw();
    UseProgram_program = program;
    glUseProgram(program);
}

//VIEWPORT
static GLint Viewport_x = 0, Viewport_y = 0;
static GLsizei Viewport_width = 640, Viewport_height = 480;
void sglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    vbo_draw();
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
    vbo_draw();
    assert((texture - GL_TEXTURE0) < MAX_TEXTURE);

    ActiveTexture_texture = texture - GL_TEXTURE0;
    glActiveTexture(texture);
}

struct tex_map
{
   unsigned address;
   GLuint tex;
};
static struct tex_map *texture_map;
static size_t texture_map_size;
static size_t texture_map_cap;

static GLuint find_tex_from_address(unsigned address)
{
   for (size_t i = 0; i < texture_map_size; i++)
   {
      if (texture_map[i].address == address)
         return texture_map[i].tex;
   }
   return 0;
}

static void delete_tex_from_address(unsigned address)
{
   for (size_t i = 0; i < texture_map_size; i++)
   {
      if (texture_map[i].address == address)
      {
         glDeleteTextures(1, &texture_map[i].tex);
         memmove(texture_map + i, texture_map + i + 1, (texture_map_size - (i + 1)) * sizeof(*texture_map));
         texture_map_size--;
         return;
      }
   }
}

GLuint sglAddressToTex(unsigned address)
{
   return find_tex_from_address(address);
}

//BIND TEXTURE
static GLuint BindTexture_ids[MAX_TEXTURE];
void sglBindTextureGlide(GLenum target, GLuint texture)
{
    vbo_draw();
    assert(target == GL_TEXTURE_2D);

    if (texture)
    {
       // Glitch64 is broken. It never calls glGenTextures, and simply assumes that you can bind to whatever ...
       // This makes no sense, so we'll fix it for them! <_<
       GLuint tex = find_tex_from_address(texture);

       if (!tex)
       {
          if (texture_map_size >= texture_map_cap)
          {
             texture_map_cap = 2 * (texture_map_cap + 1);
             texture_map = realloc(texture_map, sizeof(*texture_map) * texture_map_cap);
          }
          texture_map[texture_map_size].address = texture;
          glGenTextures(1, &tex);
          texture_map[texture_map_size++].tex = tex;
       }
       BindTexture_ids[ActiveTexture_texture] = tex;
    }
    else
       BindTexture_ids[ActiveTexture_texture] = texture;

    glBindTexture(target, BindTexture_ids[ActiveTexture_texture]);
}

void sglBindTexture(GLenum target, GLuint texture)
{
    vbo_draw();
    assert(target == GL_TEXTURE_2D);
    BindTexture_ids[ActiveTexture_texture] = texture;
    glBindTexture(target, BindTexture_ids[ActiveTexture_texture]);
}

#ifdef GLIDE64 // Avoid texture id conflicts
void sglDeleteTextures(GLuint n, const GLuint* ids)
{
    for (int i = 0; i < n; i++)
       delete_tex_from_address(ids[i]);
}
#endif


//ENTER/EXIT

void sglEnter()
{
   if (stop)
      return;

    for (int i = 0; i < MAX_ATTRIB; i ++)
    {
        if (VertexAttribPointer_enabled[i]) glEnableVertexAttribArray(i);
        else                                glDisableVertexAttribArray(i);

        if (!VertexAttribPointer_is4f[i])
            glVertexAttribPointer(i, VertexAttribPointer_size[i], VertexAttribPointer_type[i], VertexAttribPointer_normalized[i],
                                 VertexAttribPointer_stride[i], VertexAttribPointer_pointer[i]);
        else
            glVertexAttrib4f(i, VertexAttribPointer_4f[i][0], VertexAttribPointer_4f[i][1], VertexAttribPointer_4f[i][2], VertexAttribPointer_4f[i][3]);
    }

    sglBindFramebuffer(GL_FRAMEBUFFER, Framebuffer_framebuffer); // < sgl is intentional

    glBlendFuncSeparate(BlendFunc_srcRGB, BlendFunc_dstRGB, BlendFunc_srcAlpha, BlendFunc_dstAlpha);
    glClearColor(ClearColor_red, ClearColor_green, ClearColor_blue, ClearColor_alpha);
    glClearDepth(ClearDepth_value);
    glColorMask(ColorMask_red, ColorMask_green, ColorMask_blue, ColorMask_alpha);
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
   if (stop)
      return;

    for (int i = 0; i < SGL_CAP_MAX; i ++)
        glDisable(CapTranslate[i]);

    glBlendFunc(GL_ONE, GL_ZERO);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glCullFace(GL_BACK);
    glDepthMask(GL_TRUE);
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

    glBindFramebuffer(GL_FRAMEBUFFER, retro_get_fbo_id());
}

