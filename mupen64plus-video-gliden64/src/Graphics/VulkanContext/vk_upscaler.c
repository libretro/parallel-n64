#include <string.h>
#include <math.h>

#include <libretro.h>
#include <temporal_upscaler.h>

#include "vk_upscaler.h"
#include "../../Types.h"
#include "../../Config.h"
#include "../../VI.h"
#include "../../DisplayWindow.h"

extern u32 screen_width;
extern u32 screen_height;

/* The triangles of a frame there is room for from load on: a frame of
 * more grows it, once */
#define UP_FRAME_TRIANGLES 4096

/* Behind this w a vertex has no place on screen */
#define UP_NEAR_W 0.0625f

static const struct retro_hw_render_upscaler_interface *up_iface;
static struct retro_hw_upscaler_request up_request;
static rdp_upscaler_t *up_geometry;
static bool up_on;
static bool up_reset_pending = true;
static unsigned up_factor;
/* the frames the factor was chosen for */
static unsigned up_native_width;
static unsigned up_native_height;

/* the options it overrides while it draws, as the user has them */
static struct
{
   u32 native_res_factor;
   u32 multisampling;
   u32 fxaa;
   u32 overscan;
} up_saved;

static struct
{
   uint32_t address;
   uint32_t stride;
   int size_log2;
   bool valid;
} up_color;

static struct
{
   uint32_t address;
   uint32_t stride;
   int size_log2;
   unsigned width, height;
   float scale;
   float map[4];
   bool valid;
} up_shown;

void gliden64_upscaler_set_interface(const struct retro_hw_render_upscaler_interface *iface)
{
   up_iface         = iface;
   up_reset_pending = true;
   /* at load, not at the first frame drawn for it; with room for the
    * triangles of a frame, so that drawing one allocates nothing */
   if (iface && !up_geometry)
   {
      up_geometry = rdp_upscaler_new();
      if (up_geometry)
         rdp_upscaler_reserve(up_geometry, UP_FRAME_TRIANGLES);
   }
}

bool gliden64_upscaler_active(void)
{
   return up_on;
}

bool gliden64_upscaler_offered(void)
{
   return up_iface != NULL;
}

void gliden64_upscaler_reset(void)
{
   up_reset_pending = true;
}

/* It takes the frame as drawn: at the size chosen for it, without
 * anti-aliasing or a border of the core's own */
static bool upscaler_override(void)
{
   bool changed = false;
   if (config.frameBufferEmulation.nativeResFactor != 0)
   {
      up_saved.native_res_factor                = config.frameBufferEmulation.nativeResFactor;
      config.frameBufferEmulation.nativeResFactor = 0;
      changed                                   = true;
   }
   if (config.video.multisampling != 0)
   {
      up_saved.multisampling     = config.video.multisampling;
      config.video.multisampling = 0;
      changed                    = true;
   }
   if (config.video.fxaa != 0)
   {
      up_saved.fxaa     = config.video.fxaa;
      config.video.fxaa = 0;
      changed           = true;
   }
   if (config.frameBufferEmulation.enableOverscan != 0)
   {
      up_saved.overscan                          = config.frameBufferEmulation.enableOverscan;
      config.frameBufferEmulation.enableOverscan = 0;
      changed                                    = true;
   }
   return changed;
}

static void upscaler_restore(void)
{
   config.frameBufferEmulation.nativeResFactor = up_saved.native_res_factor;
   config.video.multisampling                  = up_saved.multisampling;
   config.video.fxaa                           = up_saved.fxaa;
   config.frameBufferEmulation.enableOverscan  = up_saved.overscan;
   memset(&up_saved, 0, sizeof(up_saved));
}

/* Halton (2, 3) over the frames the ratio of the sizes wants: at least
 * 8, at most 64 */
