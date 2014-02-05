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

int dzdx = 0;
int deltaZ = 0;
VERTEX **org_vtx;

//software backface culling. Gonetz
// mega modifications by Dave2001

static int cull_tri(VERTEX **v) // type changed to VERTEX** [Dave2001]
{
   int i, draw;
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

   rdp.u_cull_mode >>= CULLSHIFT;

   switch (rdp.u_cull_mode)
   {
      case 1: // cull front
         if (area < 0.0f) //counter-clockwise, positive
            return true;
         break;
      case 2: // cull back
         if (area >= 0.0f) //clockwise, negative
            return true;
         break;
   }

   return false;
}

// FIXME - not consistent with glN64
static void gSPPopMatrix(uint32_t param)
{
   if (rdp.model_i > 0)
   {
      rdp.model_i--;
      memcpy (rdp.model, rdp.model_stack[rdp.model_i], 64);
      rdp.update |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
   }
}

static void gSPPopMatrixN(uint32_t num)
{
   if (rdp.model_i > num - 1)
   {
      rdp.model_i -= num;
      memcpy (rdp.model, rdp.model_stack[rdp.model_i], 64);
      rdp.update |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
   }
}

static void gSPViewport(uint32_t v, bool correct_viewport)
{
   int16_t scale_x, scale_y, scale_z, trans_x, trans_y, trans_z, *rdram;
   uint32_t address = (segoffset(v) & 0xFFFFFF) >> 1;
   rdram = (int16_t*)gfx.RDRAM;

   scale_x = rdram[(address + 0)^1] / 4;
   scale_y = rdram[(address + 1)^1] / 4;
   scale_z = rdram[(address + 2)^1];
   trans_x = rdram[(address + 4)^1] / 4;
   trans_y = rdram[(address + 5)^1] / 4;
   trans_z = rdram[(address + 6)^1];
   if (correct_viewport)
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

   // there are other values than x and y, but I don't know what they do

   rdp.update |= UPDATE_VIEWPORT;
   //FRDP ("viewport scale(%d, %d, %d), trans(%d, %d, %d), from:%08lx\n", scale_x, scale_y, scale_z, trans_x, trans_y, trans_z, a);
}

/*
 * Enables/disables textures and sets the scaling value for texturing in the
 * RSP.
 *
 * The texture coordinates are computed by the RSP using the following formula:
 *
 * (Displayed texture coordinates) = (Scale value) * (Shifted texture coordinate values (see note))
 *
 * Shifted texture coordinate values: These are the values of the texture coordinates obtained
 * from the Vtx structure after they have been shifted by the "shift" parameter set in the texture
 * tile attributes.
 *
 * sc    - scaling value for the texture's s coordinate (16-bit precision, .16)
 * tc    - scaling value for the texture's t coordinate (16-bit precision, .16)
 * level - one less than the maximum number of MIP-map levels (max - 1)
 * tile  - tile descriptor index (3-bit precision, 0~7)
 * on    - texture flag - G_ON (texture on) | G_OFF (texture off)
 */
static void gSPTexture(int32_t sc, int32_t tc, int32_t level,
      int32_t tile, int32_t on)
{
   TILE *tmp_tile;
   if (tile == 7 && (settings.hacks&hack_Supercross))
      tile = 0; //fix for supercross 2000
   rdp.mipmap_level = level;
   rdp.cur_tile = tile;

   rdp.tiles[tile].on = 0;

   if (!on)
      return;

   tmp_tile = (TILE*)&rdp.tiles[tile];
   tmp_tile->on = 1;
   tmp_tile->org_s_scale = sc;
   tmp_tile->org_t_scale = tc;
   tmp_tile->s_scale = (float)(sc+1)/65536.0f;
   tmp_tile->t_scale = (float)(tc+1)/65536.0f;
   tmp_tile->s_scale /= 32.0f;
   tmp_tile->t_scale /= 32.0f;

   rdp.update |= UPDATE_TEXTURE;

   //FRDP("uc0:texture: tile: %d, mipmap_lvl: %d, on: %d, s_scale: %f, t_scale: %f\n", tile, rdp.mipmap_level, on, tmp_tile->s_scale, tmp_tile->t_scale);
}

