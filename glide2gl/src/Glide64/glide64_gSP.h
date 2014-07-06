typedef struct DRAWOBJECT_t
{
  float objX;
  float objY;
  float scaleW;
  float scaleH;
  int16_t imageW;
  int16_t imageH;

  uint16_t  imageStride;
  uint16_t  imageAdrs;
  uint8_t  imageFmt;
  uint8_t  imageSiz;
  uint8_t  imagePal;
  uint8_t  imageFlags;
} DRAWOBJECT;

struct MAT2D {
  float A, B, C, D;
  float X, Y;
  float BaseScaleX;
  float BaseScaleY;
} mat_2d = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};

// positional and texel coordinate clipping
#define CCLIP(ux,lx,ut,lt,uc,lc) \
		if (ux > lx || lx < uc || ux > lc) return; \
		if (ux < uc) { \
			float p = (uc-ux)/(lx-ux); \
			ut = p*(lt-ut)+ut; \
			ux = uc; \
		} \
		if (lx > lc) { \
			float p = (lc-ux)/(lx-ux); \
			lt = p*(lt-ut)+ut; \
			lx = lc; \
		}

#define CCLIP2(ux,lx,ut,lt,un,ln,uc,lc) \
		if (ux > lx || lx < uc || ux > lc) return; \
		if (ux < uc) { \
			float p = (uc-ux)/(lx-ux); \
			ut = p*(lt-ut)+ut; \
			un = p*(ln-un)+un; \
			ux = uc; \
		} \
		if (lx > lc) { \
			float p = (lc-ux)/(lx-ux); \
			lt = p*(lt-ut)+ut; \
			ln = p*(ln-un)+un; \
			lx = lc; \
		}

//forward decls
static void uc6_draw_polygons (VERTEX v[4]);
static void uc6_read_object_data (DRAWOBJECT *d);
static void uc6_init_tile(const DRAWOBJECT *d);
extern uint32_t dma_offset_mtx;
extern int32_t cur_mtx;
extern uint32_t dma_offset_mtx;
extern uint32_t dma_offset_vtx;
extern int32_t billboarding;

int dzdx = 0;
int deltaZ = 0;
VERTEX **org_vtx;

//software backface culling. Gonetz
// mega modifications by Dave2001

static int cull_tri(VERTEX **v) // type changed to VERTEX** [Dave2001]
{
   int i, draw, iarea;
   unsigned int mode;
   float x1, y1, x2, y2, area;

   if (v[0]->scr_off & v[1]->scr_off & v[2]->scr_off)
      return true;

   // Triangle can't be culled, if it need clipping
   draw = false;

   for (i=0; i<3; i++)
   {
      if (!v[i]->screen_translated)
      {
         v[i]->sx = rdp.view_trans[0] + v[i]->x_w * rdp.view_scale[0] + rdp.offset_x;
         v[i]->sy = rdp.view_trans[1] + v[i]->y_w * rdp.view_scale[1] + rdp.offset_y;
         v[i]->sz = rdp.view_trans[2] + v[i]->z_w * rdp.view_scale[2];
         v[i]->screen_translated = 1;
      }
      if (v[i]->w < 0.01f) //need clip_z. can't be culled now
         draw = 1;
   }

   rdp.u_cull_mode = (rdp.flags & CULLMASK);
   if (draw || rdp.u_cull_mode == 0 || rdp.u_cull_mode == CULLMASK) //no culling set
   {
      rdp.u_cull_mode >>= CULLSHIFT;
      return false;
   }

   x1 = v[0]->sx - v[1]->sx;
   y1 = v[0]->sy - v[1]->sy;
   x2 = v[2]->sx - v[1]->sx;
   y2 = v[2]->sy - v[1]->sy;
   area = y1 * x2 - x1 * y2;
   iarea = *(int*)&area;

   mode = (rdp.u_cull_mode << 19UL);
   rdp.u_cull_mode >>= CULLSHIFT;

   if ((iarea & 0x7FFFFFFF) == 0)
   {
      //LRDP (" zero area triangles\n");
      return true;
   }

   if ((rdp.flags & CULLMASK) && ((int)(iarea ^ mode)) >= 0)
   {
      //LRDP (" culled\n");
      return true;
   }

   return false;
}