static unsigned upscaler_jitter_phase(void)
{
   float ratio;
   unsigned phase;
   if (!up_native_width || !up_factor)
      return 8;
   ratio = (float)up_request.output_width / (float)(up_native_width * up_factor);
   phase = (unsigned)ceil(8.0f * ratio * ratio);
   return phase < 8 ? 8 : phase > 64 ? 64 : phase;
}

bool gliden64_upscaler_poll(void)
{
   bool on         = false;
   unsigned factor = 0;
   bool resize     = false;

   /* (one picture a frame: not the two of stereoscopic 3D) */
   if (     up_iface && up_geometry
         && config.stereo.mode == stereoOff
         && VI.width != 0 && VI.height != 0
         && up_iface->get_request(up_iface->handle, &up_request))
   {
      rdp_upscaler_range_t range;
      range.want_width = up_request.render_width;
      range.min_width  = up_request.min_render_width;
      range.min_height = up_request.min_render_height;
      range.max_width  = up_request.max_render_width;
      range.max_height = up_request.max_render_height;
      factor = rdp_upscaler_factor(&range, VI.width, VI.height,
            up_on ? up_factor : 0, up_native_width, up_native_height);
      on     = factor != 0;
   }

   if (on)
   {
      if (     !up_on || factor != up_factor
            || VI.width != up_native_width || VI.height != up_native_height)
         resize = true;
      up_factor        = factor;
      up_native_width  = VI.width;
      up_native_height = VI.height;
      if (upscaler_override())
         resize = true;
      if (resize)
      {
         gli_window_set_size_now(up_native_width * up_factor, up_native_height * up_factor);
         up_reset_pending = true;
      }
   }
   else if (up_on)
   {
      upscaler_restore();
      gli_window_set_size_now(screen_width, screen_height);
      up_shown.valid = false;
   }
   up_on = on;

   if (up_geometry)
   {
      /* (it says so itself with the next frame it resolves) */
      rdp_upscaler_configure(up_geometry, on, upscaler_jitter_phase(), on && up_reset_pending);
      if (on && up_reset_pending)
      {
         up_color.valid   = false;
         up_reset_pending = false;
      }
   }
   return on;
}

void gliden64_upscaler_close(void)
{
   if (up_on)
      upscaler_restore();
   if (up_geometry)
      rdp_upscaler_free(up_geometry);
   up_geometry      = NULL;
   up_iface         = NULL;
   up_on            = false;
   up_factor        = 0;
   up_native_width  = 0;
   up_native_height = 0;
   up_reset_pending = true;
   up_color.valid   = false;
   up_shown.valid   = false;
}

void gliden64_upscaler_color_image(uint32_t address, uint32_t stride, int size_log2)
{
   if (!up_on)
      return;
   if (     up_color.valid && up_color.address == address
         && up_color.stride == stride && up_color.size_log2 == size_log2)
      return;
   up_color.address   = address;
   up_color.stride    = stride;
   up_color.size_log2 = size_log2;
   up_color.valid     = true;
   rdp_upscaler_set_color_image(up_geometry, address, stride, size_log2);
}

/* A vertex, where the shader puts it: x, y in native pixels of the color
 * image, z 0 near to 1, and its w */
typedef struct up_vertex
{
   float x, y, z, w;
} up_vertex;

static void upscaler_vertex(const vkr_tri_vertex *v, up_vertex *out)
{
   /* (as the vertex shader: screen coordinates, when given those) */
   if (v->modify & 0xff)
   {
      out->x = v->x * v->w;
      out->y = v->y * v->w;
   }
   else
   {
      out->x = v->x;
      out->y = v->y;
   }
   out->z = (v->modify & 0xff00) ? v->z * v->w : v->z;
   out->w = v->w;
}

