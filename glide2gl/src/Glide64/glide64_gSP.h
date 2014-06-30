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
		if (ux > lx || lx < uc || ux > lc) { rdp.tri_n += 2; return; } \
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
		if (ux > lx || lx < uc || ux > lc) { rdp.tri_n += 2; return; } \
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
extern void glide64SPClipVertex(uint32_t i);
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

void glide64SPClipVertex(uint32_t i)
{
   if (rdp.vtxbuf[i].x > rdp.clip_max_x) rdp.clip |= CLIP_XMAX;
   if (rdp.vtxbuf[i].x < rdp.clip_min_x) rdp.clip |= CLIP_XMIN;
   if (rdp.vtxbuf[i].y > rdp.clip_max_y) rdp.clip |= CLIP_YMAX;
   if (rdp.vtxbuf[i].y < rdp.clip_min_y) rdp.clip |= CLIP_YMIN;
}

static void gSPCombineMatrices(void)
{
   MulMatrices(rdp.model, rdp.proj, rdp.combined);
   rdp.update ^= UPDATE_MULT_MAT;
}

static void draw_tri (VERTEX **vtx, uint16_t linew)
{
   int i;
   deltaZ = dzdx = 0;
   if (linew == 0 && (fb_depth_render_enabled || (rdp.rm & ZMODE_DECAL) == ZMODE_DECAL))
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
            if (!rdp.Persp_en)
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
#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[0])
            {
               v->u0 += rdp.aTBuffTex[0]->u_shift + rdp.aTBuffTex[0]->tile_uls;
               v->v0 += rdp.aTBuffTex[0]->v_shift + rdp.aTBuffTex[0]->tile_ult;
            }
#endif

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

#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[0])
            {
               if (rdp.aTBuffTex[0]->tile_uls != (int)rdp.tiles[rdp.cur_tile].f_ul_s)
                  v->u0 -= rdp.tiles[rdp.cur_tile].f_ul_s;
               if (rdp.aTBuffTex[0]->tile_ult != (int)rdp.tiles[rdp.cur_tile].f_ul_t || (settings.hacks&hack_Megaman))
                  v->v0 -= rdp.tiles[rdp.cur_tile].f_ul_t; //required for megaman (boss special attack)
               v->u0 *= rdp.aTBuffTex[0]->u_scale;
               v->v0 *= rdp.aTBuffTex[0]->v_scale;
#ifdef EXTREME_LOGGING
               FRDP("tbuff_tex t0: (%f, %f)->(%f, %f)\n", v->ou, v->ov, v->u0, v->v0);
#endif
            }
            else
#endif
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
#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[1])
            {
               v->u1 += rdp.aTBuffTex[1]->u_shift + rdp.aTBuffTex[1]->tile_uls;
               v->v1 += rdp.aTBuffTex[1]->v_shift + rdp.aTBuffTex[1]->tile_ult;
            }
#endif
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

#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[1])
            {
               if (rdp.aTBuffTex[1]->tile_uls != (int)rdp.tiles[rdp.cur_tile].f_ul_s)
                  v->u1 -= rdp.tiles[rdp.cur_tile].f_ul_s;
               v->u1 *= rdp.aTBuffTex[1]->u_scale;
               v->v1 *= rdp.aTBuffTex[1]->v_scale;
#ifdef EXTREME_LOGGING
               FRDP("tbuff_tex t1: (%f, %f)->(%f, %f)\n", v->ou, v->ov, v->u1, v->v1);
#endif
            }
            else
#endif
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
#ifdef EXTREME_LOGGING
      FRDP("draw_tri. v[%d] ou=%f, ov = %f\n", i, v->ou, v->ov);
#endif
      if (v->shade_mod != cmb.shade_mod_hash)
         apply_shade_mods (v);
   } //for

   rdp.clip = 0;

   if ((vtx[0]->scr_off & 16) ||
         (vtx[1]->scr_off & 16) ||
         (vtx[2]->scr_off & 16))
      rdp.clip |= CLIP_WMIN;

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

   do_triangle_stuff (linew, false);
}

static void cull_trianglefaces(VERTEX **v, unsigned iterations, bool do_update, bool do_cull, int32_t wd)
{
   int32_t i, vcount;
   bool updated_once = false;
   vcount = 0;

   for (i = 0; i < iterations; i++)
   {
      if (do_cull && !cull_tri(v + vcount))
      {
         if (do_update && !updated_once)
         {
            update();
            updated_once = true;
         }
         draw_tri (v + vcount, wd);
      }
      else if (!do_cull)
         draw_tri (v + vcount, wd);
      rdp.tri_n ++;
      vcount += 3;
   }
}