/*
 * Sets the geometry pipeline modes enabled.
 *
 * Sets modes for culling, lighting, specular highlights, reflection mapping,
 * fog, etc. use gSPClearGeometryMode to disable the modes.
 *
 * Numerous specifications can be set for mode with a bit sum of the following
 * flags:
 *
 * mode - the geometry pipeline mode:
 *      - G_SHADE          - Enables calculation of vertex color for a triangle.
 *      - G_LIGHTING       - Enables lighting calculations.
 *      - G_SHADING_SMOOTH - Enables Gouraud shading.
 *                           When this is not enabled, flat shading is used for the
 *                           triangle, based on the color of one vertex (see gSP1Triangle).
 *                           G_SHADE must be enabled to calculate vertex color.
 *      - G_ZBUFFER        - Enables Z buffer calculations.
 *                           Other Z buffer-related parameters for the frame buffer must
 *                           also be set.
 *      - G_TEXTURE_GEN    - Enables automatic generation of the texture's s, t coordinates.
 *                           Spherical mapping based on the normal vector is used.
 *      - G_TEXTURE_GEN_LINEAR  - Enables automatic generation of the texture's, t coordinates.
 *      - G_CULL_FRONT     - Enables front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_CULL_BACK      - Enables back-face culling.
 *      - G_CULL_BOTH      - Enables both back-face and front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_FOG            - Enables generation of vertex alpha coordinate fog parameters.
 *      - G_CLIPPING       - Enables clipping.
 *                           This mode is enabled in the initial state. When disabled, clipping
 *                           is not performed. This mode can only be used with F3DLX and F3DLX.NoN.
 *
 * Other elements which have their own commands also exist for the RSP rendering state. These
 * involve chaning the state with something more complicated than a single bit, or using other
 * commands to optimize the RSP geometry engine.
 */