/* Clip space to native pixels; @v->w over UP_NEAR_W */
static void upscaler_project(const up_vertex *v, bool screen, const gliden64_upscaler_view *view,
      float *xyz)
{
   float nx, ny, z;
   if (screen)
   {
      nx = v->x / v->w * view->screen_scale[0] - 1.0f;
      ny = v->y / v->w * view->screen_scale[1] + 1.0f;
   }
   else
   {
      nx = v->x / v->w;
      ny = v->y / v->w;
   }
   /* (GL's -1 to 1) */
   z = (v->z / v->w) * 0.5f + 0.5f;
   xyz[0] = (view->viewport[0] + (nx + 1.0f) * 0.5f * view->viewport[2]) / view->scale;
   xyz[1] = (view->viewport[1] + (1.0f - ny) * 0.5f * view->viewport[3]) / view->scale;
   xyz[2] = z < 0.0f ? 0.0f : z > 1.0f ? 1.0f : z;
}

/* The weights of the second and third vertex over w, and 1 over w, as
 * planes over the native pixels: the triangle whole, in homogeneous
 * coordinates, where a vertex behind the eye is as good as any. A pixel
 * (x, y) with weights b has (x w, y w, w) = b N, the rows of N the
 * vertices': b / w = (x, y, 1) inverse(N). */
static bool upscaler_planes(const up_vertex *v, bool screen, const gliden64_upscaler_view *view,
      double *planes)
{
   double n[3][3], inv[3][3], det, m;
   unsigned i, k;

   for (i = 0; i < 3; i++)
   {
      double x = v[i].x, y = v[i].y, w = v[i].w;
      if (screen)
      {
         x = x * view->screen_scale[0] - w;
         y = y * view->screen_scale[1] + w;
      }
      /* native x = (viewport x + (x / w + 1) / 2 * width) / scale, times w */
      n[i][0] = (view->viewport[0] * w + (x + w) * 0.5 * view->viewport[2]) / view->scale;
      n[i][1] = (view->viewport[1] * w + (w - y) * 0.5 * view->viewport[3]) / view->scale;
      n[i][2] = w;
   }
   /* (of order one, whatever the game's units) */
   m = 0.0;
   for (i = 0; i < 3; i++)
      if (fabs(n[i][2]) > m)
         m = fabs(n[i][2]);
   if (m == 0.0)
      return false;
   for (i = 0; i < 3; i++)
      for (k = 0; k < 3; k++)
         n[i][k] /= m;

   inv[0][0] = n[1][1] * n[2][2] - n[1][2] * n[2][1];
   inv[0][1] = n[0][2] * n[2][1] - n[0][1] * n[2][2];
   inv[0][2] = n[0][1] * n[1][2] - n[0][2] * n[1][1];
   inv[1][0] = n[1][2] * n[2][0] - n[1][0] * n[2][2];
   inv[1][1] = n[0][0] * n[2][2] - n[0][2] * n[2][0];
   inv[1][2] = n[0][2] * n[1][0] - n[0][0] * n[1][2];
   inv[2][0] = n[1][0] * n[2][1] - n[1][1] * n[2][0];
   inv[2][1] = n[0][1] * n[2][0] - n[0][0] * n[2][1];
   inv[2][2] = n[0][0] * n[1][1] - n[0][1] * n[1][0];
   det = n[0][0] * inv[0][0] + n[0][1] * inv[1][0] + n[0][2] * inv[2][0];
   if (fabs(det) < 1e-12)
      return false;

   /* c, cx, cy of each: column i of the inverse is vertex i's weight */
   for (i = 0; i < 2; i++)
   {
      planes[3 * i]     = inv[2][i + 1] / det;
      planes[3 * i + 1] = inv[0][i + 1] / det;
      planes[3 * i + 2] = inv[1][i + 1] / det;
   }
   planes[6] = (inv[2][0] + inv[2][1] + inv[2][2]) / det;
   planes[7] = (inv[0][0] + inv[0][1] + inv[0][2]) / det;
   planes[8] = (inv[1][0] + inv[1][1] + inv[1][2]) / det;
   return true;
}

