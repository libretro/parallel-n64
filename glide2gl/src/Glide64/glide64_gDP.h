static INLINE void loadTile(uint32_t *src, uint32_t *dst, int width, int height, int line, int off, uint32_t *end)
{
  uint32_t *v7, *v9, *v13, v16, *v17, v18, v20, *v24, *v27, *v31;
  int v8, v10, v11, v12, v14, v15, v19, v21, v23, v25, v26, v28, v29, v30;
  unsigned int nbits;

  nbits = sizeof(unsigned int) * 8;
  v7 = dst;
  v8 = width;
  v9 = src;
  v10 = off;
  v11 = 0;
  v12 = height;
  do
  {
    if ( end < v7 )
      break;
    v31 = v7;
    v30 = v8;
    v29 = v12;
    v28 = v11;
    v27 = v9;
    v26 = v10;
    if ( v8 )
    {
      v25 = v8;
      v24 = v9;
      v23 = v10;
      v13 = (uint32_t *)((int8_t*)v9 + (v10 & 0xFFFFFFFC));
      v14 = v10 & 3;
      if ( !(v10 & 3) )
        goto LABEL_20;
      v15 = 4 - v14;
      v16 = *v13;
      v17 = v13 + 1;
      do
      {
        v16 = __ROL__(v16, 8, nbits);
      }while (--v14 );
      do
      {
        v16 = __ROL__(v16, 8, nbits);
        *(uint8_t *)v7 = v16;
        v7 = (uint32_t *)((int8_t*)v7 + 1);
      }while(--v15 );
      v18 = *v17;
      v13 = v17 + 1;
      *v7++ = bswap32(v18);
      if (--v8)
      {
LABEL_20:
        do
        {
          *v7++ = bswap32(*v13++);
          *v7++ = bswap32(*v13++);
        }while (--v8);
      }
      v19 = v23 & 3;
      if ( v23 & 3 )
      {
        v20 = *(uint32_t *)((int8_t*)v24 + ((8 * v25 + v23) & 0xFFFFFFFC));
        do
        {
          v20 = __ROL__(v20, 8, nbits);
          *(uint8_t *)v7 = v20;
          v7 = (uint32_t *)((int8_t*)v7 + 1);
        }
        while (--v19);
      }
    }
    v9 = v27;
    v21 = v29;
    v8 = v30;
    v11 = v28 ^ 1;
    if ( v28 == 1 )
    {
      v7 = v31;
      if ( v30 )
      {
        do
        {
           *v7    ^= v7[1];
           v7[1] ^= *v7;
           *v7    ^= v7[1];
           v7 += 2;
        }while (--v8);
      }
      v21 = v29;
      v8 = v30;
    }
    v10 = line + v26;
    v12 = v21 - 1;
  }while ( v12 );
}

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

static void gDPSetTileSize(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t lr_t )
{
   uint32_t w0 = rdp.cmd0;
   rdp.last_tile_size = tile;

   rdp.tiles[tile].f_ul_s = (float)((w0 >> 12) & 0xFFF) / 4.0f;
   rdp.tiles[tile].f_ul_t = (float)(w0 & 0xFFF) / 4.0f;

   if (lr_s == 0 && ul_s == 0)  //pokemon puzzle league set such tile size
      wrong_tile = tile;
   else if (wrong_tile == (int)tile)
      wrong_tile = -1;

   // coords in 10.2 format
   rdp.tiles[tile].ul_s = ul_s;
   rdp.tiles[tile].ul_t = ul_t;
   rdp.tiles[tile].lr_s = lr_s;
   rdp.tiles[tile].lr_t = lr_t;

   // handle wrapping
   if (rdp.tiles[tile].lr_s < rdp.tiles[tile].ul_s) rdp.tiles[tile].lr_s += 0x400;
   if (rdp.tiles[tile].lr_t < rdp.tiles[tile].ul_t) rdp.tiles[tile].lr_t += 0x400;

   rdp.update |= UPDATE_TEXTURE;

   rdp.first = 1;

#ifdef EXTREME_LOGGING
   FRDP ("settilesize: tile: %d, ul_s: %d, ul_t: %d, lr_s: %d, lr_t: %d, f_ul_s: %f, f_ul_t: %f\n",
         tile, ul_s, ul_t, lr_s, lr_t, rdp.tiles[tile].f_ul_s, rdp.tiles[tile].f_ul_t);
#endif
}

void LoadTile32b (uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t width, uint32_t height);

static void gDPLoadTile( uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt )
{
   if (rdp.skip_drawing)
      return;

   rdp.timg.set_by = 1;  // load tile

   rdp.addr[rdp.tiles[tile].t_mem] = rdp.timg.addr;

   if (lrs < uls || lrt < ult)
      return;

   if (wrong_tile >= 0)  //there was a tile with zero length
   {
      rdp.tiles[wrong_tile].lr_s = lrs;

      if (rdp.tiles[tile].size > rdp.tiles[wrong_tile].size)
         rdp.tiles[wrong_tile].lr_s <<= (rdp.tiles[tile].size - rdp.tiles[wrong_tile].size);
      else if (rdp.tiles[tile].size < rdp.tiles[wrong_tile].size)
         rdp.tiles[wrong_tile].lr_s >>= (rdp.tiles[wrong_tile].size - rdp.tiles[tile].size);
      rdp.tiles[wrong_tile].lr_t = lrt;
      rdp.tiles[wrong_tile].mask_s = rdp.tiles[wrong_tile].mask_t = 0;
      //     wrong_tile = -1;
   }

   if (rdp.tbuff_tex)// && (rdp.tiles[tile].format == G_IM_FMT_RGBA))
   {
#ifdef EXTREME_LOGGING
      FRDP("loadtile: tbuff_tex uls: %d, ult:%d\n", uls, ult);
#endif
      rdp.tbuff_tex->tile_uls = uls;
      rdp.tbuff_tex->tile_ult = ult;
   }

   if ((settings.hacks&hack_Tonic) && tile == 7)
   {
      rdp.tiles[0].ul_s = uls;
      rdp.tiles[0].ul_t = ult;
      rdp.tiles[0].lr_s = lrs;
      rdp.tiles[0].lr_t = lrt;
   }

   uint32_t height = lrt - ult + 1;   // get height
   uint32_t width = lrs - uls + 1;

   int line_n = rdp.timg.width << rdp.tiles[tile].size >> 1;
   uint32_t offs = ult * line_n;
   offs += uls << rdp.tiles[tile].size >> 1;
   offs += rdp.timg.addr;
   if (offs >= BMASK)
      return;

   if (rdp.timg.size == 3)
      LoadTile32b(tile, uls, ult, width, height);
   else
   {
      // check if points to bad location
      if (offs + line_n*height > BMASK)
         height = (BMASK - offs) / line_n;
      if (height == 0)
         return;

      uint32_t wid_64 = rdp.tiles[tile].line;
      uint8_t *dst = ((uint8_t*)rdp.tmem) + (rdp.tiles[tile].t_mem<<3);
      uint8_t *end = ((uint8_t*)rdp.tmem) + 4096 - (wid_64<<3);
      loadTile((uint32_t *)gfx.RDRAM, (uint32_t *)dst, wid_64, height, line_n, offs, (uint32_t *)end);
   }
#ifdef EXTREME_LOGGING
   FRDP("loadtile: tile: %d, uls: %d, ult: %d, lr_s: %d, lr_t: %d\n", tile,
         uls, ult, lrs, lrt);
#endif

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
      setTBufTex(rdp.tiles[tile].t_mem, rdp.tiles[tile].line*height);
#endif
}
