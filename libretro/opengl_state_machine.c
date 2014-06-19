#include <stdlib.h>
#include <string.h>

#define NO_TRANSLATE 1

#include "glsym/glsym.h"
#include "opengl_state_machine.h"

extern int stop;

//forward declarations
extern void vbo_draw(void);

static int CapState[SGL_CAP_MAX];
static const int CapTranslate[SGL_CAP_MAX] = 
{
    GL_TEXTURE_2D, GL_DEPTH_TEST, GL_BLEND, GL_POLYGON_OFFSET_FILL, GL_CULL_FACE, GL_SCISSOR_TEST
};

void sglGenerateMipmap(GLenum target)
{
   glGenerateMipmap(target);
}

void sglUniform1f(GLint location, GLfloat v0)
{
   glUniform1f(location, v0);
}

void sglUniform1i(GLint location, GLint v0)
{
   glUniform1i(location, v0);
}

void sglUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
   glUniform2f(location, v0, v1);
}

void sglUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
   glUniform3f(location, v0, v1, v2);
}

void sglUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
   glUniform4f(location, v0, v1, v2, v3);
}

void sglUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
   glUniform4fv(location, count, value);
}

void sglDeleteShader(GLuint shader)
{
   glDeleteShader(shader);
}

void sglDeleteProgram(GLuint program)
{
   glDeleteProgram(program);
}

GLuint sglCreateShader(GLenum shaderType)
{
   return glCreateShader(shaderType);
}

GLuint sglCreateProgram(void)
{
   return glCreateProgram();
}

void sglCompileShader(GLuint shader)
{
   glCompileShader(shader);
}

void sglLinkProgram(GLuint program)
{
   glLinkProgram(program);
}

void sglAttachShader(GLuint program, GLuint shader)
{
   glAttachShader(program, shader);
}

void sglGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
   glGetShaderiv(shader, pname, params);
}

void sglGetProgramiv(GLuint shader, GLenum pname, GLint *params)
{
   glGetProgramiv(shader, pname, params);
}

void sglShaderSource(GLuint shader, GLsizei count, const GLchar **string, const GLint *length)
{
   return glShaderSource(shader, count, string, length);
}

void sglBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
   glBindAttribLocation(program, index, name);
}

void sglGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog)
{
   glGetShaderInfoLog(shader, maxLength, length, infoLog);
}

void sglGetProgramInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog)
{
   glGetProgramInfoLog(shader, maxLength, length, infoLog);
}

GLint sglGetUniformLocation(GLuint program, const GLchar *name)
{
   return glGetUniformLocation(program, name);
}

void sglEnable(GLenum cap)
{
    vbo_draw();

    glEnable(CapTranslate[cap]);
    CapState[cap] = 1;
}

void sglDisable(GLenum cap)
{
    vbo_draw();

    glDisable(CapTranslate[cap]);
    CapState[cap] = 0;
}

GLboolean sglIsEnabled(GLenum cap)
{
    return CapState[cap] ? GL_TRUE : GL_FALSE;
}

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

    VertexAttribPointer_enabled[index] = 1;
    glEnableVertexAttribArray(index);
}

void sglDisableVertexAttribArray(GLuint index)
{
    vbo_draw();

    VertexAttribPointer_enabled[index] = 0;
    glDisableVertexAttribArray(index);
}

void sglVertexAttribPointer(GLuint name, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer)
{
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
    VertexAttribPointer_is4f[name] = 1;
    VertexAttribPointer_4f[name][0] = x;
    VertexAttribPointer_4f[name][1] = y;
    VertexAttribPointer_4f[name][2] = z;
    VertexAttribPointer_4f[name][3] = w;
    glVertexAttrib4f(name, x, y, z, w);
}

void sglVertexAttrib4fv(GLuint name, GLfloat* v)
{
    VertexAttribPointer_is4f[name] = 1;
    memcpy(VertexAttribPointer_4f[name], v, sizeof(GLfloat) * 4);
    glVertexAttrib4fv(name, v);
}


extern GLuint retro_get_fbo_id();
static GLuint Framebuffer_framebuffer = 0;
void sglBindFramebuffer(GLenum target, GLuint framebuffer)
{
   vbo_draw();

   if (!stop)
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer ? framebuffer : retro_get_fbo_id());
}

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