static void upscaler_keep(const float *xyz, const double *planes, const gliden64_upscaler_view *view,
      uint64_t signature, unsigned flags)
{
   if (view->cull)
   {
      /* as the rasterizer: counter-clockwise is the front, y down */
      float area = (xyz[3] - xyz[0]) * (xyz[7] - xyz[1]) - (xyz[6] - xyz[0]) * (xyz[4] - xyz[1]);
      bool front = area < 0.0f;
      if (view->cull == 3 || (view->cull == 1) == front)
         return;
   }
   rdp_upscaler_triangle_vertices(up_geometry, xyz, planes, signature, flags);
}

static void upscaler_triangle(const vkr_tri_vertex *a, const vkr_tri_vertex *b, const vkr_tri_vertex *c,
      const gliden64_upscaler_view *view, uint64_t signature, unsigned flags)
{
   up_vertex in[3], out[4];
   float xyz[12];
   double planes[9];
   unsigned i, n = 0;
   bool screen = (a->modify & 0xff) != 0;
   bool have_planes;

   upscaler_vertex(a, &in[0]);
   upscaler_vertex(b, &in[1]);
   upscaler_vertex(c, &in[2]);
   have_planes = upscaler_planes(in, screen, view, planes);

   /* cut at the near plane, where the rasterizer cuts it: a vertex behind
    * the eye is nowhere on screen */
   for (i = 0; i < 3; i++)
   {
      const up_vertex *p = &in[i];
      const up_vertex *q = &in[(i + 1) % 3];
      bool p_in = p->w >= UP_NEAR_W;
      bool q_in = q->w >= UP_NEAR_W;
      if (p_in)
         out[n++] = *p;
      if (p_in != q_in)
      {
         float t   = (UP_NEAR_W - p->w) / (q->w - p->w);
         out[n].x  = p->x + (q->x - p->x) * t;
         out[n].y  = p->y + (q->y - p->y) * t;
         out[n].z  = p->z + (q->z - p->z) * t;
         out[n].w  = UP_NEAR_W;
         n++;
      }
   }
   if (n < 3)
      return;
   for (i = 0; i < n; i++)
      upscaler_project(&out[i], screen, view, &xyz[3 * i]);
   upscaler_keep(xyz, have_planes ? planes : NULL, view, signature, flags);
   if (n == 4)
   {
      float second[9];
      memcpy(&second[0], &xyz[0], 3 * sizeof(float));
      memcpy(&second[3], &xyz[6], 6 * sizeof(float));
      upscaler_keep(second, have_planes ? planes : NULL, view, signature, flags);
   }
}

void gliden64_upscaler_triangles(const vkr_tri_vertex *vertices, unsigned num_vertices,
      const uint16_t *indices, unsigned num_indices, bool strip,
      const gliden64_upscaler_view *view, uint64_t signature, unsigned flags,
      float *jitter)
{
   unsigned i;
   unsigned count = indices ? num_indices : num_vertices;

   jitter[0] = 0.0f;
   jitter[1] = 0.0f;
   if (!up_on || !up_color.valid || view->scale <= 0.0f)
      return;
   if (!rdp_upscaler_draw_jitter(up_geometry, &jitter[0], &jitter[1]))
      return;
   if (strip)
   {
      for (i = 0; i + 2 < count; i++)
      {
         /* every other one is wound the other way */
         unsigned a = i, b = (i & 1) ? i + 2 : i + 1, c = (i & 1) ? i + 1 : i + 2;
         if (indices)
         {
            a = indices[a];
            b = indices[b];
            c = indices[c];
         }
         if (a < num_vertices && b < num_vertices && c < num_vertices)
            upscaler_triangle(&vertices[a], &vertices[b], &vertices[c], view, signature, flags);
      }
      return;
   }
   for (i = 0; i + 2 < count; i += 3)
   {
      unsigned a = i, b = i + 1, c = i + 2;
      if (indices)
      {
         a = indices[a];
         b = indices[b];
         c = indices[c];
      }
      if (a < num_vertices && b < num_vertices && c < num_vertices)
         upscaler_triangle(&vertices[a], &vertices[b], &vertices[c], view, signature, flags);
   }
}

