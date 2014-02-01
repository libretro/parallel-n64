//forward decls
extern void LoadBlock32b(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt);
extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int tile_set;

static INLINE void loadTile(uint32_t *src, uint32_t *dst, int width, int height, int line, int off, uint32_t *end)
{
  uint32_t *v7, *v9, *v13, v16, *v17, v18, v20, *v24, *v27, *v31, nbits;
  int v8, v10, v11, v12, v14, v15, v19, v21, v23, v25, v26, v28, v29, v30;

  nbits = sizeof(uint32_t) * 8;
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
                 *dst    ^= dst[1];
                 dst[1] ^= *dst;
                 *dst    ^= dst[1];
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
     *dst    ^= dst[1];
     dst[1] ^= *dst;
     *dst    ^= dst[1];
     dst += 2;
     --v18;
  }
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

static void gDPSetTile( uint32_t format, uint32_t size, uint32_t line, uint32_t tmem,
      uint32_t palette, uint32_t cmt, uint32_t cms, uint32_t maskt, uint32_t masks, uint32_t shiftt, uint32_t shifts,
      uint32_t mirrort, uint32_t mirrors)
{
   uint32_t w1;
   int i;
   TILE *tile;

   w1 = rdp.cmd1;
   tile_set = 1; // used to check if we only load the first settilesize
   rdp.first = 0;
   rdp.last_tile = (uint32_t)((w1 >> 24) & 0x07);

   tile = (TILE*)&rdp.tiles[rdp.last_tile];
   tile->format   = format; 
   tile->size     = size;
   tile->line     = line;
   tile->t_mem    = tmem;
   tile->palette  = palette;
   tile->clamp_t  = cmt;
   tile->clamp_s  = cms;
   tile->mask_t   = maskt;
   tile->mask_s   = masks;
   tile->shift_t  = shiftt;
   tile->shift_s  = shifts;
   tile->mirror_t = mirrort;
   tile->mirror_s = mirrors;

   rdp.update |= UPDATE_TEXTURE;


#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled && rdp.last_tile < rdp.cur_tile + 2)
   {
      for (i = 0; i < 2; i++)
      {
         if (rdp.aTBuffTex[i])
         {
            if (rdp.aTBuffTex[i]->t_mem == tile->t_mem)
            {
               if (rdp.aTBuffTex[i]->size == tile->size)
               {
                  rdp.aTBuffTex[i]->tile = rdp.last_tile;
                  rdp.aTBuffTex[i]->info.format = tile->format == G_IM_FMT_RGBA ? GR_TEXFMT_RGB_565 : GR_TEXFMT_ALPHA_INTENSITY_88;
                  FRDP("rdp.aTBuffTex[%d] tile=%d, format=%s\n", i, rdp.last_tile, tile->format == 0 ? "RGB565" : "Alpha88");
               }
               else
                  rdp.aTBuffTex[i] = 0;
               break;
            }
            else if (rdp.aTBuffTex[i]->tile == rdp.last_tile) //wrong! t_mem must be the same
               rdp.aTBuffTex[i] = 0;
         }
      }
   }
#endif

   //FRDP ("settile: tile: %d, format: %s, size: %s, line: %d, ""t_mem: %08lx, palette: %d, clamp_t/mirror_t: %s, mask_t: %d, ""shift_t: %d, clamp_s/mirror_s: %s, mask_s: %d, shift_s: %d\n",rdp.last_tile, str_format[tile->format], str_size[tile->size], tile->line, tile->t_mem, tile->palette, str_cm[(tile->clamp_t<<1)|tile->mirror_t], tile->mask_t, tile->shift_t, str_cm[(tile->clamp_s<<1)|tile->mirror_s], tile->mask_s, tile->shift_s);
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

static void gDPSetFillColor(uint32_t c)
{
   rdp.fill_color = c;
   rdp.update |= UPDATE_ALPHA_COMPARE | UPDATE_COMBINE;

#ifdef EXTREME_LOGGING
   FRDP("setfillcolor: %08lx\n", c);
#endif
}

static void gDPSetFogColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
   uint32_t w1 = rdp.cmd1;
   rdp.fog_color = w1;
   rdp.update |= UPDATE_COMBINE | UPDATE_FOG_ENABLED;

#ifdef EXTREME_LOGGING
   FRDP("setfogcolor - %08lx\n", w1);
#endif
}