static GLclampf ClearColor_red = 0.0f, ClearColor_green = 0.0f, ClearColor_blue = 0.0f, ClearColor_alpha = 0.0f;
void sglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
   vbo_draw();
   glClearColor(red, green, blue, alpha);
   ClearColor_red = red;
   ClearColor_green = green;
   ClearColor_blue = blue;
   ClearColor_alpha = alpha;
}

static GLdouble ClearDepth_value = 1.0;
void sglClearDepth(GLdouble depth)
{
   vbo_draw();
#ifdef HAVE_OPENGLES2
   glClearDepthf(depth);
#else
   glClearDepth(depth);
#endif
   ClearDepth_value = depth;
}

static GLboolean ColorMask_red = GL_TRUE;
static GLboolean ColorMask_green = GL_TRUE;
static GLboolean ColorMask_blue = GL_TRUE;
static GLboolean ColorMask_alpha = GL_TRUE;

void sglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
   vbo_draw();
   glColorMask(red, green, blue, alpha);
   ColorMask_red = red;
   ColorMask_green = green;
   ColorMask_blue = blue;
   ColorMask_alpha = alpha;
}

static GLenum CullFace_mode = GL_BACK;

void sglCullFace(GLenum mode)
{
   vbo_draw();
   glCullFace(mode);
   CullFace_mode = mode;
}

static GLenum DepthFunc_func = GL_LESS;
void sglDepthFunc(GLenum func)
{
  vbo_draw();
  glDepthFunc(func);
  DepthFunc_func = func;
}

static GLboolean DepthMask_flag = GL_TRUE;
void sglDepthMask(GLboolean flag)
{
  vbo_draw();
  glDepthMask(flag);
  DepthMask_flag = flag;
}

static GLclampd DepthRange_zNear = 0.0, DepthRange_zFar = 1.0;

void sglDepthRange(GLclampd zNear, GLclampd zFar)
{
   vbo_draw();
#ifdef HAVE_OPENGLES2
   glDepthRangef(zNear, zFar);
#else
   glDepthRange(zNear, zFar);
#endif
   DepthRange_zNear = zNear;
   DepthRange_zFar = zFar;
}

static GLenum FrontFace_mode = GL_CCW;
void sglFrontFace(GLenum mode)
{
   vbo_draw();
   glFrontFace(mode);
   FrontFace_mode = mode;
}

static GLfloat PolygonOffset_factor = 0.0f, PolygonOffset_units = 0.0f;
void sglPolygonOffset(GLfloat factor, GLfloat units)
{
  vbo_draw();
  glPolygonOffset(factor, units);
  PolygonOffset_factor = factor;
  PolygonOffset_units = units;
}

static GLint Scissor_x = 0, Scissor_y = 0;
static GLsizei Scissor_width = 640, Scissor_height = 480;
void sglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
  vbo_draw();
  glScissor(x, y, width, height);
  Scissor_x = x;
  Scissor_y = y;
  Scissor_width = width;
  Scissor_height = height;
}

static GLuint UseProgram_program = 0;
void sglUseProgram(GLuint program)
{
   vbo_draw();
   glUseProgram(program);
   UseProgram_program = program;
}

static GLint Viewport_x = 0, Viewport_y = 0;
static GLsizei Viewport_width = 640, Viewport_height = 480;
void sglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
   vbo_draw();
   glViewport(x, y, width, height);
   Viewport_x = x;
   Viewport_y = y;
   Viewport_width = width;
   Viewport_height = height;
}

#define MAX_TEXTURE 4
static GLenum ActiveTexture_texture = 0;
void sglActiveTexture(GLenum texture)
{
   vbo_draw();
   glActiveTexture(texture);
   ActiveTexture_texture = texture - GL_TEXTURE0;
}

static GLuint BindTexture_ids[MAX_TEXTURE];
void sglBindTexture(GLenum target, GLuint texture)
{
   vbo_draw();
   glBindTexture(target, texture);
   BindTexture_ids[ActiveTexture_texture] = texture;
}

void sglDeleteRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   glDeleteRenderbuffers(n, renderbuffers);
}

void sglGenFramebuffers(GLsizei n, GLuint *ids)
{
   glGenFramebuffers(n, ids);
}

void sglGenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   glGenRenderbuffers(n, renderbuffers);
}