void gliden64_upscaler_rect(const vkr_rect_vertex *vertices, unsigned num_vertices,
      const gliden64_upscaler_view *view)
{
   unsigned i;
   float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;

   if (!up_on || !up_color.valid || view->scale <= 0.0f || !num_vertices)
      return;
   for (i = 0; i < num_vertices; i++)
   {
      float w = vertices[i].w != 0.0f ? vertices[i].w : 1.0f;
      /* (the rectangle's shader does not turn y over) */
      float x = (view->viewport[0] + (vertices[i].x / w + 1.0f) * 0.5f * view->viewport[2]) / view->scale;
      float y = (view->viewport[1] + (vertices[i].y / w + 1.0f) * 0.5f * view->viewport[3]) / view->scale;
      if (i == 0 || x < x0) x0 = x;
      if (i == 0 || x > x1) x1 = x;
      if (i == 0 || y < y0) y0 = y;
      if (i == 0 || y > y1) y1 = y;
   }
   rdp_upscaler_rectangle_corners(up_geometry, x0, y0, x1, y1);
}

void gliden64_upscaler_shown(uint32_t address, uint32_t stride, int size_log2,
      unsigned width, unsigned height, float scale, const float *map)
{
   if (!up_on)
      return;
   up_shown.address   = address;
   up_shown.stride    = stride;
   up_shown.size_log2 = size_log2;
   up_shown.width     = width;
   up_shown.height    = height;
   up_shown.scale     = scale;
   memcpy(up_shown.map, map, sizeof(up_shown.map));
   up_shown.valid     = true;
}

bool gliden64_upscaler_frame(vkr_upscaler_frame *frame)
{
   rdp_upscaler_framebuffer_t fb;
   rdp_upscaler_geometry_t geometry;
   unsigned scale;

   memset(frame, 0, sizeof(*frame));
   if (!up_on || !up_shown.valid || up_shown.size_log2 < 1)
      return false;
   up_shown.valid = false;
   scale          = (unsigned)(up_shown.scale + 0.5f);
   if (!scale)
      return false;

   fb.origin    = up_shown.address;
   fb.stride    = up_shown.stride;
   fb.size_log2 = (unsigned)up_shown.size_log2;
   fb.width     = up_shown.width;
   fb.height    = up_shown.height;
   rdp_upscaler_resolve(up_geometry, &fb, scale, &geometry);
   /* (resolve ends the frame drawn: the next draw asks for its record) */

   frame->iface        = up_iface;
   frame->vertices     = geometry.vertices;
   frame->draws        = geometry.draws;
   frame->num_vertices = geometry.num_vertices;
   frame->num_draws    = geometry.num_draws;
   /* resolve's pixels are @scale a native one where the framebuffer's
    * are up_shown.scale: the same, the factor being whole */
   frame->scale[0]     = up_shown.map[0];
   frame->scale[1]     = up_shown.map[1];
   frame->offset[0]    = up_shown.map[2];
   frame->offset[1]    = up_shown.map[3];
   frame->jitter[0]    = geometry.jitter_x * up_shown.map[0];
   frame->jitter[1]    = geometry.jitter_y * up_shown.map[1];
   /* (the planes are from the middle of the framebuffer, jittered) */
   frame->origin[0]    = ((float)(fb.width * scale) * 0.5f + geometry.jitter_x) * up_shown.map[0] + up_shown.map[2];
   frame->origin[1]    = ((float)(fb.height * scale) * 0.5f + geometry.jitter_y) * up_shown.map[1] + up_shown.map[3];
   frame->farthest     = geometry.farthest;
   frame->reset        = geometry.reset;
   return true;
}