static void gSPSetGeometryMode(uint32_t mode)
{
   rdp.geom_mode |= mode;

   // TODO - should we look at all these state changes at this point?
   // This isn't done in gln64

   if (mode & G_ZBUFFER)  // Z-Buffer enable
   {
      if (!(rdp.flags & ZBUF_ENABLED))
      {
         rdp.flags |= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }

   //Added by Gonetz
   if (mode & G_FOG)      // Fog enable
   {
      if (!(rdp.flags & FOG_ENABLED))
      {
         rdp.flags |= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
   //FRDP("uc0:setgeometrymode %08lx; result: %08lx\n", mode, rdp.geom_mode);
   if (settings.ucode == 5)
      return;

   if (mode & CULL_FRONT)  // Front culling
   {
      if (!(rdp.flags & CULL_FRONT))
      {
         rdp.flags |= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   if (mode & CULL_BACK)  // Back culling
   {
      if (!(rdp.flags & CULL_BACK))
      {
         rdp.flags |= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
}

/*
 * Disables the geometry pipeline modes.
 *
 * Disables pipeline modes for culling, lighting, specular highlights reflection
 * mapping, fog, etc. To enable these various modes, use gSPSetGeometryMode.
 *
 * By disabling G_CLIPPING, processing time can be reduced because the data required
 * for clipping decision does not need to be calculated by gSPVertex. Also, clipping
 * is performd in GSP1Triangle, GSP2Triangles and gSP1Quadrangle.
 *
 * mode - the geometry pipeline mode:
 *      - G_SHADE          - Disables calculation of vertex color for a triangle.
 *      - G_LIGHTING       - Disables lighting calculations.
 *      - G_SHADING_SMOOTH - Disables Gouraud shading.
 *                           When this is not enabled, flat shading is used for the
 *                           triangle, based on the color of one vertex (see gSP1Triangle).
 *                           G_SHADE must be enabled to calculate vertex color.
 *      - G_ZBUFFER        - Disables Z buffer calculations.
 *                           Other Z buffer-related parameters for the frame buffer must
 *                           also be set.
 *      - G_TEXTURE_GEN    - Disables automatic generation of the texture's s, t coordinates.
 *                           Spherical mapping based on the normal vector is used.
 *      - G_TEXTURE_GEN_LINEAR  - Disables automatic generation of the texture's, t coordinates.
 *      - G_CULL_FRONT     - Disables front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_CULL_BACK      - Disables back-face culling.
 *      - G_CULL_BOTH      - Disables both back-face and front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_FOG            - Disables generation of vertex alpha coordinate fog parameters.
 *      - G_CLIPPING       - Disables clipping.
 *                           This mode is enabled in the initial state. When disabled, clipping
 *                           is not performed. This mode can only be used with F3DLX and F3DLX.NoN.
 */
static void gSPClearGeometryMode(uint32_t mode)
{
   rdp.geom_mode &= (~mode);

   // TODO - should we look at all these state changes at this point?
   // This isn't done in gln64
   
   if (mode & G_ZBUFFER)  // Z-Buffer enable
   {
      if (rdp.flags & ZBUF_ENABLED)
      {
         rdp.flags ^= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }

   //Added by Gonetz
   if (mode & G_FOG)      // Fog enable
   {
      if (rdp.flags & FOG_ENABLED)
      {
         rdp.flags ^= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }

   if (settings.ucode == 5)
      return;

   if (mode & CULL_FRONT)  // Front culling
   {
      if (rdp.flags & CULL_FRONT)
      {
         rdp.flags ^= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   if (mode & CULL_BACK)  // Back culling
   {
      if (rdp.flags & CULL_BACK)
      {
         rdp.flags ^= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   //FRDP("uc0:cleargeometrymode %08lx\n", mode);
}

static void gSPCombineMatrices(void)
{
   MulMatrices(rdp.model, rdp.proj, rdp.combined);
   rdp.update ^= UPDATE_MULT_MAT;
}

/* clip_w - clips aint the z-axis */
static void clip_w (void)
{
   int i, j, index, n;
   float percent;
   VERTEX *tmp;
   
   n = rdp.n_global;
   // Swap vertex buffers
   tmp = (VERTEX*)rdp.vtxbuf2;
   rdp.vtxbuf2 = rdp.vtxbuf;
   rdp.vtxbuf = tmp;
   rdp.vtx_buffer ^= 1;
   index = 0;

   // Check the vertices for clipping
   for (i=0; i < n; i++)
   {
      VERTEX *first, *second;
      j = i+1;
      if (j == 3)
         j = 0;
      first = (VERTEX*)&rdp.vtxbuf2[i];
      second = (VERTEX*)&rdp.vtxbuf2[j];

      if (first->w >= 0.01f)
      {
         if (second->w >= 0.01f)    // Both are in, save the last one
         {
            rdp.vtxbuf[index] = rdp.vtxbuf2[j];
            rdp.vtxbuf[index++].not_zclipped = 1;
         }
         else      // First is in, second is out, save intersection
         {
            percent = (-first->w) / (second->w - first->w);
            rdp.vtxbuf[index].not_zclipped = 0;
            rdp.vtxbuf[index].x = first->x + (second->x - first->x) * percent;
            rdp.vtxbuf[index].y = first->y + (second->y - first->y) * percent;
            rdp.vtxbuf[index].z = first->z + (second->z - first->z) * percent;
            rdp.vtxbuf[index].w = settings.depth_bias * 0.01f;
            rdp.vtxbuf[index].u0 = first->u0 + (second->u0 - first->u0) * percent;
            rdp.vtxbuf[index].v0 = first->v0 + (second->v0 - first->v0) * percent;
            rdp.vtxbuf[index].u1 = first->u1 + (second->u1 - first->u1) * percent;
            rdp.vtxbuf[index].v1 = first->v1 + (second->v1 - first->v1) * percent;
            rdp.vtxbuf[index++].number = first->number | second->number;
         }
      }
      else
      {
         if (second->w >= 0.01f)  // First is out, second is in, save intersection & in point
         {
            percent = (-second->w) / (first->w - second->w);
            rdp.vtxbuf[index].not_zclipped = 0;
            rdp.vtxbuf[index].x = second->x + (first->x - second->x) * percent;
            rdp.vtxbuf[index].y = second->y + (first->y - second->y) * percent;
            rdp.vtxbuf[index].z = second->z + (first->z - second->z) * percent;
            rdp.vtxbuf[index].w = settings.depth_bias * 0.01f;
            rdp.vtxbuf[index].u0 = second->u0 + (first->u0 - second->u0) * percent;
            rdp.vtxbuf[index].v0 = second->v0 + (first->v0 - second->v0) * percent;
            rdp.vtxbuf[index].u1 = second->u1 + (first->u1 - second->u1) * percent;
            rdp.vtxbuf[index].v1 = second->v1 + (first->v1 - second->v1) * percent;
            rdp.vtxbuf[index++].number = first->number | second->number;

            // Save the in point
            rdp.vtxbuf[index] = rdp.vtxbuf2[j];
            rdp.vtxbuf[index++].not_zclipped = 1;
         }
      }
   }
   rdp.n_global = index;
}

static void draw_tri_depth(VERTEX **vtx)
{
   float X0 = vtx[0]->sx / rdp.scale_x;
   float Y0 = vtx[0]->sy / rdp.scale_y;
   float X1 = vtx[1]->sx / rdp.scale_x;
   float Y1 = vtx[1]->sy / rdp.scale_y;
   float X2 = vtx[2]->sx / rdp.scale_x;
   float Y2 = vtx[2]->sy / rdp.scale_y;
   float diffy_02 = Y0 - Y2;
   float diffy_12 = Y1 - Y2;
   float diffx_02 = X0 - X2;
   float diffx_12 = X1 - X2;

   float denom = (diffx_02 * diffy_12 - diffx_12 * diffy_02);
   if(denom*denom > 0.0)
   {
      float diffz_02 = vtx[0]->sz - vtx[2]->sz;
      float diffz_12 = vtx[1]->sz - vtx[2]->sz;
      float fdzdx = (diffz_02 * diffy_12 - diffz_12 * diffy_02) / denom;
      if ((rdp.rm & ZMODE_DECAL) == ZMODE_DECAL)
      {
         // Calculate deltaZ per polygon for Decal z-mode
         float fdzdy = (diffz_02 * diffx_12 - diffz_12 * diffx_02) / denom;
         float fdz = fabs(fdzdx) + fabs(fdzdy);
         if ((settings.hacks & hack_Zelda) && (rdp.rm & 0x800))
            fdz *= 4.0;  // Decal mode in Zelda sometimes needs mutiplied deltaZ to work correct, e.g. roads
         deltaZ = max(8, (int)fdz);
      }
      dzdx = (int)(fdzdx * 65536.0); }
}

static void draw_tri (VERTEX **vtx, uint16_t linew)
{
   int i;

   org_vtx = vtx;

   for (i = 0; i < 3; i++)
   {
      VERTEX *v = (VERTEX*)vtx[i];

      if (v->uv_calculated != rdp.tex_ctr)
      {
         //FRDP(" * CALCULATING VERTEX U/V: %d\n", v->number);
         v->uv_calculated = rdp.tex_ctr;

         if (!(rdp.geom_mode & G_LIGHTING))
         {
            if (!(rdp.geom_mode & UPDATE_SCISSOR))
            {
               if (rdp.geom_mode & G_SHADE)
                  glideSetVertexFlatShading(v, vtx, rdp.cmd1);
               else
                  glideSetVertexPrimShading(v, rdp.prim_color);
            }
         }

         // Fix texture coordinates
         if (!v->uv_scaled)
         {
            v->ou *= rdp.tiles[rdp.cur_tile].s_scale;
            v->ov *= rdp.tiles[rdp.cur_tile].t_scale;
            v->uv_scaled = 1;
            if (!(rdp.othermode_h & RDP_PERSP_TEX_ENABLE))
            {
               //          v->oow = v->w = 1.0f;
               v->ou *= 0.5f;
               v->ov *= 0.5f;
            }
         }
         v->u1 = v->u0 = v->ou;
         v->v1 = v->v0 = v->ov;

         if (rdp.tex >= 1 && rdp.cur_cache[0])
         {

            if (rdp.tiles[rdp.cur_tile].shift_s)
            {
               if (rdp.tiles[rdp.cur_tile].shift_s > 10)
                  v->u0 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile].shift_s));
               else
                  v->u0 /= (float)(1 << rdp.tiles[rdp.cur_tile].shift_s);
            }
            if (rdp.tiles[rdp.cur_tile].shift_t)
            {
               if (rdp.tiles[rdp.cur_tile].shift_t > 10)
                  v->v0 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile].shift_t));
               else
                  v->v0 /= (float)(1 << rdp.tiles[rdp.cur_tile].shift_t);
            }

            {
               v->u0 -= rdp.tiles[rdp.cur_tile].f_ul_s;
               v->v0 -= rdp.tiles[rdp.cur_tile].f_ul_t;
               v->u0 = rdp.cur_cache[0]->c_off + rdp.cur_cache[0]->c_scl_x * v->u0;
               v->v0 = rdp.cur_cache[0]->c_off + rdp.cur_cache[0]->c_scl_y * v->v0;
            }
            v->u0_w = v->u0 / v->w;
            v->v0_w = v->v0 / v->w;
         }

         if (rdp.tex >= 2 && rdp.cur_cache[1])
         {
            if (rdp.tiles[rdp.cur_tile+1].shift_s)
            {
               if (rdp.tiles[rdp.cur_tile+1].shift_s > 10)
                  v->u1 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile+1].shift_s));
               else
                  v->u1 /= (float)(1 << rdp.tiles[rdp.cur_tile+1].shift_s);
            }
            if (rdp.tiles[rdp.cur_tile+1].shift_t)
            {
               if (rdp.tiles[rdp.cur_tile+1].shift_t > 10)
                  v->v1 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile+1].shift_t));
               else
                  v->v1 /= (float)(1 << rdp.tiles[rdp.cur_tile+1].shift_t);
            }

            {
               v->u1 -= rdp.tiles[rdp.cur_tile+1].f_ul_s;
               v->v1 -= rdp.tiles[rdp.cur_tile+1].f_ul_t;
               v->u1 = rdp.cur_cache[1]->c_off + rdp.cur_cache[1]->c_scl_x * v->u1;
               v->v1 = rdp.cur_cache[1]->c_off + rdp.cur_cache[1]->c_scl_y * v->v1;
            }

            v->u1_w = v->u1 / v->w;
            v->v1_w = v->v1 / v->w;
         }
         //      FRDP(" * CALCULATING VERTEX U/V: %d  u0: %f, v0: %f, u1: %f, v1: %f\n", v->number, v->u0, v->v0, v->u1, v->v1);
      }
      if (v->shade_mod != cmb.shade_mod_hash)
         apply_shade_mods (v);
   } //for

   rdp.clip = 0;

   vtx[0]->not_zclipped = vtx[1]->not_zclipped = vtx[2]->not_zclipped = 1;

   // Set vertex buffers
   rdp.vtxbuf = rdp.vtx1;  // copy from v to rdp.vtx1
   rdp.vtxbuf2 = rdp.vtx2;
   rdp.vtx_buffer = 0;
   rdp.n_global = 3;

   rdp.vtxbuf[0] = *vtx[0];
   rdp.vtxbuf[0].number = 1;
   rdp.vtxbuf[1] = *vtx[1];
   rdp.vtxbuf[1].number = 2;
   rdp.vtxbuf[2] = *vtx[2];
   rdp.vtxbuf[2].number = 4;

   if ((vtx[0]->scr_off & 16) ||
         (vtx[1]->scr_off & 16) ||
         (vtx[2]->scr_off & 16))
      clip_w();

   do_triangle_stuff (linew, false);
}

static void cull_trianglefaces(VERTEX **v, unsigned iterations, bool do_update, bool do_cull, int32_t wd)
{
   int32_t i, vcount;
   vcount = 0;

   if (do_update)
      update();

   for (i = 0; i < iterations; i++)
   {
      bool do_draw_tri = true;
      if (do_cull)
      {
         if (cull_tri(v + vcount))
            do_draw_tri = false;
      }

      if (do_draw_tri)
      {
         deltaZ = dzdx = 0;
         if (wd == 0 && (fb_depth_render_enabled || (rdp.rm & ZMODE_DECAL) == ZMODE_DECAL))
            draw_tri_depth(v + vcount);
         draw_tri (v + vcount, wd);
      }
      vcount += 3;
   }
}

//angrylion's macro, helps to cut overflowed values.
#define SIGN16(x) (((x) & 0x8000) ? ((x) | ~0xffff) : ((x) & 0xffff))

static void pre_update(void)
{
   // This is special, not handled in update(), but here
   // Matrix Pre-multiplication idea by Gonetz (Gonetz@ngs.ru)
   if (rdp.update & UPDATE_MULT_MAT)
      gSPCombineMatrices();

   // This is special, not handled in update()
   if (rdp.update & UPDATE_LIGHTS)
   {
      uint32_t l;
      rdp.update ^= UPDATE_LIGHTS;

      // Calculate light vectors
      for (l = 0; l < rdp.num_lights; l++)
      {
         InverseTransformVector(&rdp.light[l].dir[0], rdp.light_vector[l], rdp.model);
         NormalizeVector (rdp.light_vector[l]);
      }
   }
}

#ifdef HAVE_NEON
#include <arm_neon.h>
#endif

/*
 * Loads into the RSP vertex buffer the vertices that will be used by the 
 * gSP1Triangle commands to generate polygons.
 *
 * v  - Segment address of the vertex list  pointer to a list of vertices.
 * n  - Number of vertices (1 - 32).
 * v0 - Starting index in vertex buffer where vertices are to be loaded into.
 */
static void gSPVertex(uint32_t addr, uint32_t n, uint32_t v0)
{
   int i;
   float x, y, z;
#ifdef HAVE_NEON
   float32x4_t comb0, comb1, comb2, comb3;
   float32x4_t v_xyzw;
#endif
   void   *membase_ptr  = (void*)gfx_info.RDRAM + addr;
   uint32_t iter = 16;

#ifdef HAVE_NEON
   comb0 = vld1q_f32(rdp.combined[0]);
   comb1 = vld1q_f32(rdp.combined[1]);
   comb2 = vld1q_f32(rdp.combined[2]);
   comb3 = vld1q_f32(rdp.combined[3]);
#endif

   for (i=0; i < (n * iter); i+= iter)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i / iter)];
      int16_t *rdram    = (int16_t*)membase_ptr;
      int8_t  *rdram_s8 = (int8_t* )membase_ptr;
      uint8_t *rdram_u8 = (uint8_t*)membase_ptr;
      uint8_t *color = (uint8_t*)(rdram_u8 + 12);
      y                 = (float)rdram[0];
      x                 = (float)rdram[1];
      vert->flags       = (uint16_t)rdram[2];
      z                 = (float)rdram[3];
      vert->ov          = (float)rdram[4];
      vert->ou          = (float)rdram[5];
      vert->uv_scaled   = 0;
      vert->a           = color[0];

