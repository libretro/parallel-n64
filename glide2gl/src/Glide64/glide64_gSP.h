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

static void gSPViewport(uint32_t v)
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

   // there are other values than x and y, but I don't know what they do

   rdp.update |= UPDATE_VIEWPORT;
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

   address = rdp.pc[rdp.pc_i] & BMASK;
   rdp.pc[rdp.pc_i] = (address + 24) & BMASK; //skip next 3 command, b/c they all are part of gSPForceMatrix

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