static void gSPLight(uint32_t l, unsigned n)
{
   uint32_t address = RSP_SegmentToPhysical(l);

   // Get the data
   rdp.light[n].col[0] = (((uint8_t*)gfx.RDRAM)[(address+0)^3]) * 0.0039215689f;
   rdp.light[n].col[1] = (((uint8_t*)gfx.RDRAM)[(address+1)^3]) * 0.0039215689f;
   rdp.light[n].col[2] = (((uint8_t*)gfx.RDRAM)[(address+2)^3]) * 0.0039215689f;
   rdp.light[n].col[3] = 1.0f;
   // ** Thanks to Icepir8 for pointing this out **
   // Lighting must be signed byte instead of byte
   rdp.light[n].dir[0] = (float)(((int8_t*)gfx.RDRAM)[(address+8)^3]) / 127.0f;
   rdp.light[n].dir[1] = (float)(((int8_t*)gfx.RDRAM)[(address+9)^3]) / 127.0f;
   rdp.light[n].dir[2] = (float)(((int8_t*)gfx.RDRAM)[(address+10)^3]) / 127.0f;
   // **

   //rdp.update |= UPDATE_LIGHTS;

   //FRDP ("light: n: %d, r: %.3f, g: %.3f, b: %.3f, x: %.3f, y: %.3f, z: %.3f\n", i, rdp.light[i].r, rdp.light[i].g, rdp.light[i].b, rdp.light_vector[i][0], rdp.light_vector[i][1], rdp.light_vector[i][2]);
}