static void gDPSetBlendColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a )
{
   uint32_t w1 = rdp.cmd1;
   rdp.blend_color = w1;
   rdp.update |= UPDATE_COMBINE;

#ifdef EXTREME_LOGGING
   FRDP("setblendcolor: %08lx\n", w1);
#endif
}

static void gDPSetPrimColor( uint32_t m, uint32_t l, uint32_t r, uint32_t g, uint32_t b, uint32_t a )
{
   uint32_t w0 = rdp.cmd0;
   uint32_t w1 = rdp.cmd1;
   rdp.prim_color = w1;
   rdp.prim_lodmin = (w0 >> 8) & 0xFF;
   rdp.prim_lodfrac = max(w0 & 0xFF, rdp.prim_lodmin);
   rdp.update |= UPDATE_COMBINE;

#ifdef EXTREME_LOGGING
   FRDP("setprimcolor: %08lx, lodmin: %d, lodfrac: %d\n", w1, rdp.prim_lodmin,
         rdp.prim_lodfrac);
#endif
}

static void gDPSetDepthImage( uint32_t address )
{
   rdp.zimg = address;
   rdp.zi_width = rdp.ci_width;
#ifdef EXTREME_LOGGING
   FRDP("setdepthimage - %08lx\n", rdp.zimg);
#endif
}


static void gDPLoadBlock( uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt )
{
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
   /*  double fdxt = (double)0x8000000F/(double)((uint32_t)(2047/(dxt-1))); // F for error
       uint32_t _dxt = (uint32_t)fdxt;*/

   // 0x00000800 -> 0x80000000 (so we can check the sign bit instead of the 11th bit)
   uint32_t _dxt = dxt << 20;

   uint32_t addr = segoffset(rdp.timg.addr) & BMASK;

   rdp.tiles[tile].ul_s = ul_s;
   rdp.tiles[tile].ul_t = ul_t;
   rdp.tiles[tile].lr_s = lr_s;

   rdp.timg.set_by = 0;  // load block

   // do a quick boundary check before copying to eliminate the possibility for exception
   if (ul_s >= 512)
   {
      lr_s = 1;   // 1 so that it doesn't die on memcpy
      ul_s = 511;
   }
   if (ul_s+lr_s > 512)
      lr_s = 512-ul_s;

   if (addr+(lr_s<<3) > BMASK+1)
      lr_s = (uint16_t)((BMASK-addr)>>3);

   //angrylion's advice to use ul_s in texture image offset and cnt calculations.
   //Helps to fix Vigilante 8 jpeg backgrounds and logos
   uint32_t off = rdp.timg.addr + (ul_s << rdp.tiles[tile].size >> 1);
   uint8_t *dst = ((uint8_t*)rdp.tmem) + (rdp.tiles[tile].t_mem<<3);
   uint32_t cnt = lr_s-ul_s+1;
   if (rdp.tiles[tile].size == 3)
      cnt <<= 1;

   if (rdp.timg.size == 3)
      LoadBlock32b(tile, ul_s, ul_t, lr_s, dxt);
   else
      loadBlock((uint32_t *)gfx.RDRAM, (uint32_t *)dst, off, _dxt, cnt);

   rdp.timg.addr += cnt << 3;
   rdp.tiles[tile].lr_t = ul_t + ((dxt*cnt)>>11);

   rdp.update |= UPDATE_TEXTURE;

#ifdef EXTREME_LOGGING
   FRDP ("loadblock: tile: %d, ul_s: %d, ul_t: %d, lr_s: %d, dxt: %08lx -> %08lx\n",
         tile, ul_s, ul_t, lr_s,
         dxt, _dxt);
#endif

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
      setTBufTex(rdp.tiles[tile].t_mem, cnt);
#endif
}

static inline void gDPSetAlphaCompare( uint32_t mode )
{
   rdp.othermode_l |= 0x00000003;
   rdp.acmp = mode;
   rdp.update |= UPDATE_ALPHA_COMPARE;
   //FRDP ("alpha compare %s\n", ACmp[rdp.acmp]);
}

