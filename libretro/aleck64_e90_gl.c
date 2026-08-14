/* Seta E90 sprite overlay for the hardware renderers.
 *
 * The software renderer composites the E90 sprites straight into its finished
 * pixel buffer; the GL renderers leave the frame in a texture the core never
 * touches, so the sprites have to be drawn on the GPU instead.
 *
 * Rather than emit geometry per block, the shared decoder rasterises into a
 * small RGBA image in the game's own framebuffer resolution and that image is
 * blended over the frame as one nearest-filtered quad.  The blocks are flat
 * 8x8 cells of a single colour, so point sampling reproduces them exactly at
 * any output scale, both renderers keep one sprite decoder between them, and
 * the GL side costs one texture upload and one draw call per frame.
 */

#include <stdlib.h>
#include <string.h>

/* Raw GL, deliberately NOT glsm/glsmsym.h.
 *
 * That header rewrites every gl* call into glsm's tracked equivalent
 * (glBindTexture -> rglBindTexture and so on).  Going through it here would
 * record this overlay's scratch state as the core renderer's own: glsm
 * re-applies its tracked state to the plugin on the next bind, so a few
 * temporary binds made at present time came back as GLideN64's state on the
 * following frame and the picture went black.  Talking to GL directly keeps
 * the overlay invisible to glsm's bookkeeping; the state this file touches is
 * saved and restored by hand below. */
#include <glsym/glsym.h>
#include <glsm/glsm.h>

#include "device/device.h"
#include "device/aleck64/aleck64.h"
#include "main/main.h"

/* The VI cannot scan out more than this, so the decode buffer never grows
 * past it however the game reprograms the registers. */
#define E90_MAX_W 704
#define E90_MAX_H 576

static GLuint e90_prog, e90_vao, e90_vbo, e90_tex;
/* The frontend's hardware-render FBO, latched at context reset.
 *
 * It must not be re-queried at present time: glsm_get_current_framebuffer()
 * forwards straight to the frontend's get_current_framebuffer callback, which
 * is a per-frame "give me this frame's target" request, not a getter.  Calling
 * it after the renderer has finished handed back a different, empty target and
 * the frontend then presented that -- a black screen.  RetroArch keeps one
 * hardware FBO for the whole context (see the note in retro_return), so
 * latching it once at reset is both safe and sufficient. */
static GLuint e90_hw_fbo;
static uint8_t* e90_buf;
static unsigned e90_buf_w, e90_buf_h;
static int e90_gl_failed;

#ifdef HAVE_OPENGLES
static const char* e90_vs =
    "attribute vec2 pos;\n"
    "varying mediump vec2 uv;\n"
    "void main() {\n"
    "   uv = pos * vec2(0.5, -0.5) + 0.5;\n"
    "   gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";
static const char* e90_fs =
    "varying mediump vec2 uv;\n"
    "uniform sampler2D img;\n"
    "void main() {\n"
    "   gl_FragColor = texture2D(img, uv).bgra;\n"
    "}\n";
#else
static const char* e90_vs =
    "#version 330 core\n"
    "in vec2 pos;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "   uv = pos * vec2(0.5, -0.5) + 0.5;\n"
    "   gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";
static const char* e90_fs =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D img;\n"
    "void main() {\n"
    "   frag = texture(img, uv).bgra;\n"
    "}\n";
#endif