void sglBindRenderbuffer(GLenum target, GLuint renderbuffer)
{
   glBindRenderbuffer(target, renderbuffer);
}

void sglRenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width, GLsizei height)
{
   glRenderbufferStorage(target, internalFormat, width, height);
}

void sglFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
   glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

GLenum sglCheckFramebufferStatus(GLenum target)
{
   return glCheckFramebufferStatus(target);
}

void sglDeleteFramebuffers(GLsizei n, GLuint *framebuffers)
{
   glDeleteFramebuffers(n, framebuffers);
}

void sglFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
   glFramebufferTexture2D(target, attachment, textarget, texture, level);
}


#if 0
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
   size_t i;
   for (i = 0; i < texture_map_size; i++)
   {
      if (texture_map[i].address == address)
         return texture_map[i].tex;
   }
   return 0;
}

static void delete_tex_from_address(unsigned address)
{
   size_t i;
   for (i = 0; i < texture_map_size; i++)
   {
      if (texture_map[i].address == address)
      {
         glDeleteTextures(1, &texture_map[i].tex);
         memmove(texture_map + i, texture_map + i + 1, (texture_map_size - (i + 1)) * sizeof(*texture_map));
         texture_map_size--;
         return;
      }
   }

   glDeleteTextures(1, &address);
}
#endif


//ENTER/EXIT

void sglEnter(void)
{
   int i;

    for (i = 0; i < MAX_ATTRIB; i ++)
    {
        if (VertexAttribPointer_enabled[i])
           glEnableVertexAttribArray(i);
        else
           glDisableVertexAttribArray(i);

        if (!VertexAttribPointer_is4f[i])
            glVertexAttribPointer(i, VertexAttribPointer_size[i], VertexAttribPointer_type[i],
                  VertexAttribPointer_normalized[i], VertexAttribPointer_stride[i], VertexAttribPointer_pointer[i]);
        else
            glVertexAttrib4f(i, VertexAttribPointer_4f[i][0], VertexAttribPointer_4f[i][1],
                  VertexAttribPointer_4f[i][2], VertexAttribPointer_4f[i][3]);
    }

    sglBindFramebuffer(GL_FRAMEBUFFER, Framebuffer_framebuffer); // < sgl is intentional

    glBlendFuncSeparate(BlendFunc_srcRGB, BlendFunc_dstRGB, BlendFunc_srcAlpha, BlendFunc_dstAlpha);
    glClearColor(ClearColor_red, ClearColor_green, ClearColor_blue, ClearColor_alpha);
    sglClearDepth(ClearDepth_value);
    glColorMask(ColorMask_red, ColorMask_green, ColorMask_blue, ColorMask_alpha);
    glCullFace(CullFace_mode);
    glDepthFunc(DepthFunc_func);
    glDepthMask(DepthMask_flag);
    sglDepthRange(DepthRange_zNear, DepthRange_zFar);
    glFrontFace(FrontFace_mode);
    glPolygonOffset(PolygonOffset_factor, PolygonOffset_units);
    glScissor(Scissor_x, Scissor_y, Scissor_width, Scissor_height);
    glUseProgram(UseProgram_program);
    glViewport(Viewport_x, Viewport_y, Viewport_width, Viewport_height);

    for(i = 0; i != SGL_CAP_MAX; i ++)
    {
        if (CapState[i])
           glEnable(CapTranslate[i]);
        else
           glDisable(CapTranslate[i]);
    }

    for (i = 0; i < MAX_TEXTURE; i ++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, BindTexture_ids[i]);
        glEnable(GL_TEXTURE_2D);
    }

    glActiveTexture(GL_TEXTURE0 + ActiveTexture_texture);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void sglExit(void)
{
   int i;

    for (i = 0; i < SGL_CAP_MAX; i ++)
        glDisable(CapTranslate[i]);

    glBlendFunc(GL_ONE, GL_ZERO);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glCullFace(GL_BACK);
    glDepthMask(GL_TRUE);
    sglDepthRange(0, 1);
    glFrontFace(GL_CCW);
    glPolygonOffset(0, 0);
    glUseProgram(0);

    // Clear textures
    for (i = 0; i < MAX_TEXTURE; i ++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);

    for (i = 0; i < MAX_ATTRIB; i ++)
        glDisableVertexAttribArray(i);

    glBindFramebuffer(GL_FRAMEBUFFER, retro_get_fbo_id());
}
