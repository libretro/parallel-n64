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

static void gSPTexture(void)
{
   uint32_t on;
   int tile;
   tile = (rdp.cmd0 >> 8) & 0x07;
   if (tile == 7 && (settings.hacks&hack_Supercross))
      tile = 0; //fix for supercross 2000
   rdp.mipmap_level = (rdp.cmd0 >> 11) & 0x07;
   on = (rdp.cmd0 & 0xFF);
   rdp.cur_tile = tile;

   rdp.tiles[tile].on = 0;

   if (on)
   {
      uint16_t s = (uint16_t)((rdp.cmd1 >> 16) & 0xFFFF);
      uint16_t t = (uint16_t)(rdp.cmd1 & 0xFFFF);

      TILE *tmp_tile = &rdp.tiles[tile];
      tmp_tile->on = 1;
      tmp_tile->org_s_scale = s;
      tmp_tile->org_t_scale = t;
      tmp_tile->s_scale = (float)(s+1)/65536.0f;
      tmp_tile->t_scale = (float)(t+1)/65536.0f;
      tmp_tile->s_scale /= 32.0f;
      tmp_tile->t_scale /= 32.0f;

      rdp.update |= UPDATE_TEXTURE;

      //FRDP("uc0:texture: tile: %d, mipmap_lvl: %d, on: %d, s_scale: %f, t_scale: %f\n", tile, rdp.mipmap_level, on, tmp_tile->s_scale, tmp_tile->t_scale);
   }
}

static void gSPLight(uint32_t l, unsigned n)
{
   uint32_t address = RSP_SegmentToPhysical(l);

   // Get the data
   rdp.light[n].r = (float)(((uint8_t*)gfx.RDRAM)[(address+0)^3]) / 255.0f;
   rdp.light[n].g = (float)(((uint8_t*)gfx.RDRAM)[(address+1)^3]) / 255.0f;
   rdp.light[n].b = (float)(((uint8_t*)gfx.RDRAM)[(address+2)^3]) / 255.0f;
   rdp.light[n].a = 1.0f;
   // ** Thanks to Icepir8 for pointing this out **
   // Lighting must be signed byte instead of byte
   rdp.light[n].dir_x = (float)(((int8_t*)gfx.RDRAM)[(address+8)^3]) / 127.0f;
   rdp.light[n].dir_y = (float)(((int8_t*)gfx.RDRAM)[(address+9)^3]) / 127.0f;
   rdp.light[n].dir_z = (float)(((int8_t*)gfx.RDRAM)[(address+10)^3]) / 127.0f;
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

static void gSPLightColor( uint32_t n, uint32_t w1)
{
   rdp.light[n].r = (float)((w1 >> 24) & 0xFF) / 255.0f;
   rdp.light[n].g = (float)((w1 >> 16) & 0xFF) / 255.0f;
   rdp.light[n].b = (float)((w1 >> 8) & 0xFF) / 255.0f;
   rdp.light[n].a = 255;
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