static GLuint e90_compile(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Called with the caller's save/restore block already open, so this leaves
 * its bindings behind on purpose. */
static int e90_gl_init(void)
{
    static const GLfloat quad[8] = { -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,  1.f,  1.f };
    GLuint vs, fs;
    GLint ok = 0;

    vs = e90_compile(GL_VERTEX_SHADER, e90_vs);
    fs = e90_compile(GL_FRAGMENT_SHADER, e90_fs);
    if (!vs || !fs)
        goto fail;

    e90_prog = glCreateProgram();
    glAttachShader(e90_prog, vs);
    glAttachShader(e90_prog, fs);
    glBindAttribLocation(e90_prog, 0, "pos");
    glLinkProgram(e90_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(e90_prog, GL_LINK_STATUS, &ok);
    if (!ok)
        goto fail;

    glGenBuffers(1, &e90_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, e90_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

#ifndef HAVE_OPENGLES
    /* a core profile refuses to draw without one */
    glGenVertexArrays(1, &e90_vao);
    glBindVertexArray(e90_vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
#endif

    glGenTextures(1, &e90_tex);
    glBindTexture(GL_TEXTURE_2D, e90_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return 1;

fail:
    e90_gl_failed = 1;
    return 0;
}

/* The active picture the E90 shares with the VI, in the game's own
 * framebuffer pixels: the sprite coordinates are in that space, and the GL
 * renderers scale exactly that area to the output.  Mirrors the geometry
 * angrylion derives from the same registers. */
static int e90_vi_geometry(unsigned* out_w, unsigned* out_h)
{
    const uint32_t* vi = g_dev.vi.regs;
    int32_t h_start = (vi[VI_H_START_REG] >> 16) & 0x3ff;
    int32_t h_end   =  vi[VI_H_START_REG]        & 0x3ff;
    int32_t v_start = (vi[VI_V_START_REG] >> 16) & 0x3ff;
    int32_t v_end   =  vi[VI_V_START_REG]        & 0x3ff;
    int32_t x_add   =  vi[VI_X_SCALE_REG]        & 0xfff;
    int32_t hres    = h_end - h_start;
    int32_t vres    = (v_end - v_start) >> 1; /* measured in half-lines */
    int32_t w       = x_add * hres / 1024;

    if (hres <= 0 || vres <= 0 || w <= 0)
        return 0;
    if (w > E90_MAX_W) w = E90_MAX_W;
    if (vres > E90_MAX_H) vres = E90_MAX_H;

    *out_w = (unsigned)w;
    *out_h = (unsigned)vres;
    return 1;
}

/* Called once the GL context exists, before any frame is presented. */
void aleck64_e90_gl_context_reset(void)
{
    e90_prog = 0;
    e90_vao = 0;
    e90_vbo = 0;
    e90_tex = 0;
    e90_buf_w = 0;
    e90_buf_h = 0;
    e90_gl_failed = 0;
    e90_hw_fbo = glsm_get_current_framebuffer();
}

/* out_width/out_height are the frame the core is about to hand the frontend:
 * the overlay covers exactly that, since the renderers scale the VI's active
 * picture to it. */
void aleck64_e90_gl_draw(unsigned out_width, unsigned out_height)
{
    GLint prev_prog = 0, prev_tex = 0, prev_vbo = 0, prev_vao = 0, prev_unit = 0;
    GLint prev_fbo = 0, prev_vp[4] = {0, 0, 0, 0};
    GLint prev_align = 4, prev_rowlen = 0;
    GLint prev_blend = 0, prev_depth = 0, prev_cull = 0, prev_scissor = 0;
    unsigned w, h, y;

    if (!g_aleck64_enabled || !g_aleck64_e90 || e90_gl_failed)
        return;
    if (!e90_vi_geometry(&w, &h))
        return;
    if (e90_prog == 0 && e90_buf == NULL) {
        /* the decode buffer is needed before any GL work; the GL objects are
         * created below, inside the saved-state window */
        e90_buf = (uint8_t*)calloc(E90_MAX_W * E90_MAX_H, 4);
        if (e90_buf == NULL) {
            e90_gl_failed = 1;
            return;
        }
    }

    /* only the rows the sprites can reach need clearing */
    if (w != e90_buf_w || h != e90_buf_h) {
        memset(e90_buf, 0, (size_t)E90_MAX_W * E90_MAX_H * 4);
        e90_buf_w = w;
        e90_buf_h = h;
    } else {
        for (y = 0; y < h; y++)
            memset(e90_buf + (size_t)y * E90_MAX_W * 4, 0, (size_t)w * 4);
    }

    /* 1:1 into the chip's own space; the GPU does the scaling */
    if (aleck64_e90_overlay(e90_buf, E90_MAX_W, w, h, w) == 0)
        return;

    /* The renderer may well have left its own target bound, so aim at the
     * frame the frontend is about to read rather than at whatever is current. */
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prev_rowlen);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_vbo);
#ifndef HAVE_OPENGLES
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
#endif
    /* glIsEnabled is not among the GL symbols this build links; the
     * equivalent glGetIntegerv query is. */
    glGetIntegerv(GL_BLEND, &prev_blend);
    glGetIntegerv(GL_DEPTH_TEST, &prev_depth);
    glGetIntegerv(GL_CULL_FACE, &prev_cull);
    glGetIntegerv(GL_SCISSOR_TEST, &prev_scissor);

    /* Creating the shader, buffers and texture leaves them bound, and glsm is
     * not tracking these calls, so it happens inside the saved window too --
     * doing it outside is what left GLideN64 drawing with this file's texture
     * and blacked the picture out. */
    if (!e90_prog && !e90_gl_init())
        goto restore;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, e90_hw_fbo);
    glViewport(0, 0, (GLsizei)out_width, (GLsizei)out_height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, e90_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef HAVE_OPENGLES
    glPixelStorei(GL_UNPACK_ROW_LENGTH, E90_MAX_W);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, e90_buf);
#else
    /* GLES2 has no UNPACK_ROW_LENGTH: hand it a tightly packed copy */
    {
        static uint8_t* packed;
        if (packed == NULL)
            packed = (uint8_t*)malloc((size_t)E90_MAX_W * E90_MAX_H * 4);
        if (packed != NULL) {
            for (y = 0; y < h; y++)
                memcpy(packed + (size_t)y * w * 4,
                       e90_buf + (size_t)y * E90_MAX_W * 4, (size_t)w * 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, packed);
        }
    }
#endif

    glUseProgram(e90_prog);
    glUniform1i(glGetUniformLocation(e90_prog, "img"), 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifndef HAVE_OPENGLES
    glBindVertexArray(e90_vao);
#else
    glBindBuffer(GL_ARRAY_BUFFER, e90_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
#endif
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

restore:
#ifndef HAVE_OPENGLES
    glBindVertexArray((GLuint)prev_vao);
#else
    glDisableVertexAttribArray(0);
#endif
    if (!prev_blend)   glDisable(GL_BLEND);
    if (prev_depth)    glEnable(GL_DEPTH_TEST);
    if (prev_cull)     glEnable(GL_CULL_FACE);
    if (prev_scissor)  glEnable(GL_SCISSOR_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_vbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    glActiveTexture((GLenum)prev_unit);
    glUseProgram((GLuint)prev_prog);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prev_rowlen);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_fbo);
}

/* The GL objects die with the context; drop the handles so the next context
 * reset rebuilds them instead of drawing with stale names. */
void aleck64_e90_gl_destroy(void)
{
    e90_prog = 0;
    e90_vao = 0;
    e90_vbo = 0;
    e90_tex = 0;
    e90_buf_w = 0;
    e90_buf_h = 0;
    e90_gl_failed = 0;
    free(e90_buf);
    e90_buf = NULL;
}