#ifdef HAVE_NEON
      v_xyzw  = vmulq_n_f32(comb0,x)+vmulq_n_f32(comb1,y)+vmulq_n_f32(comb2,z)+comb3;
      vert->x = vgetq_lane_f32(v_xyzw,0);
      vert->y = vgetq_lane_f32(v_xyzw,1);
      vert->z = vgetq_lane_f32(v_xyzw,2);
      vert->w = vgetq_lane_f32(v_xyzw,3);
#else
      vert->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      vert->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      vert->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      vert->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];
#endif

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
#ifdef HAVE_NEON
      v_xyzw = vmulq_n_f32(v_xyzw,vert->oow);
      vert->x_w=vgetq_lane_f32(v_xyzw,0);
      vert->y_w=vgetq_lane_f32(v_xyzw,1);
      vert->z_w=vgetq_lane_f32(v_xyzw,2);
#else
      vert->x_w = vert->x * vert->oow;
      vert->y_w = vert->y * vert->oow;
      vert->z_w = vert->z * vert->oow;
#endif
      CalculateFog (vert);

      vert->scr_off = 0;
      if (vert->x < -vert->w)
         vert->scr_off |= 1;
      if (vert->x > vert->w)
         vert->scr_off |= 2;
      if (vert->y < -vert->w)
         vert->scr_off |= 4;
      if (vert->y > vert->w)
         vert->scr_off |= 8;
      if (vert->w < 0.1f)
         vert->scr_off |= 16;
#if 0
      if (vert->z_w > 1.0f)
         vert->scr_off |= 32;
#endif

      if (rdp.geom_mode & G_LIGHTING)
      {
         vert->vec[0] = (int8_t)color[3];
         vert->vec[1] = (int8_t)color[2];
         vert->vec[2] = (int8_t)color[1];

         if (rdp.geom_mode & G_TEXTURE_GEN)
         {
            if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
               calc_linear (vert);
            else
               calc_sphere (vert);
         }

         if (settings.ucode == 2 && rdp.geom_mode & 0x00400000)
         {
            float tmpvec[3] = {x, y, z};
            calc_point_light (vert, tmpvec);
         }
         else
         {
            NormalizeVector (vert->vec);
            calc_light (vert);
         }
      }
      else
      {
         vert->r = color[3];
         vert->g = color[2];
         vert->b = color[1];
      }
      membase_ptr += iter;
   }
}

