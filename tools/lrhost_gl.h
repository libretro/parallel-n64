/* OpenGL HW render support for lrhost, built when LRHOST_GL is defined
 * (lrhost_gl). Headless: an EGL context with no surface (Mesa's
 * EGL_PLATFORM=surfaceless and llvmpipe need no GPU), of the profile and
 * version the core asks for, and a framebuffer object the core draws into.
 * Every presented frame is read back, flipped to top-down rows, so a frame
 * can be signed and dumped exactly as for a software core. */
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define GLH_RGBA 0x1908
#define GLH_UNSIGNED_BYTE 0x1401
#define GLH_FRAMEBUFFER 0x8D40
#define GLH_READ_FRAMEBUFFER 0x8CA8
#define GLH_RENDERBUFFER 0x8D41
#define GLH_COLOR_ATTACHMENT0 0x8CE0
#define GLH_DEPTH_ATTACHMENT 0x8D00
#define GLH_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GLH_RGBA8 0x8058
#define GLH_DEPTH24_STENCIL8 0x88F0
#define GLH_DEPTH_COMPONENT24 0x81A6
#define GLH_PACK_ALIGNMENT 0x0D05
#define GLH_PIXEL_PACK_BUFFER 0x88EB

typedef unsigned int GLHuint;
typedef int GLHint;
typedef unsigned int GLHenum;
typedef int GLHsizei;

static struct retro_hw_render_callback gl_hw;
static EGLDisplay gl_dpy = EGL_NO_DISPLAY;
static EGLContext gl_egl_ctx = EGL_NO_CONTEXT;
static GLHuint gl_fbo, gl_rb_color, gl_rb_depth;
static unsigned gl_fb_w, gl_fb_h;
static uint32_t *gl_frame; static size_t gl_frame_cap;

static void (*p_glGenFramebuffers)(GLHsizei, GLHuint *);
static void (*p_glBindFramebuffer)(GLHenum, GLHuint);
static void (*p_glGenRenderbuffers)(GLHsizei, GLHuint *);
static void (*p_glBindRenderbuffer)(GLHenum, GLHuint);
static void (*p_glRenderbufferStorage)(GLHenum, GLHenum, GLHsizei, GLHsizei);
static void (*p_glFramebufferRenderbuffer)(GLHenum, GLHenum, GLHenum, GLHuint);
static void (*p_glReadPixels)(GLHint, GLHint, GLHsizei, GLHsizei, GLHenum, GLHenum, void *);
static void (*p_glPixelStorei)(GLHenum, GLHint);
static void (*p_glBindBuffer)(GLHenum, GLHuint);
static void (*p_glFinish)(void);

static uintptr_t gl_get_current_framebuffer(void) { return gl_fbo; }

static retro_proc_address_t gl_get_proc_address(const char *sym)
{
    return (retro_proc_address_t)eglGetProcAddress(sym);
}

