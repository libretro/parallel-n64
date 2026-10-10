/* cc -std=c99 -DHAVE_OPENGLES -DHAVE_OPENGLES3 -Ilibretro-common/include
 * tools/test_glsm_gles3.c libretro-common/glsym/glsym_es3.c
 * libretro-common/glsym/rglgen.c -lEGL -lGLESv2 -o test_glsm_gles3
 * EGL_PLATFORM=surfaceless ./test_glsm_gles3
 */
#include <assert.h>
#include <EGL/egl.h>
#include <glsym/glsym.h>
#ifndef GLSM_SOURCE
#define GLSM_SOURCE "../libretro-common/glsm/glsm.c"
#endif
#include GLSM_SOURCE

retro_log_printf_t log_cb;
static GLuint target;
static uintptr_t framebuffer(void) { return target; }
static retro_proc_address_t resolve(const char *name)
{
   return (retro_proc_address_t)eglGetProcAddress(name);
}

static void check_binding(GLenum pname, GLuint expected)
{
   GLint actual;
   glGetIntegerv(pname, &actual);
   assert((GLuint)actual == expected);
}

static GLuint shader(GLenum type, const char *source)
{
   GLuint result = glCreateShader(type);
   GLint ok;
   glShaderSource(result, 1, &source, NULL);
   glCompileShader(result);
   glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
   assert(ok);
   return result;
}

static void check_pixel(void)
{
   GLubyte pixel[4];
   glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
   assert(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0);
   assert(glGetError() == GL_NO_ERROR);
}

int main(void)
{
   const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
   const EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
   const EGLint surface_attrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLConfig config;
   EGLint count;
   EGLContext context;
   EGLSurface surface;
   GLuint texture, frontend_vao, frontend_vbo, core_vao, core_vbo, core_ebo;
   GLuint program, vs, fs;
   GLfloat vertices[] = { -1, -1, 1, -1, 0, 1 };
   GLushort indices[] = { 0, 1, 2 };
   GLint captured;
   void *pointer;
   unsigned frame;

   assert(eglInitialize(display, NULL, NULL));
   assert(eglBindAPI(EGL_OPENGL_ES_API));
   assert(eglChooseConfig(display, config_attrs, &config, 1, &count) && count);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
   surface = eglCreatePbufferSurface(display, config, surface_attrs);
   assert(eglMakeCurrent(display, surface, surface, context));
   hw_render.get_proc_address = resolve;
   hw_render.get_current_framebuffer = framebuffer;
   rglgen_resolve_symbols(resolve);
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &target);
   glBindFramebuffer(GL_FRAMEBUFFER, target);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
   glsm_state_setup();
   while (glGetError() != GL_NO_ERROR) {}
   vs = shader(GL_VERTEX_SHADER, "#version 300 es\nlayout(location=0) in vec2 p;"
      "void main(){gl_Position=vec4(p,0,1);}");
   fs = shader(GL_FRAGMENT_SHADER, "#version 300 es\nprecision mediump float;"
      "out vec4 c;void main(){c=vec4(0,1,0,1);}");
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   rglUseProgram(program);
   rglViewport(0, 0, 16, 16);

   glGenVertexArrays(1, &frontend_vao);
   glGenBuffers(1, &frontend_vbo);
   rglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
   rglEnableVertexAttribArray(0);
   for (frame = 0; frame < 10; ++frame)
   {
      glsm_state_unbind();
      glBindVertexArray(frontend_vao);
      glBindBuffer(GL_ARRAY_BUFFER, frontend_vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(1);
      glsm_state_bind();
      check_binding(GL_VERTEX_ARRAY_BINDING, 0);
      check_binding(GL_ARRAY_BUFFER_BINDING, 0);
      glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &captured);
      assert(captured == 0);
      glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
      assert(pointer == vertices);
      glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &captured);
      assert(captured == 0);
      glClear(GL_COLOR_BUFFER_BIT);
      rglDrawArrays(GL_TRIANGLES, 0, 3);
      check_pixel();
   }

   glGenVertexArrays(1, &core_vao);
   glGenBuffers(1, &core_vbo);
   glGenBuffers(1, &core_ebo);
   rglBindVertexArray(core_vao);
   rglBindBuffer(GL_ARRAY_BUFFER, core_vbo);
   rglBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   rglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
   rglEnableVertexAttribArray(0);
   rglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, core_ebo);
   rglBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
   for (frame = 0; frame < 10; ++frame)
   {
      glsm_state_unbind();
      glBindVertexArray(core_vao);
      check_binding(GL_ELEMENT_ARRAY_BUFFER_BINDING, core_ebo);
      glBindVertexArray(frontend_vao);
      glBindBuffer(GL_ARRAY_BUFFER, frontend_vbo);
      glsm_state_bind();
      check_binding(GL_VERTEX_ARRAY_BINDING, core_vao);
      check_binding(GL_ARRAY_BUFFER_BINDING, core_vbo);
      check_binding(GL_ELEMENT_ARRAY_BUFFER_BINDING, core_ebo);
      glClear(GL_COLOR_BUFFER_BIT);
      rglDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
      check_pixel();
   }
   puts("GLES3 client arrays and VAO buffers survive frontend presentation");
   eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroySurface(display, surface);
   eglDestroyContext(display, context);
   eglTerminate(display);
   return 0;
}