/*
 * Draws 1 triangle.
 *
 * Generates a single triangle face (using the vertices v0, v1, and v2) in the
 * internal vertex buffer loaded by the gSPvertex macro. The flag (0, 1, or 2)
 * identifies which of the three vertices contains the normal or the color for the
 * face (for flat shading).
 *
 * v0   - vertex buffer index of the first coordinate (variable ranges per microcode).
 * v1   - vertex buffer index of the second coordinate (variable ranges per microcode).
 * v2   - vertex buffer index of the third coordinate (variable ranges per microcode).
 * flag - used for flat shading; ordinal ID of the vertex parameter to use for 
 *        shading - triangle face flag 0~2
 *
 * FIXME: not spec-conformant (do_update)
 */
static void gsSP1Triangle(int32_t v0, int32_t v1, int32_t v2, int32_t flag, bool do_update)
{
   VERTEX *v[3];
   if (rdp.skip_drawing)
      return;

   v[0] = &rdp.vtx[v0]; 
   v[1] = &rdp.vtx[v1];
   v[2] = &rdp.vtx[v2];

   cull_trianglefaces(v, 1, do_update, true, 0);

   //FRDP("gsSP1Triangle #%d - %d, %d, %d\n", rdp.tri_n, v1, v2, v3);
}

/*
 * Draws 2 triangles.
 *
 * Generates the first triangle face (using the vertices v00, v01, and v02) in the
 * internal vertex buffer loaded by the gSPvertex macro.
 *
 * Generates the second triangle face (using the vertices v10, v11, and v12) in the
 * internal vertex buffer loaded by the gSPVertex macro. 
 *
 * flag0 (0, 1, or 2)
 * identifies which of the three vertices (v00, v01, v02) contains the normal or the color for the
 * face (for flat shading).
 *
 * v01   - vertex buffer index 1st triangle (variable ranges per microcode).
 * v01   - vertex buffer index triangle (variable ranges per microcode).
 * v02   - vertex buffer index triangle(variable ranges per microcode).
 * flag  - Triangle face flag 0~2 (for v00, v01, v02)
 * v10   - vertex buffer index 2nd triangle (variable ranges per microcode).
 * v11   - vertex buffer index 2nd triangle (variable ranges per microcode).
 * v12   - vertex buffer index 2nd triangle (variable ranges per microcode).
 * flag1 - Triangle face flag 0~2 (for v10, v11, v12)
 *
 */
static void gsSP2Triangles(uint32_t v00, uint32_t v01, uint32_t v02, uint32_t flag0, uint32_t v10, uint32_t v11, uint32_t v12, uint32_t flag1)
{
   VERTEX *v[6];
   
   if (rdp.skip_drawing)
      return;

   v[0] = &rdp.vtx[v00];
   v[1] = &rdp.vtx[v01];
   v[2] = &rdp.vtx[v02];
   v[3] = &rdp.vtx[v10];
   v[4] = &rdp.vtx[v11];
   v[5] = &rdp.vtx[v12];

   cull_trianglefaces(v, 2, true, true, 0);
   //FRDP("uc1:quad3d #%d, #%d\n", rdp.tri_n, rdp.tri_n+1);
}

