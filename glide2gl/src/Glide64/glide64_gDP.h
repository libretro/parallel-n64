static void gDPFullSync(void)
{
   // Set an interrupt to allow the game to continue
   *gfx.MI_INTR_REG |= 0x20;
   gfx.CheckInterrupts();

#ifdef EXTREME_LOGGING
   LRDP("gDPFullSync\n");
#endif
}

static void gDPSetKeyR(uint32_t cR, uint32_t sR, uint32_t wR)
{
   rdp.SCALE = (rdp.SCALE & 0x00FFFFFF) | (sR << 24);
   rdp.CENTER = (rdp.CENTER & 0x00FFFFFF) | (cR << 24);

#ifdef EXTREME_LOGGING
   FRDP("setkeyr. cR=%02lx, sR=%02lx\n", cR, sR);
#endif
}

static void gDPSetKeyGB(uint32_t cG, uint32_t sG, uint32_t wG, uint32_t cB, uint32_t sB, uint32_t wB )
{
   rdp.SCALE = (rdp.SCALE & 0xFF0000FF) | (sG << 16) | (sB << 8);
   rdp.CENTER = (rdp.CENTER & 0xFF0000FF) | (cG << 16) | (cB << 8);

#ifdef EXTREME_LOGGING
   FRDP("setkeygb. cG=%02lx, sG=%02lx, cB=%02lx, sB=%02lx\n", cG, sG, cB, sB);
#endif
}

static void gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry )
{
   (void)mode;
   // clipper resolution is 320x240, scale based on computer resolution
   rdp.scissor_o.ul_x = ulx;
   rdp.scissor_o.ul_y = uly;
   rdp.scissor_o.lr_x = lrx;
   rdp.scissor_o.lr_y = lry;

   rdp.ci_upper_bound = rdp.scissor_o.ul_y;
   rdp.ci_lower_bound = rdp.scissor_o.lr_y;
   rdp.scissor_set = true;

#ifdef EXTREME_LOGGING
   FRDP("setscissor: (%d,%d) -> (%d,%d)\n", rdp.scissor_o.ul_x, rdp.scissor_o.ul_y,
         rdp.scissor_o.lr_x, rdp.scissor_o.lr_y);
#endif

   rdp.update |= UPDATE_SCISSOR;

   if (rdp.view_scale[0] != 0) //viewport is set?
      return;

   rdp.view_scale[0] = (rdp.scissor_o.lr_x>>1)*rdp.scale_x;
   rdp.view_scale[1] = (rdp.scissor_o.lr_y>>1)*-rdp.scale_y;
   rdp.view_trans[0] = rdp.view_scale[0];
   rdp.view_trans[1] = -rdp.view_scale[1];
   rdp.update |= UPDATE_VIEWPORT;
}

static void gDPSetPrimDepth( uint16_t z, uint16_t dz )
{
   uint32_t w1 = rdp.cmd1;
   rdp.prim_depth = (uint16_t)((w1 >> 16) & 0x7FFF);
   rdp.prim_dz = (uint16_t)(w1 & 0x7FFF);

#ifdef EXTREME_LOGGING
   FRDP("setprimdepth: %d\n", rdp.prim_depth);
#endif
}