static int gl_init(unsigned max_w, unsigned max_h)
{
    EGLint major, minor, n;
    EGLConfig cfg;
    const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLint ctx_attr[16];
    int i = 0;
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

#ifdef EGL_PLATFORM_SURFACELESS_MESA
    if (get_platform_display)
        gl_dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
#endif
    if (gl_dpy == EGL_NO_DISPLAY)
        gl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gl_dpy == EGL_NO_DISPLAY || !eglInitialize(gl_dpy, &major, &minor))
    { fprintf(stderr, "lrhost_gl: no EGL display\n"); return 0; }
    if (!eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(gl_dpy, cfg_attr, &cfg, 1, &n) || n < 1)
    { fprintf(stderr, "lrhost_gl: no OpenGL config\n"); return 0; }

    if (gl_hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE)
    {
        ctx_attr[i++] = EGL_CONTEXT_MAJOR_VERSION; ctx_attr[i++] = (EGLint)gl_hw.version_major;
        ctx_attr[i++] = EGL_CONTEXT_MINOR_VERSION; ctx_attr[i++] = (EGLint)gl_hw.version_minor;
        ctx_attr[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK; ctx_attr[i++] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
    }
    else
    {
        ctx_attr[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK; ctx_attr[i++] = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;
    }
    ctx_attr[i++] = EGL_NONE;
    gl_egl_ctx = eglCreateContext(gl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (gl_egl_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(gl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, gl_egl_ctx))
    { fprintf(stderr, "lrhost_gl: cannot make a %s %u.%u context current\n",
              gl_hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE ? "core" : "compatibility",
              gl_hw.version_major, gl_hw.version_minor); return 0; }

#define GLP(n) *(void **)&p_##n = (void *)eglGetProcAddress(#n); if (!p_##n) { fprintf(stderr, "lrhost_gl: no %s\n", #n); return 0; }
    GLP(glGenFramebuffers); GLP(glBindFramebuffer); GLP(glGenRenderbuffers); GLP(glBindRenderbuffer);
    GLP(glRenderbufferStorage); GLP(glFramebufferRenderbuffer); GLP(glReadPixels); GLP(glPixelStorei);
    GLP(glBindBuffer); GLP(glFinish);
#undef GLP

    gl_fb_w = max_w; gl_fb_h = max_h;
    p_glGenFramebuffers(1, &gl_fbo);
    p_glBindFramebuffer(GLH_FRAMEBUFFER, gl_fbo);
    p_glGenRenderbuffers(1, &gl_rb_color);
    p_glBindRenderbuffer(GLH_RENDERBUFFER, gl_rb_color);
    p_glRenderbufferStorage(GLH_RENDERBUFFER, GLH_RGBA8, (GLHsizei)max_w, (GLHsizei)max_h);
    p_glFramebufferRenderbuffer(GLH_FRAMEBUFFER, GLH_COLOR_ATTACHMENT0, GLH_RENDERBUFFER, gl_rb_color);
    if (gl_hw.depth)
    {
        p_glGenRenderbuffers(1, &gl_rb_depth);
        p_glBindRenderbuffer(GLH_RENDERBUFFER, gl_rb_depth);
        p_glRenderbufferStorage(GLH_RENDERBUFFER, gl_hw.stencil ? GLH_DEPTH24_STENCIL8 : GLH_DEPTH_COMPONENT24,
                                (GLHsizei)max_w, (GLHsizei)max_h);
        p_glFramebufferRenderbuffer(GLH_FRAMEBUFFER, gl_hw.stencil ? GLH_DEPTH_STENCIL_ATTACHMENT : GLH_DEPTH_ATTACHMENT,
                                    GLH_RENDERBUFFER, gl_rb_depth);
    }
    p_glBindRenderbuffer(GLH_RENDERBUFFER, 0);
    return 1;
}

/* the frame the core drew, top-down XRGB8888 */
static const uint32_t *gl_read_frame(unsigned w, unsigned h)
{
    size_t need = (size_t)w * h, y;
    uint32_t *rows;
    if (!w || !h || w > gl_fb_w || h > gl_fb_h) return NULL;
    if (need * 2 > gl_frame_cap)
    {
        free(gl_frame);
        gl_frame_cap = need * 2;
        gl_frame = (uint32_t *)malloc(gl_frame_cap * 4);
        if (!gl_frame) { gl_frame_cap = 0; return NULL; }
    }
    rows = gl_frame + need;
    /* the core's state is its own: only what the read needs is changed */
    p_glBindBuffer(GLH_PIXEL_PACK_BUFFER, 0);
    p_glPixelStorei(GLH_PACK_ALIGNMENT, 4);
    p_glBindFramebuffer(GLH_READ_FRAMEBUFFER, gl_fbo);
    p_glReadPixels(0, 0, (GLHsizei)w, (GLHsizei)h, GLH_RGBA, GLH_UNSIGNED_BYTE, rows);
    for (y = 0; y < h; y++)
    {
        const uint8_t *src = (const uint8_t *)(gl_hw.bottom_left_origin ? rows + (h - 1 - y) * w : rows + y * w);
        uint32_t *dst = gl_frame + y * w;
        unsigned x;
        for (x = 0; x < w; x++)
            dst[x] = ((uint32_t)src[4 * x] << 16) | ((uint32_t)src[4 * x + 1] << 8) | src[4 * x + 2];
    }
    return gl_frame;
}

static void gl_shutdown(void)
{
    if (gl_hw.context_destroy) gl_hw.context_destroy();
    if (gl_dpy != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(gl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (gl_egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(gl_dpy, gl_egl_ctx);
        eglTerminate(gl_dpy);
    }
    free(gl_frame);
}
