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
         float fdzdy = (float)((diffz_02*diffx_12 - diffz_12*diffx_02) / denom);
         float fdz = (float)(fabs(fdzdx) + fabs(fdzdy));
         if ((settings.hacks & hack_Zelda) && (rdp.rm & 0x800))
            fdz *= 4.0;  // Decal mode in Zelda sometimes needs mutiplied deltaZ to work correct, e.g. roads
         deltaZ = max(8, (int)fdz);
      }
      dzdx = (int)(fdzdx * 65536.0); }
}

static void draw_tri_uv_calculation(VERTEX **vtx, VERTEX *v)
{
   //FRDP(" * CALCULATING VERTEX U/V: %d\n", v->number);

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
   v->uv_calculated = rdp.tex_ctr;
}

static void draw_tri (VERTEX **vtx, uint16_t linew)
{
   int i;

   org_vtx = vtx;

   for (i = 0; i < 3; i++)
   {
      VERTEX *v = (VERTEX*)vtx[i];

      if (v->uv_calculated != rdp.tex_ctr)
         draw_tri_uv_calculation(vtx, v);
      if (v->shade_mod != cmb.shade_mod_hash)
         apply_shade_mods (v);
   }

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
   uint32_t i;
   int32_t vcount = 0;

   if (do_update)
      update();

   for (i = 0; i < iterations; i++, vcount += 3)
   {
      if (do_cull)
         if (cull_tri(v + vcount))
            continue;

      deltaZ = dzdx = 0;
      if (wd == 0 && (fb_depth_render_enabled || (rdp.rm & ZMODE_DECAL) == ZMODE_DECAL))
         draw_tri_depth(v + vcount);
      draw_tri (v + vcount, wd);
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

#ifdef __ARM_NEON__
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
static void gSPVertex_G64(uint32_t v, uint32_t n, uint32_t v0)
{
   unsigned int i;
   float x, y, z;
   uint32_t iter = 16;
   void   *vertex  = (void*)(gfx_info.RDRAM + v);

   for (i=0; i < (n * iter); i+= iter)
   {
      VERTEX *vtx = (VERTEX*)&rdp.vtx[v0 + (i / iter)];
      int16_t *rdram    = (int16_t*)vertex;
      uint8_t *rdram_u8 = (uint8_t*)vertex;
      uint8_t *color = (uint8_t*)(rdram_u8 + 12);
      y                 = (float)rdram[0];
      x                 = (float)rdram[1];
      vtx->flags        = (uint16_t)rdram[2];
      z                 = (float)rdram[3];
      vtx->ov           = (float)rdram[4];
      vtx->ou           = (float)rdram[5];
      vtx->uv_scaled    = 0;
      vtx->a            = color[0];

      vtx->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      vtx->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      vtx->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      vtx->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];

      vtx->uv_calculated = 0xFFFFFFFF;
      vtx->screen_translated = 0;
      vtx->shade_mod = 0;

      if (fabs(vtx->w) < 0.001)
         vtx->w = 0.001f;
      vtx->oow = 1.0f / vtx->w;
      vtx->x_w = vtx->x * vtx->oow;
      vtx->y_w = vtx->y * vtx->oow;
      vtx->z_w = vtx->z * vtx->oow;
      CalculateFog (vtx);

      vtx->scr_off = 0;
      if (vtx->x < -vtx->w)
         vtx->scr_off |= 1;
      if (vtx->x > vtx->w)
         vtx->scr_off |= 2;
      if (vtx->y < -vtx->w)
         vtx->scr_off |= 4;
      if (vtx->y > vtx->w)
         vtx->scr_off |= 8;
      if (vtx->w < 0.1f)
         vtx->scr_off |= 16;
#if 0
      if (vtx->z_w > 1.0f)
         vtx->scr_off |= 32;
#endif

      if (rdp.geom_mode & G_LIGHTING)
      {
         vtx->vec[0] = (int8_t)color[3];
         vtx->vec[1] = (int8_t)color[2];
         vtx->vec[2] = (int8_t)color[1];

         if (rdp.geom_mode & G_TEXTURE_GEN)
         {
            if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
               calc_linear (vtx);
            else
               calc_sphere (vtx);
         }

         if (settings.ucode == 2 && rdp.geom_mode & G_POINT_LIGHTING)
         {
            float tmpvec[3] = {x, y, z};
            calc_point_light (vtx, tmpvec);
         }
         else
         {
            NormalizeVector (vtx->vec);
            calc_light (vtx);
         }
      }
      else
      {
         vtx->r = color[3];
         vtx->g = color[2];
         vtx->b = color[1];
      }
      vertex = (char*)vertex + iter;
   }
}

static INLINE void loadBlock(uint32_t *src, uint32_t *dst, uint32_t off, int dxt, int cnt)
{
   uint32_t *v5, *v7, v8, v10, v13, v14, nbits;
   int32_t v6, v9, v16, v18;

   nbits = sizeof(uint32_t) * 8;
   v5 = dst;
   v6 = cnt;
   if ( cnt )
   {
      v7 = (uint32_t *)((int8_t*)src + (off & 0xFFFFFFFC));
      v8 = off & 3;
      if ( !(off & 3) )
         goto LABEL_23;
      v9 = 4 - v8;
      v10 = *v7++;
      do
      {
         v10 = __ROL__(v10, 8, nbits);
      }while (--v8);
      do
      {
         *v5++ = __ROL__(v10, 8, nbits);
      }while (--v9);
      *v5++ = bswap32(*v7++);
      v6 = cnt - 1;
      if ( cnt != 1 )
      {
LABEL_23:
         do
         {
            *v5++ = bswap32(*v7++);
            *v5++ = bswap32(*v7++);
         }while (--v6 );
      }
      v13 = off & 3;
      if ( off & 3 )
      {
         v14 = *(uint32_t *)((int8_t*)src + ((8 * cnt + off) & 0xFFFFFFFC));
         do
         {
            *v5++ = __ROL__(v14, 8, nbits);
         }while (--v13);
      }
   }
   v6 = cnt;
   v16 = 0;
   v18 = 0;
dxt_test:
   do
   {
      dst += 2;
      if ( !--v6 )
         break;
      v16 += dxt;
      if ( v16 < 0 )
      {
         do
         {
            ++v18;
            if ( !--v6 )
               goto end_dxt_test;
            v16 += dxt;
            if ( v16 >= 0 )
            {
               do
               {
                  *dst ^= dst[1];
                  dst[1] ^= *dst;
                  *dst ^= dst[1];
                  dst += 2;
               }while(--v18);
               goto dxt_test;
            }
         }while(1);
      }
   }while(1);
end_dxt_test:
   while ( v18 )
   {
      *dst ^= dst[1];
      dst[1] ^= *dst;
      *dst ^= dst[1];
      dst += 2;
      --v18;
   }
}

/*
* Loads a texture from DRAM into texture memory (TMEM).
* The texture image is loaded into memory in a single transfer.
*
* tile - Tile descriptor index 93-bit precision, 0~7).
* ul_s - texture tile's upper-left s coordinate (10.2, 0.0~1023.75)
* ul_t - texture tile's upper-left t coordinate (10.2, 0.0~1023.75)
* lr_s - texture tile's lower-right s coordinate (10.2, 0.0~1023.75)
* dxt - amount of change in value of t per scan line (12-bit precision, 0~4095)
*/
static void gDPLoadBlock( uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt )
{
   uint32_t _dxt, addr, off, cnt;
   uint8_t *dst;

   if (rdp.skip_drawing)
      return;

   if (ucode5_texshiftaddr)
   {
      if (ucode5_texshift % ((lr_s+1)<<3))
      {
         rdp.timg.addr -= ucode5_texshift;
         ucode5_texshiftaddr = 0;
         ucode5_texshift = 0;
         ucode5_texshiftcount = 0;
      }
      else
         ucode5_texshiftcount++;
   }

   rdp.addr[rdp.tiles[tile].t_mem] = rdp.timg.addr;

   // ** DXT is used for swapping every other line
   /* double fdxt = (double)0x8000000F/(double)((uint32_t)(2047/(dxt-1))); // F for error
      uint32_t _dxt = (uint32_t)fdxt;*/

   // 0x00000800 -> 0x80000000 (so we can check the sign bit instead of the 11th bit)
   _dxt = dxt << 20;
   addr = segoffset(rdp.timg.addr) & BMASK;

   rdp.tiles[tile].ul_s = ul_s;
   rdp.tiles[tile].ul_t = ul_t;
   rdp.tiles[tile].lr_s = lr_s;

   rdp.timg.set_by = 0; // load block

   // do a quick boundary check before copying to eliminate the possibility for exception
   if (ul_s >= 512)
   {
      lr_s = 1; // 1 so that it doesn't die on memcpy
      ul_s = 511;
   }
   if (ul_s+lr_s > 512)
      lr_s = 512-ul_s;

   if (addr+(lr_s<<3) > BMASK+1)
      lr_s = (uint16_t)((BMASK-addr)>>3);

   //angrylion's advice to use ul_s in texture image offset and cnt calculations.
   //Helps to fix Vigilante 8 jpeg backgrounds and logos
   off = rdp.timg.addr + (ul_s << rdp.tiles[tile].size >> 1);
   dst = ((uint8_t*)rdp.tmem) + (rdp.tiles[tile].t_mem<<3);
   cnt = lr_s-ul_s+1;
   if (rdp.tiles[tile].size == 3)
      cnt <<= 1;

   if (rdp.timg.size == 3)
      LoadBlock32b(tile, ul_s, ul_t, lr_s, dxt);
   else
      loadBlock((uint32_t *)gfx_info.RDRAM, (uint32_t *)dst, off, _dxt, cnt);

   rdp.timg.addr += cnt << 3;
   rdp.tiles[tile].lr_t = ul_t + ((dxt*cnt)>>11);

   rdp.update |= UPDATE_TEXTURE;

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
      setTBufTex(rdp.tiles[tile].t_mem, cnt);
#endif
   //FRDP ("loadblock: tile: %d, ul_s: %d, ul_t: %d, lr_s: %d, dxt: %08lx -> %08lx\n", tile, ul_s, ul_t, lr_s, dxt, _dxt);
}

static void gSPLookAt_G64(uint32_t l, uint32_t n)
{
   int8_t  *rdram_s8  = (int8_t*) (gfx_info.RDRAM  + RSP_SegmentToPhysical(l));
   int8_t dir_x = rdram_s8[11];
   int8_t dir_y = rdram_s8[10];
   int8_t dir_z = rdram_s8[9];
   rdp.lookat[n][0] = (float)(dir_x) / 127.0f;
   rdp.lookat[n][1] = (float)(dir_y) / 127.0f;
   rdp.lookat[n][2] = (float)(dir_z) / 127.0f;
   rdp.use_lookat = (n == 0) || (n == 1 && (dir_x || dir_y));
#ifdef EXTREME_LOGGING
   //FRDP("lookat_x (%f, %f, %f)\n", rdp.lookat[1][0], rdp.lookat[1][1], rdp.lookat[1][2]);
#endif
}

static void gSPLight_G64(uint32_t l, int32_t n)
{
   int16_t *rdram     = (int16_t*)(gfx_info.RDRAM  + RSP_SegmentToPhysical(l));
   uint8_t *rdram_u8  = (uint8_t*)(gfx_info.RDRAM  + RSP_SegmentToPhysical(l));
   int8_t  *rdram_s8  = (int8_t*) (gfx_info.RDRAM  + RSP_SegmentToPhysical(l));

	--n;

	if (n < 8)
   {
      /* Get the data */
      rdp.light[n].nonblack  = rdram_u8[3];
      rdp.light[n].nonblack += rdram_u8[2];
      rdp.light[n].nonblack += rdram_u8[1];

      rdp.light[n].col[0]    = rdram_u8[3] / 255.0f;
      rdp.light[n].col[1]    = rdram_u8[2] / 255.0f;
      rdp.light[n].col[2]    = rdram_u8[1] / 255.0f;
      rdp.light[n].col[3]    = 1.0f;

      // ** Thanks to Icepir8 for pointing this out **
      // Lighting must be signed byte instead of byte
      rdp.light[n].dir[0] = (float)rdram_s8[11] / 127.0f;
      rdp.light[n].dir[1] = (float)rdram_s8[10] / 127.0f;
      rdp.light[n].dir[2] = (float)rdram_s8[9] / 127.0f;

      rdp.light[n].x = (float)rdram[5];
      rdp.light[n].y = (float)rdram[4];
      rdp.light[n].z = (float)rdram[7];
      rdp.light[n].ca = (float)rdram[0] / 16.0f;
      rdp.light[n].la = (float)rdram[4];
      rdp.light[n].qa = (float)rdram[13] / 8.0f;
      //rdp.update |= UPDATE_LIGHTS;
   }
}

static void gSPViewport_G64(uint32_t v)
{
   int16_t *rdram     = (int16_t*)(gfx_info.RDRAM  + RSP_SegmentToPhysical( v ));

   int16_t scale_y = rdram[0] >> 2;
   int16_t scale_x = rdram[1] >> 2;
   int16_t scale_z = rdram[3];
   int16_t trans_x = rdram[5] >> 2;
   int16_t trans_y = rdram[4] >> 2;
   int16_t trans_z = rdram[7];
   if (settings.correct_viewport)
   {
      scale_x = abs(scale_x);
      scale_y = abs(scale_y);
   }
   rdp.view_scale[0] = scale_x * rdp.scale_x;
   rdp.view_scale[1] = -scale_y * rdp.scale_y;
   rdp.view_scale[2] = 32.0f * scale_z;
   rdp.view_trans[0] = trans_x * rdp.scale_x;
   rdp.view_trans[1] = trans_y * rdp.scale_y;
   rdp.view_trans[2] = 32.0f * trans_z;

   rdp.update |= UPDATE_VIEWPORT;
}

static void gSPFogFactor_G64(int16_t fm, int16_t fo )
{
   rdp.fog_multiplier = fm;
   rdp.fog_offset     = fo;
}

static void gSPNumLights_G64(int32_t n)
{
   if (n > 12)
      return;

   rdp.num_lights = n;
   rdp.update |= UPDATE_LIGHTS;
   //FRDP ("numlights: %d\n", rdp.num_lights);
}