static inline void gDPSetDepthSource( uint32_t source )
{
   rdp.zsrc = source;
   rdp.update |= UPDATE_ZBUF_ENABLED;
   //FRDP ("z-src sel: %s\n", str_zs[rdp.zsrc]);
   //FRDP ("z-src sel: %08lx\n", rdp.zsrc);
}

static void gDPSetRenderMode( uint32_t mode1, uint32_t mode2 )
{
   rdp.othermode_l &= 0x00000007;
   rdp.othermode_l |= mode1;
   rdp.othermode_l |= mode2;
   rdp.update |= UPDATE_FOG_ENABLED; //if blender has no fog bits, fog must be set off
   rdp.render_mode_changed |= rdp.rm ^ rdp.othermode_l;
   rdp.rm = rdp.othermode_l;
   if (settings.flame_corona && (rdp.rm == 0x00504341)) //hack for flame's corona
      rdp.othermode_l |= UPDATE_BIASLEVEL | UPDATE_LIGHTS;
   //FRDP ("rendermode: %08lx\n", rdp.othermode_l);  // just output whole othermode_l
}

static void gDPFillRectangle( int32_t ul_x, int32_t ul_y, int32_t lr_x, int32_t lr_y )
{
   if ((ul_x > lr_x) || (ul_y > lr_y))
   {
#ifdef EXTREME_LOGGING
      LRDP("Fillrect. Wrong coordinates. Skipped\n");
#endif
      return;
   }
   int pd_multiplayer = (settings.ucode == ucode_PerfectDark) && (rdp.cycle_mode == G_CYC_FILL) && (rdp.fill_color == 0xFFFCFFFC);
   if ((rdp.cimg == rdp.zimg) || (fb_emulation_enabled && rdp.frame_buffers[rdp.ci_count-1].status == CI_ZIMG) || pd_multiplayer)
   {
      //LRDP("Fillrect - cleared the depth buffer\n");

      if (!(settings.hacks&hack_Hyperbike) || rdp.ci_width > 64) //do not clear main depth buffer for aux depth buffers
      {
         update_scissor ();
         grDepthMask (FXTRUE);
         grColorMask (FXFALSE, FXFALSE);
         grBufferClear (0, 0, rdp.fill_color ? rdp.fill_color&0xFFFF : 0xFFFF);
         grColorMask (FXTRUE, FXTRUE);
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
      //if (settings.frame_buffer&fb_depth_clear)
      {
         uint32_t y, x;
         ul_x = min(max(ul_x, rdp.scissor_o.ul_x), rdp.scissor_o.lr_x);
         lr_x = min(max(lr_x, rdp.scissor_o.ul_x), rdp.scissor_o.lr_x);
         ul_y = min(max(ul_y, rdp.scissor_o.ul_y), rdp.scissor_o.lr_y);
         lr_y = min(max(lr_y, rdp.scissor_o.ul_y), rdp.scissor_o.lr_y);
         uint32_t zi_width_in_dwords = rdp.ci_width >> 1;
         ul_x >>= 1;
         lr_x >>= 1;
         uint32_t * dst = (uint32_t*)(gfx.RDRAM+rdp.cimg);
         dst += ul_y * zi_width_in_dwords;
         for (y = ul_y; y < lr_y; y++)
         {
            for (x = ul_x; x < lr_x; x++)
               dst[x] = rdp.fill_color;
            dst += zi_width_in_dwords;
         }
      }
      return;
   }

   if (rdp.skip_drawing)
      return; //Fillrect skipped

   if (rdp.cur_image && (rdp.cur_image->format != G_IM_FMT_RGBA) && (rdp.cycle_mode == G_CYC_FILL) && (rdp.cur_image->width == lr_x - ul_x) && (rdp.cur_image->height == lr_y - ul_y))
   {
      uint32_t color = rdp.fill_color;
      if (rdp.ci_size < 3)
      {
         color = ((color&1)?0xFF:0) |
            ((uint32_t)(((color&0xF800) >> 11)) << 24) |
            ((uint32_t)(((color&0x07C0) >> 6)) << 16) |
            ((uint32_t)(((color&0x003E) >> 1)) << 8);
      }
      grDepthMask (FXFALSE);
      grBufferClear (color, 0, 0xFFFF);
      grDepthMask (FXTRUE);
      rdp.update |= UPDATE_ZBUF_ENABLED;
#ifdef EXTREME_LOGGING
      LRDP("Fillrect - cleared the texture buffer\n");
#endif
      return;
   }

   update_scissor();

   if (settings.decrease_fillrect_edge && rdp.cycle_mode == G_CYC_1CYCLE)
   {
      lr_x--;
      lr_y--;
   }
   //FRDP("fillrect (%d,%d) -> (%d,%d), cycle mode: %d, #%d, #%d\n", ul_x, ul_y, lr_x, lr_y, rdp.cycle_mode, rdp.tri_n, rdp.tri_n+1);
   //FRDP("scissor (%d,%d) -> (%d,%d)\n", rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);

   // KILL the floating point error with 0.01f
   int32_t s_ul_x = (uint32_t)min(max(ul_x * rdp.scale_x + rdp.offset_x + 0.01f, rdp.scissor.ul_x), rdp.scissor.lr_x);
   int32_t s_lr_x = (uint32_t)min(max(lr_x * rdp.scale_x + rdp.offset_x + 0.01f, rdp.scissor.ul_x), rdp.scissor.lr_x);
   int32_t s_ul_y = (uint32_t)min(max(ul_y * rdp.scale_y + rdp.offset_y + 0.01f, rdp.scissor.ul_y), rdp.scissor.lr_y);
   int32_t s_lr_y = (uint32_t)min(max(lr_y * rdp.scale_y + rdp.offset_y + 0.01f, rdp.scissor.ul_y), rdp.scissor.lr_y);

   if (s_lr_x < 0) s_lr_x = 0;
   if (s_lr_y < 0) s_lr_y = 0;
   if ((uint32_t)s_ul_x > settings.res_x) s_ul_x = settings.res_x;
   if ((uint32_t)s_ul_y > settings.res_y) s_ul_y = settings.res_y;

   //FRDP (" - %d, %d, %d, %d\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y);

   {
      grFogMode (GR_FOG_DISABLE);

      const float Z = (rdp.cycle_mode == G_CYC_FILL) ? 0.0f : set_sprite_combine_mode();

      // Draw the rectangle
      VERTEX v[4] = {
         { (float)s_ul_x, (float)s_ul_y, Z, 1.0f,  0,0,0,0,  {0,0,0,0}, 0,0, 0,0,0,0},
         { (float)s_lr_x, (float)s_ul_y, Z, 1.0f,  0,0,0,0,  {0,0,0,0}, 0,0, 0,0,0,0},
         { (float)s_ul_x, (float)s_lr_y, Z, 1.0f,  0,0,0,0,  {0,0,0,0}, 0,0, 0,0,0,0},
         { (float)s_lr_x, (float)s_lr_y, Z, 1.0f,  0,0,0,0,  {0,0,0,0}, 0,0, 0,0,0,0} };

      if (rdp.cycle_mode == G_CYC_FILL)
      {
         uint32_t color = rdp.fill_color;

         if ((settings.hacks&hack_PMario) && rdp.frame_buffers[rdp.ci_count-1].status == CI_AUX)
         {
            //background of auxiliary frame buffers must have zero alpha.
            //make it black, set 0 alpha to plack pixels on frame buffer read
            color = 0;
         }
         else if (rdp.ci_size < 3)
         {
            color = ((color&1)?0xFF:0) |
               ((uint32_t)((float)((color&0xF800) >> 11) / 31.0f * 255.0f) << 24) |
               ((uint32_t)((float)((color&0x07C0) >> 6) / 31.0f * 255.0f) << 16) |
               ((uint32_t)((float)((color&0x003E) >> 1) / 31.0f * 255.0f) << 8);
         }

         grConstantColorValue (color);

         grColorCombine (GR_COMBINE_FUNCTION_LOCAL,
               GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT,
               GR_COMBINE_OTHER_NONE,
               FXFALSE);

         grAlphaCombine (GR_COMBINE_FUNCTION_LOCAL,
               GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT,
               GR_COMBINE_OTHER_NONE,
               FXFALSE);

         grAlphaBlendFunction (GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

         grAlphaTestFunction (GR_CMP_ALWAYS);
         grStippleMode(GR_STIPPLE_DISABLE);

         grCullMode(GR_CULL_DISABLE);
         grFogMode (GR_FOG_DISABLE);
         grDepthBufferFunction (GR_CMP_ALWAYS);
         grDepthMask (FXFALSE);

         rdp.update |= UPDATE_COMBINE | UPDATE_CULL_MODE | UPDATE_FOG_ENABLED | UPDATE_ZBUF_ENABLED;
      }
      else
      {
         uint32_t cmb_mode_c = (rdp.cycle1 << 16) | (rdp.cycle2 & 0xFFFF);
         uint32_t cmb_mode_a = (rdp.cycle1 & 0x0FFF0000) | ((rdp.cycle2 >> 16) & 0x00000FFF);
         if (cmb_mode_c == 0x9fff9fff || cmb_mode_a == 0x09ff09ff) //shade
         {
            int k;
            AllowShadeMods (v, 4);
            for (k = 0; k < 4; k++)
               apply_shade_mods (&v[k]);
         }
         if ((rdp.othermode_l & 0x4000) && ((rdp.othermode_l >> 16) == 0x0550)) //special blender mode for Bomberman64
         {
            grAlphaCombine (GR_COMBINE_FUNCTION_LOCAL,
                  GR_COMBINE_FACTOR_NONE,
                  GR_COMBINE_LOCAL_CONSTANT,
                  GR_COMBINE_OTHER_NONE,
                  FXFALSE);
            grConstantColorValue((cmb.ccolor&0xFFFFFF00)|(rdp.fog_color&0xFF));
            rdp.update |= UPDATE_COMBINE;
         }
      }

      grDrawTriangle(&v[0], &v[2], &v[1]);
      grDrawTriangle(&v[2], &v[3], &v[1]);

      rdp.tri_n += 2;
   }
}

static void gDPSetEnvColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a )
{
   rdp.env_color = rdp.cmd1;
   rdp.update |= UPDATE_COMBINE;

   //FRDP("setenvcolor: %08lx\n", w1);
}

// Sets the source for an image copy
static void gDPSetTextureImage( uint32_t format, uint32_t size, uint32_t width, uint32_t address )
{
   //static const char *format[]   = { "RGBA", "YUV", "CI", "IA", "I", "?", "?", "?" };
   //static const char *size[]     = { "4bit", "8bit", "16bit", "32bit" };
   rdp.timg.format = format;
   rdp.timg.size   = size;
   rdp.timg.width  = width;
   rdp.timg.addr   = address;
 
   if (ucode5_texshiftaddr)
   {
      if (rdp.timg.format == G_IM_FMT_RGBA)
      {
         uint16_t * t = (uint16_t*)(gfx.RDRAM+ucode5_texshiftaddr);
         ucode5_texshift = t[ucode5_texshiftcount^1];
         rdp.timg.addr += ucode5_texshift;
      }
      else
      {
         ucode5_texshiftaddr = 0;
         ucode5_texshift = 0;
         ucode5_texshiftcount = 0;
      }
   }
   rdp.s2dex_tex_loaded = true;
   rdp.update |= UPDATE_TEXTURE;

   if (rdp.frame_buffers[rdp.ci_count-1].status == CI_COPY_SELF && (rdp.timg.addr >= rdp.cimg) && (rdp.timg.addr < rdp.ci_end))
   {
      if (!rdp.fb_drawn)
      {
         if (!rdp.cur_image)
            CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
         else
            CloseTextureBuffer(true);
         rdp.fb_drawn = true;
      }
   }

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled) //search this texture among drawn texture buffers
      FindTextureBuffer(rdp.timg.addr, rdp.timg.width);
#endif

   //FRDP("settextureimage: format: %s, size: %s, width: %d, addr: %08lx\n", format[rdp.timg.format], size[rdp.timg.size], rdp.timg.width, rdp.timg.addr);
}

static void gDPSetConvert(int32_t k0, int32_t k1, int32_t k2, int32_t k3, int32_t k4, int32_t k5)
{
   /*
      rdp.YUV_C0 = 1.1647f  ;
      rdp.YUV_C1 = 0.79931f ;
      rdp.YUV_C2 = -0.1964f ;
      rdp.YUV_C3 = -0.40651f;
      rdp.YUV_C4 = 1.014f   ;
      */
   rdp.K4 = k4;
   rdp.K5 = k5;
   //FRDP("setconvert. K4=%02lx K5=%02lx\n", rdp.K4, rdp.K5);
}