//gSPForceMatrix command. Modification of uc2_movemem:matrix. Gonetz.
static void gSPForceMatrix(uint32_t mptr)
{
   uint32_t address;
   // do not update the combined matrix!
   rdp.update &= ~UPDATE_MULT_MAT;

   address = RSP_SegmentToPhysical(mptr);
   load_matrix(rdp.combined, address);

   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[0][0], rdp.combined[0][1], rdp.combined[0][2], rdp.combined[0][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[1][0], rdp.combined[1][1], rdp.combined[1][2], rdp.combined[1][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[2][0], rdp.combined[2][1], rdp.combined[2][2], rdp.combined[2][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[3][0], rdp.combined[3][1], rdp.combined[3][2], rdp.combined[3][3]);
}

static void gSPFogFactor(int16_t fm, int16_t fo)
{
   rdp.fog_multiplier = fm;
   rdp.fog_offset = fo;
   //FRDP ("fog: multiplier: %f, offset: %f\n", rdp.fog_multiplier, rdp.fog_offset);
}

// FIXME - not consistent with glN64
static void gSPClipRatio(uint32_t w0, uint32_t w1)
{
   if (((w0 >> 8) & 0xFFFF) == G_MW_CLIP)
   {
      rdp.clip_ratio = squareRoot((float)w1);
      rdp.update |= UPDATE_VIEWPORT;
   }
   //FRDP ("clip %08lx, %08lx\n", w0, w1);
}

static void gSPSetGeometryMode(uint32_t w1)
{
   rdp.geom_mode |= w1;

   // TODO - should we look at all these state changes at this point?
   // This isn't done in gln64

   if (w1 & G_ZBUFFER)  // Z-Buffer enable
   {
      if (!(rdp.flags & ZBUF_ENABLED))
      {
         rdp.flags |= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }
   if (w1 & CULL_FRONT)  // Front culling
   {
      if (!(rdp.flags & CULL_FRONT))
      {
         rdp.flags |= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   if (w1 & CULL_BACK)  // Back culling
   {
      if (!(rdp.flags & CULL_BACK))
      {
         rdp.flags |= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   //Added by Gonetz
   if (w1 & G_FOG)      // Fog enable
   {
      if (!(rdp.flags & FOG_ENABLED))
      {
         rdp.flags |= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
   //FRDP("uc0:setgeometrymode %08lx; result: %08lx\n", w1, rdp.geom_mode);
}

static void gSPClearGeometryMode(uint32_t w1)
{
   rdp.geom_mode &= (~w1);

   // TODO - should we look at all these state changes at this point?
   // This isn't done in gln64
   
   if (w1 & G_ZBUFFER)  // Z-Buffer enable
   {
      if (rdp.flags & ZBUF_ENABLED)
      {
         rdp.flags ^= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }
   if (w1 & CULL_FRONT)  // Front culling
   {
      if (rdp.flags & CULL_FRONT)
      {
         rdp.flags ^= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   if (w1 & CULL_BACK)  // Back culling
   {
      if (rdp.flags & CULL_BACK)
      {
         rdp.flags ^= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   //Added by Gonetz
   if (w1 & G_FOG)      // Fog enable
   {
      if (rdp.flags & FOG_ENABLED)
      {
         rdp.flags ^= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
   //FRDP("uc0:cleargeometrymode %08lx\n", w1);
}

static void gSPDisplayList(uint32_t dl)
{
   uint32_t address = RSP_SegmentToPhysical(dl);

   // Don't execute display list
   // This fixes partially Gauntlet: Legends (first condition)
   if (address == rdp.pc[rdp.pc_i] - 8 || rdp.pc_i >= 9)
      return;

   rdp.pc_i ++;  // go to the next PC in the stack
   rdp.pc[rdp.pc_i] = address;  // jump to the address
}

static void gSPBranchList(uint32_t dl)
{
   uint32_t address = RSP_SegmentToPhysical(dl);

   // Don't execute display list
   // This fixes partially Gauntlet: Legends (first condition)
   if (address == rdp.pc[rdp.pc_i] - 8 || rdp.pc_i >= 9)
      return;

   rdp.pc[rdp.pc_i] = address;  // just jump to the address
}

static void gSPEndDisplayList(void)
{
   if (rdp.pc_i > 0)
      rdp.pc_i --;
   else
      rdp.halt = 1; // Halt execution here
}

static inline void gSPSegment( int32_t seg, int32_t base )
{
    rdp.segment[seg] = base;
    //FRDP ("segment: %08lx -> seg%d\n", seg, base);
}

static inline void gSPNumLights(int32_t n)
{
   rdp.num_lights = (n <= 8) ? n : 0;
   rdp.update |= UPDATE_LIGHTS;
   //FRDP ("numlights: %d\n", rdp.num_lights);
}

static void gSPLightColor( uint32_t n, uint32_t packedColor)
{
   rdp.light[n].col[0] = _SHIFTR( packedColor, 24, 8 ) * 0.0039215689f;
   rdp.light[n].col[1] = _SHIFTR( packedColor, 16, 8 ) * 0.0039215689f;
   rdp.light[n].col[2] = _SHIFTR( packedColor, 8, 8 )  * 0.0039215689f;
   rdp.light[n].col[3] = 255;
   //FRDP ("lightcol light:%d, %08lx\n", n, w1);
}

// FIXME - call not consistent with glN64
static void gSPModifyVertex(uint8_t where, uint16_t vtx, uint32_t val)
{
   VERTEX *v = &rdp.vtx[vtx];
   uint32_t w0, w1;
   w0 = rdp.cmd0;
   w1 = rdp.cmd1;

   switch (where)
   {
      case 0:
         uc6_obj_sprite(w0, w1);
         break;

      case G_MWO_POINT_RGBA:    // RGBA
         v->r = (uint8_t)(val >> 24);
         v->g = (uint8_t)((val >> 16) & 0xFF);
         v->b = (uint8_t)((val >> 8) & 0xFF);
         v->a = (uint8_t)(val & 0xFF);
         v->shade_mod = 0;

         //FRDP ("RGBA: %d, %d, %d, %d\n", v->r, v->g, v->b, v->a);
         break;

      case G_MWO_POINT_ST:    // ST
         {
            float scale = rdp.Persp_en ? 0.03125f : 0.015625f;
            v->ou = (float)((int16_t)(val>>16)) * scale;
            v->ov = (float)((int16_t)(val&0xFFFF)) * scale;
            v->uv_calculated = 0xFFFFFFFF;
            v->uv_scaled = 1;
         }
         //FRDP ("u/v: (%04lx, %04lx), (%f, %f)\n", (int16_t)(val>>16), (int16_t)(val&0xFFFF), v->ou, v->ov);
         break;

      case G_MWO_POINT_XYSCREEN:    // XY screen
         {
            float scr_x = (float)((int16_t)(val>>16)) / 4.0f;
            float scr_y = (float)((int16_t)(val&0xFFFF)) / 4.0f;
            v->screen_translated = 2;
            v->sx = scr_x * rdp.scale_x + rdp.offset_x;
            v->sy = scr_y * rdp.scale_y + rdp.offset_y;
            if (v->w < 0.01f)
            {
               v->w = 1.0f;
               v->oow = 1.0f;
               v->z_w = 1.0f;
            }
            v->sz = rdp.view_trans[2] + v->z_w * rdp.view_scale[2];

            v->scr_off = 0;
            if (scr_x < 0) v->scr_off |= 1;
            if (scr_x > rdp.vi_width) v->scr_off |= 2;
            if (scr_y < 0) v->scr_off |= 4;
            if (scr_y > rdp.vi_height) v->scr_off |= 8;
            if (v->w < 0.1f) v->scr_off |= 16;

            //FRDP ("x/y: (%f, %f)\n", scr_x, scr_y);
         }
         break;

      case G_MWO_POINT_ZSCREEN:    // Z screen
         {
            float scr_z = (float)((int16_t)(val>>16));
            v->z_w = (scr_z - rdp.view_trans[2]) / rdp.view_scale[2];
            v->z = v->z_w * v->w;
            //FRDP ("z: %f\n", scr_z);
         }
         break;

      default:
         //LRDP("UNKNOWN\n");
         break;
   }
   //FRDP ("uc0:modifyvtx: vtx: %d, where: 0x%02lx, val: %08lx - ", vtx, where, w1);
}

void glide64SPClipVertex(uint32_t i)
{
   if (rdp.vtxbuf[i].x > rdp.clip_max_x) rdp.clip |= CLIP_XMAX;
   if (rdp.vtxbuf[i].x < rdp.clip_min_x) rdp.clip |= CLIP_XMIN;
   if (rdp.vtxbuf[i].y > rdp.clip_max_y) rdp.clip |= CLIP_YMAX;
   if (rdp.vtxbuf[i].y < rdp.clip_min_y) rdp.clip |= CLIP_YMIN;
}

static void gSPBranchLessZ( uint32_t branchdl, uint32_t vtx, float zval )
{
   uint32_t address = RSP_SegmentToPhysical( branchdl );
   if( fabs(rdp.vtx[vtx].z) <= zval )
      rdp.pc[rdp.pc_i] = address;
   //FRDP ("uc1:branch_less_z, addr: %08lx\n", address);
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
      VERTEX *v = vtx[i];

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
   int i, vcount;
   bool updated_once = false;
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

// Generates one line, using vertices v0, v1 in the internal vertex
// buffer.
// Allows you to specify the line width (wd) in half-pixel units
static void gSPLineW3D(int32_t v0, int32_t v1, int32_t wd, int32_t flag)
{
   VERTEX *v[3];
   v[0] = &rdp.vtx[v1];
   v[1] = &rdp.vtx[v0];
   v[2] = &rdp.vtx[v0];

   uint32_t cull_mode = (rdp.flags & CULLMASK) >> CULLSHIFT;
   rdp.flags |= CULLMASK;
   rdp.update |= UPDATE_CULL_MODE;

   cull_trianglefaces(v, 1, true, true, wd);

   rdp.flags ^= CULLMASK;
   rdp.flags |= cull_mode << CULLSHIFT;
   rdp.update |= UPDATE_CULL_MODE;

   //FRDP("uc0:line3d v0:%d, v1:%d, width:%d\n", v0, v1, wd);
}

// Draw a single triangle face
static void gsSP1Triangle(int32_t v0, int32_t v1, int32_t v2, int32_t flag, bool do_update)
{
   VERTEX *v[3];

   v[0] = &rdp.vtx[v0]; 
   v[1] = &rdp.vtx[v1];
   v[2] = &rdp.vtx[v2];

   cull_trianglefaces(v, 1, do_update, true, 0);

   //FRDP("gsSP1Triangle #%d - %d, %d, %d\n", rdp.tri_n, v1, v2, v3);
}

// Draw two triangle faces
static void gsSP2Triangles(uint32_t v00, uint32_t v01, uint32_t v02, uint32_t flag0, uint32_t v10, uint32_t v11, uint32_t v12, uint32_t flag1)
{
   VERTEX *v[6];

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

/*
 * Tells RDP to draw a textured 2D rectangle. Afterwards you can call the gDPFillRectangle
 * command to fill rectangles with a solid color.
 *
 * The rectangle drawn is iclusive of the coordinates specified in copy mode. In one-cycle
 * or two-cycle mode, the rectangle is drawn exclusive of the bottom and right edges in order to
 * provide proper anti-aliasing in these modes.
 *
 * ul_x - High X coordinate of rectangle (10.2 fixed point format)
 *
 * ul_y - High Y coordinate of rectangle (10.2 fixed point format)
 *
 * lr_x - Low X coordinate of rectangle (10.2 fixed point format)
 *
 * lr_y - Low Y coordinate of rectangle (10.2 fixed point format)
 *
 * tile - The texture tile number that selects one of 8 texture tile descriptors in the RDP.
 *
 * off_x_i - Specify the texture coordinate of the upper-left corner of the 
 *        rectangle (10.5 fixed point format).
 *
 * off_y_i - Specify the texture coordinate of the upper-left corner of the
 *        rectangle (10.5 fixed point format).
 *
 * dsdx - Specifies change in s per change in x (10.5 fixed point format).
 *
 * dtdy - Specifies change in s per change in y (10.5 fixed point format).
 *
 * flip - Specifies whether texture rectangle needs flipping.
 */
static void gSPTextureRectangle(uint32_t ul_x, uint32_t ul_y, uint32_t lr_x, uint32_t lr_y,
      uint32_t tile, int32_t off_x_i, int32_t off_y_i, int32_t _dsdx, int32_t _dtdy,
      uint32_t flip)
{
   uint32_t prev_tile, cmd0;
   float Z, dsdx, dtdy, s_ul_x, s_lr_x, s_ul_y, s_lr_y, off_size_x, off_size_y;
   int i;

   rdp.texrecting = 1;

   prev_tile = rdp.cur_tile;
   rdp.cur_tile = tile;

   Z = set_sprite_combine_mode ();

   rdp.texrecting = 0;

   if (!rdp.cur_cache[0])
   {
      rdp.cur_tile = prev_tile;
      rdp.tri_n += 2;
      return;
   }

   // ****
   // ** Texrect offset by Gugaman **
   //
   //integer representation of texture coordinate.
   //needed to detect and avoid overflow after shifting
   dsdx = (float)(int16_t)(_dsdx) / 1024.0f;
   dtdy = (float)(int16_t)(_dtdy) / 1024.0f;
   if (off_x_i & 0x8000) //check for sign bit
      off_x_i |= ~0xffff; //make it negative
   //the same as for off_x_i
   if (off_y_i & 0x8000)
      off_y_i |= ~0xffff;

   if (rdp.cycle_mode == G_CYC_COPY)
      dsdx /= 4.0f;

   s_ul_x = ul_x * rdp.scale_x + rdp.offset_x;
   s_lr_x = lr_x * rdp.scale_x + rdp.offset_x;
   s_ul_y = ul_y * rdp.scale_y + rdp.offset_y;
   s_lr_y = lr_y * rdp.scale_y + rdp.offset_y;

   //FRDP("texrect (%.2f, %.2f, %.2f, %.2f), tile: %d, #%d, #%d\n", ul_x, ul_y, lr_x, lr_y, tile, rdp.tri_n, rdp.tri_n+1);
   //FRDP ("(%f, %f) -> (%f, %f), s: (%d, %d) -> (%d, %d)\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y, rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);
   //FRDP("\toff_x: %f, off_y: %f, dsdx: %f, dtdy: %f\n", off_x_i/32.0f, off_y_i/32.0f, dsdx, dtdy);

   if (flip == 0xE5 )
   {
      //texrectflip
      off_size_x = (lr_y - ul_y - 1) * dsdx;
      off_size_y = (lr_x - ul_x - 1) * dtdy;
   }
   else
   {
      off_size_x = (lr_x - ul_x - 1) * dsdx;
      off_size_y = (lr_y - ul_y - 1) * dtdy;
   }

   struct {
      float ul_u, ul_v, lr_u, lr_v;
   } texUV[2]; //struct for texture coordinates

   //calculate texture coordinates
   for (i = 0; i < 2; i++)
   {
      if (rdp.cur_cache[i] && (rdp.tex & (i+1)))
      {
         float sx = 1, sy = 1;
         int x_i = off_x_i, y_i = off_y_i;
         TILE *tile = &rdp.tiles[rdp.cur_tile + i];
         //shifting
         if (tile->shift_s)
         {
            if (tile->shift_s > 10)
            {
               uint8_t iShift = (16 - tile->shift_s);
               x_i <<= iShift;
               sx = (float)(1 << iShift);
            }
            else
            {
               uint8_t iShift = tile->shift_s;
               x_i >>= iShift;
               sx = 1.0f/(float)(1 << iShift);
            }
         }
         if (tile->shift_t)
         {
            if (tile->shift_t > 10)
            {
               uint8_t iShift = (16 - tile->shift_t);
               y_i <<= iShift;
               sy = (float)(1 << iShift);
            }
            else
            {
               uint8_t iShift = tile->shift_t;
               y_i >>= iShift;
               sy = 1.0f/(float)(1 << iShift);
            }
         }

#ifdef HAVE_HWFBE
         if (rdp.aTBuffTex[i]) //hwfbe texture
         {
            float t0_off_x;
            float t0_off_y;
            if (off_x_i + off_y_i == 0)
            {
               t0_off_x = tile->ul_s;
               t0_off_y = tile->ul_t;
            }
            else
            {
               t0_off_x = off_x_i/32.0f;
               t0_off_y = off_y_i/32.0f;
            }
            t0_off_x += rdp.aTBuffTex[i]->u_shift;// + tile->ul_s; //commented for Paper Mario motion blur
            t0_off_y += rdp.aTBuffTex[i]->v_shift;// + tile->ul_t;
            texUV[i].ul_u = t0_off_x * sx;
            texUV[i].ul_v = t0_off_y * sy;

            texUV[i].lr_u = texUV[i].ul_u + off_size_x * sx;
            texUV[i].lr_v = texUV[i].ul_v + off_size_y * sy;

            texUV[i].ul_u *= rdp.aTBuffTex[i]->u_scale;
            texUV[i].ul_v *= rdp.aTBuffTex[i]->v_scale;
            texUV[i].lr_u *= rdp.aTBuffTex[i]->u_scale;
            texUV[i].lr_v *= rdp.aTBuffTex[i]->v_scale;
            FRDP("tbuff_tex[%d] ul_u: %f, ul_v: %f, lr_u: %f, lr_v: %f\n",
                  i, texUV[i].ul_u, texUV[i].ul_v, texUV[i].lr_u, texUV[i].lr_v);
         }
         else //common case
#endif
         {
            //kill 10.5 format overflow by SIGN16 macro
            texUV[i].ul_u = SIGN16(x_i) / 32.0f;
            texUV[i].ul_v = SIGN16(y_i) / 32.0f;

            texUV[i].ul_u -= tile->f_ul_s;
            texUV[i].ul_v -= tile->f_ul_t;

            texUV[i].lr_u = texUV[i].ul_u + off_size_x * sx;
            texUV[i].lr_v = texUV[i].ul_v + off_size_y * sy;

            texUV[i].ul_u = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_x * texUV[i].ul_u;
            texUV[i].lr_u = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_x * texUV[i].lr_u;
            texUV[i].ul_v = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_y * texUV[i].ul_v;
            texUV[i].lr_v = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_y * texUV[i].lr_v;
         }
      }
      else
      {
         texUV[i].ul_u = texUV[i].ul_v = texUV[i].lr_u = texUV[i].lr_v = 0;
      }
   }
   rdp.cur_tile = prev_tile;

   // ****

   FRDP ("  scissor: (%d, %d) -> (%d, %d)\n", rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);

   CCLIP2 (s_ul_x, s_lr_x, texUV[0].ul_u, texUV[0].lr_u, texUV[1].ul_u, texUV[1].lr_u, (float)rdp.scissor.ul_x, (float)rdp.scissor.lr_x);
   CCLIP2 (s_ul_y, s_lr_y, texUV[0].ul_v, texUV[0].lr_v, texUV[1].ul_v, texUV[1].lr_v, (float)rdp.scissor.ul_y, (float)rdp.scissor.lr_y);

   FRDP ("  draw at: (%f, %f) -> (%f, %f)\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y);

   VERTEX vstd[4] = {
      { s_ul_x, s_ul_y, Z, 1.0f, texUV[0].ul_u, texUV[0].ul_v, texUV[1].ul_u, texUV[1].ul_v, {0, 0, 0, 0}, 255 },
      { s_lr_x, s_ul_y, Z, 1.0f, texUV[0].lr_u, texUV[0].ul_v, texUV[1].lr_u, texUV[1].ul_v, {0, 0, 0, 0}, 255 },
      { s_ul_x, s_lr_y, Z, 1.0f, texUV[0].ul_u, texUV[0].lr_v, texUV[1].ul_u, texUV[1].lr_v, {0, 0, 0, 0}, 255 },
      { s_lr_x, s_lr_y, Z, 1.0f, texUV[0].lr_u, texUV[0].lr_v, texUV[1].lr_u, texUV[1].lr_v, {0, 0, 0, 0}, 255 } };

   if ( flip == 0xE5 )
   {
      //texrectflip
      vstd[1].u0 = texUV[0].ul_u;
      vstd[1].v0 = texUV[0].lr_v;
      vstd[1].u1 = texUV[1].ul_u;
      vstd[1].v1 = texUV[1].lr_v;

      vstd[2].u0 = texUV[0].lr_u;
      vstd[2].v0 = texUV[0].ul_v;
      vstd[2].u1 = texUV[1].lr_u;
      vstd[2].v1 = texUV[1].ul_v;
   }

   VERTEX *vptr = vstd;
   int n_vertices = 4;

   AllowShadeMods (vptr, n_vertices);
   for (i=0; i<n_vertices; i++)
      apply_shade_mods (&vptr[i]);

   {
      if (rdp.fog_mode >= FOG_MODE_BLEND)
      {
         float fog;
         if (rdp.fog_mode == FOG_MODE_BLEND)
            fog = 1.0f/max(1, rdp.fog_color&0xFF);
         else
            fog = 1.0f/max(1, (~rdp.fog_color)&0xFF);

         for (i = 0; i < n_vertices; i++)
            vptr[i].f = fog;

         grFogMode (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT);
      }

      ConvertCoordsConvert (vptr, n_vertices);

      grDrawVertexArrayContiguous (GR_TRIANGLE_STRIP, n_vertices, vptr, sizeof(VERTEX));

      rdp.tri_n += 2;
   }
}

/*
 * Loads into the RSP vertex buffer the vertices that will be used by the 
 * gSP1Triangle commands to generate polygons.
 *
 * v  - Segment address of the vertex list.
 * n  - Number of vertices (1 - 32).
 * v0 - Starting index in vertex buffer where vertices are to be loaded.
 */
static void gSPVertex(uint32_t addr, uint32_t n, uint32_t v0)
{
   int i;
   float x, y, z;

   rdp.v0 = v0; // Current vertex
   rdp.vn = n;  // Number to copy

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

   //FRDP ("rsp:vertex v0:%d, n:%d, from: %08lx\n", v0, n, addr);

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i>>4)];
      int16_t *rdram = (int16_t*)gfx.RDRAM;
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      vert->flags  = ((uint16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 3)^1];
      vert->ou = (float)rdram[(((addr+i) >> 1) + 4)^1];
      vert->ov = (float)rdram[(((addr+i) >> 1) + 5)^1];
      vert->uv_scaled = 0;
      vert->a    = ((uint8_t*)gfx.RDRAM)[(addr+i + 15)^3];

      vert->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      vert->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      vert->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      vert->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];


      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
      vert->x_w = vert->x * vert->oow;
      vert->y_w = vert->y * vert->oow;
      vert->z_w = vert->z * vert->oow;
      CalculateFog (vert);

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

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
         int8_t *rdram = (int8_t*)gfx.RDRAM;
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
         NormalizeVector (vert->vec);

         calc_light (vert);
      }
      else
      {
         uint8_t *rdram = (uint8_t*)gfx.RDRAM;
         vert->r = rdram[(addr+i + 12)^3];
         vert->g = rdram[(addr+i + 13)^3];
         vert->b = rdram[(addr+i + 14)^3];
      }
      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
   }
}