// Draw four triangle faces
static void gsSP4Triangles(uint32_t v00, uint32_t v01, uint32_t v02, uint32_t flag0,
      uint32_t v10, uint32_t v11, uint32_t v12, uint32_t flag1,
      uint32_t v20, uint32_t v21, uint32_t v22, uint32_t flag2,
      uint32_t v30, uint32_t v31, uint32_t v32, uint32_t flag3
      )
{
   VERTEX *v[12];

   if (rdp.skip_drawing)
      return;

   v[0]  = &rdp.vtx[v00];
   v[1]  = &rdp.vtx[v01];
   v[2]  = &rdp.vtx[v02];
   v[3]  = &rdp.vtx[v10];
   v[4]  = &rdp.vtx[v11];
   v[5]  = &rdp.vtx[v12];
   v[6]  = &rdp.vtx[v20];
   v[7]  = &rdp.vtx[v21];
   v[8]  = &rdp.vtx[v22];
   v[9]  = &rdp.vtx[v30];
   v[10] = &rdp.vtx[v31];
   v[11] = &rdp.vtx[v32];

   cull_trianglefaces(v, 4, true, true, 0);

   //FRDP("uc8:tri4 (#%d - #%d), %d-%d-%d, %d-%d-%d, %d-%d-%d, %d-%d-%d\n", rdp.tri_n, rdp.tri_n+3, ((w0 >> 23) & 0x1F), ((w0 >> 18) & 0x1F), ((((w0 >> 15) & 0x7) << 2) | ((w1 >> 30) &0x3)), ((w0 >> 10) & 0x1F), ((w0 >> 5) & 0x1F), ((w0 >> 0) & 0x1F), ((w1 >> 25) & 0x1F), ((w1 >> 20) & 0x1F), ((w1 >> 15) & 0x1F), ((w1 >> 10) & 0x1F), ((w1 >> 5) & 0x1F), ((w1 >> 0) & 0x1F));
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

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i>>4)];
      int16_t *rdram = (int16_t*)gfx_info.RDRAM;
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      vert->flags  = ((uint16_t*)gfx_info.RDRAM)[(((addr+i) >> 1) + 3)^1];
      vert->ou = (float)rdram[(((addr+i) >> 1) + 4)^1];
      vert->ov = (float)rdram[(((addr+i) >> 1) + 5)^1];
      vert->uv_scaled = 0;
      vert->a    = ((uint8_t*)gfx_info.RDRAM)[(addr+i + 15)^3];

      vert->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      vert->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      vert->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      vert->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
      vert->x_w = vert->x * vert->oow;
      vert->y_w = vert->y * vert->oow;
      vert->z_w = vert->z * vert->oow;
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
         int8_t *rdram = (int8_t*)gfx_info.RDRAM;
         vert->vec[0] = rdram[(addr+i + 12)^3];
         vert->vec[1] = rdram[(addr+i + 13)^3];
         vert->vec[2] = rdram[(addr+i + 14)^3];
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
         uint8_t *rdram = (uint8_t*)gfx_info.RDRAM;
         vert->r = rdram[(addr+i + 12)^3];
         vert->g = rdram[(addr+i + 13)^3];
         vert->b = rdram[(addr+i + 14)^3];
      }
      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
   }
   //FRDP ("rsp:vertex v0:%d, n:%d, from: %08lx\n", v0, n, addr);
}

#ifdef HAVE_NEON
#include <arm_neon.h>

static void gSPVertexNEON(uint32_t addr, uint32_t n, uint32_t v0)
{
   int i;
   float x, y, z;
   float32x4_t comb0, comb1, comb2, comb3;
   float32x4_t v_xyzw;

   comb0 = vld1q_f32(rdp.combined[0]);
   comb1 = vld1q_f32(rdp.combined[1]);
   comb2 = vld1q_f32(rdp.combined[2]);
   comb3 = vld1q_f32(rdp.combined[3]);

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i>>4)];
      int16_t *rdram = (int16_t*)gfx_info.RDRAM;
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      vert->flags  = ((uint16_t*)gfx_info.RDRAM)[(((addr+i) >> 1) + 3)^1];
      vert->ou = (float)rdram[(((addr+i) >> 1) + 4)^1];
      vert->ov = (float)rdram[(((addr+i) >> 1) + 5)^1];
      vert->uv_scaled = 0;
      vert->a    = ((uint8_t*)gfx_info.RDRAM)[(addr+i + 15)^3];

      v_xyzw  = vmulq_n_f32(comb0,x)+vmulq_n_f32(comb1,y)+vmulq_n_f32(comb2,z)+comb3;
      vert->x = vgetq_lane_f32(v_xyzw,0);
      vert->y = vgetq_lane_f32(v_xyzw,1);
      vert->z = vgetq_lane_f32(v_xyzw,2);
      vert->w = vgetq_lane_f32(v_xyzw,3);

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
      v_xyzw = vmulq_n_f32(v_xyzw,vert->oow);
      vert->x_w=vgetq_lane_f32(v_xyzw,0);
      vert->y_w=vgetq_lane_f32(v_xyzw,1);
      vert->z_w=vgetq_lane_f32(v_xyzw,2);
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
         int8_t *rdram = (int8_t*)gfx_info.RDRAM;
         vert->vec[0] = rdram[(addr+i + 12)^3];
         vert->vec[1] = rdram[(addr+i + 13)^3];
         vert->vec[2] = rdram[(addr+i + 14)^3];
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
         uint8_t *rdram = (uint8_t*)gfx_info.RDRAM;
         vert->r = rdram[(addr+i + 12)^3];
         vert->g = rdram[(addr+i + 13)^3];
         vert->b = rdram[(addr+i + 14)^3];
      }
      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
   }
   //FRDP ("rsp:vertex v0:%d, n:%d, from: %08lx\n", v0, n, addr);
}
#endif
