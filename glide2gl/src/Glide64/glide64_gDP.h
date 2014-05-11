//forward decls
extern void LoadBlock32b(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt);
extern void RestoreScale(void);

extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int CI_SET;
extern uint32_t swapped_addr;

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

/*
 * Sync Load will stall any subsequent texture load commands
 * until all preceding geometry has been rasterized. This
 * should be placed before a load texture command that is
 * overwriting atexture or TLUT used by up to two 
 * preceding primitives.
 */

static void gDPLoadSync(void)
{
   //LRDP("loadsync - ignored\n");
}

/*
 * Sync Pipe will stall the pipeline until any 
 * primitive on the pipeline is finished using
 * previously issud configuration commands. This
 * encompasses all stalls in Sync Load as well as
 * several other configuration commands. The Primitive
 * Color and Primitive Depth commands are exempt from
 * needing  Sync Pipe. 
 *
 * If one is careful to order RDP commands properly, this
 * can be left out. For example Set Texture Image does not affect
 * the rasterization of triangles or rectangles, so it can be
 * called immediately after a rectangle or triangle command
 * without the need of a Sync Pipe to separate.
 *
 * Note that calling this spuriously will cause rendering artifacts
 * such as the RDP rendering geometry to the wrong frame buffer. In
 * essence, do not call Sync Pipe when there is nothing that needs to be
 * waited on.
 */

static void gDPPipeSync(void)
{
   //LRDP("pipesync - ignored\n");
}

/*
 * Sync Tile will stall any tile load operations until any
 * preceding command finishes reading from texture memory (TMEM).
 * It is unclear how this differs from Sync Load.
 *
 */
static void gDPTileSync(void)
{
   //LRDP("tilesync - ignored\n");
}

/*
* Sets the tile descriptor parameters.
* Sets the paramaters of a tile descriptor defining the origin and range of a texture tile.
*
* ul, ult - Defines the s, t texture coordinates at the tile origin. This is used when
* shifting a texture to straddle the face of a polygon. It also defines the lower-limit
* boundary when clamping.
* lrs, lrt- Used when clamping to define the upper-limit boundary.
*/
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

   //FRDP ("settilesize: tile: %d, ul_s: %d, ul_t: %d, lr_s: %d, lr_t: %d, f_ul_s: %f, f_ul_t: %f\n", tile, ul_s, ul_t, lr_s, lr_t, rdp.tiles[tile].f_ul_s, rdp.tiles[tile].f_ul_t);
}

/*
 * Tells the RDP about a texture that is to be loaded out of a previously specified
 * Texture Image.
 *
 * format - Controls the output format of the rasterized image.
 *
 * - 000 - RGBA
 * - 001 - YUV
 * - 010 - Color Index (CI)
 * - 011 - Intensity Alpha (IA)
 * - 011 - Intensity (I)
 *
 * size - Size of an individual pixel in terms of bits
 * 
 * - 00 - 4 bits
 *   01 - 8 bits (Color Index)
 *   10 - 16 bits (RGBA)
 *   11 - 32 bits (RGBA)
 *
 * line - The width of the texture to be stored in terms of 64 bit words. 
 *
 * tmem - The location in texture memory (TMEM) to store the texture in
 * terms of 64 bit words. This implies that textures must be 8 byte
 * aligned in TMEM.
 *
 * tile_idx - The tile ID to give this texture.
 *
 * palette -For 4 bit sprites, this is the palette number. In effect, this is
 * the upper 4bits to make an 8 bit color index pixel which will then be looked up
 * in a palette.
 *
 * cmt - Enables clamping in the T direction when texturing primitives.
 *
 * cms - Enables clamping in the S direction when texturing primitives.
 *
 * maskt - Number of bits to mask or mirror in the T direction.
 *
 * masks - Number of bits to mask or mirror in the S direction.
 *
 * shiftt - Level of detail shift in the T direction.
 *
 * shifts - Level of detail shift in the S direction.
 *
 * mirrort - Enable mirroring in the T direction when texturing primitives.
 *
 * mirrors - Enable mirroring in the S direction when texturing primitives.
 *
 * FIXME NOTE: we're using unsigned 32 bit here - yet 64 bit words is being implied as
 * input operands
 */
static void gDPSetTile( uint32_t format, uint32_t size, uint32_t line, uint32_t tmem, uint32_t tile_idx,
      uint32_t palette, uint32_t cmt, uint32_t cms, uint32_t maskt, uint32_t masks, uint32_t shiftt, uint32_t shifts,
      uint32_t mirrort, uint32_t mirrors)
{
   int i;
   TILE *tile;

   tile_set = 1; // used to check if we only load the first settilesize
   rdp.first = 0;
   rdp.last_tile = tile_idx;

   tile = (TILE*)&rdp.tiles[tile_idx];
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

   if ((settings.hacks&hack_GoldenEye) && tile->format == G_IM_FMT_RGBA && rdp.tlut_mode == 2 && tile->size == G_IM_SIZ_16b)
   {
      /* Hack -GoldenEye water texture in Frigate stage. It has CI format in fact, but the game set it to RGBA */
      tile->format = G_IM_FMT_CI;
      tile->size = G_IM_SIZ_8b;
   }

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

   rdp.update |= UPDATE_TEXTURE;

   //FRDP ("settile: tile: %d, format: %s, size: %s, line: %d, ""t_mem: %08lx, palette: %d, clamp_t/mirror_t: %s, mask_t: %d, ""shift_t: %d, clamp_s/mirror_s: %s, mask_s: %d, shift_s: %d\n",rdp.last_tile, str_format[tile->format], str_size[tile->size], tile->line, tile->t_mem, tile->palette, str_cm[(tile->clamp_t<<1)|tile->mirror_t], tile->mask_t, tile->shift_t, str_cm[(tile->clamp_s<<1)|tile->mirror_s], tile->mask_s, tile->shift_s);
}

void LoadTile32b (uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t width, uint32_t height);

/*
 * Tells the RDP to pull actual texture data out of a texture set by
 * Set Texture Image and place it in TMEM in a texture slot defined by Set Tile.
 * The tile ID references a tile that has been set up by Set Tile.
 *
 * tile - The tile ID to load this texture into.
 *
 * lst - Low S coordinate of texture (10.5 fixed point format).
 *
 * lrt - Low T coordinate of texture (10.5 fixed point format).
 *
 * uls - High S coordinate of texture (10.5 fixed point format).
 *
 * ult - High T coordinate of texture (10.5 fixed point format).
 */

static void gDPLoadTile( uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt )
{
   uint32_t width, height, offs;
   int line_n;

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

#ifdef HAVE_HWFBE
   if (rdp.tbuff_tex)// && (rdp.tiles[tile].format == G_IM_FMT_RGBA))
   {
      rdp.tbuff_tex->tile_uls = uls;
      rdp.tbuff_tex->tile_ult = ult;
      //FRDP("loadtile: tbuff_tex uls: %d, ult:%d\n", uls, ult);
   }
#endif

   if ((settings.hacks&hack_Tonic) && tile == 7)
   {
      rdp.tiles[0].ul_s = uls;
      rdp.tiles[0].ul_t = ult;
      rdp.tiles[0].lr_s = lrs;
      rdp.tiles[0].lr_t = lrt;
   }

   height = lrt - ult + 1;   // get height
   width = lrs - uls + 1;

   line_n = rdp.timg.width << rdp.tiles[tile].size >> 1;
   offs = ult * line_n;
   offs += uls << rdp.tiles[tile].size >> 1;
   offs += rdp.timg.addr;
   if (offs >= BMASK)
      return;

   if (rdp.timg.size == 3)
      LoadTile32b(tile, uls, ult, width, height);
   else
   {
      uint32_t wid_64;
      uint8_t *dst, *end;
      // check if points to bad location
      if (offs + line_n*height > BMASK)
         height = (BMASK - offs) / line_n;
      if (height == 0)
         return;

      wid_64 = rdp.tiles[tile].line;
      dst = ((uint8_t*)rdp.tmem) + (rdp.tiles[tile].t_mem<<3);
      end = ((uint8_t*)rdp.tmem) + 4096 - (wid_64<<3);
      loadTile((uint32_t *)gfx.RDRAM, (uint32_t *)dst, wid_64, height, line_n, offs, (uint32_t *)end);
   }

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
      setTBufTex(rdp.tiles[tile].t_mem, rdp.tiles[tile].line*height);
#endif

   //FRDP("loadtile: tile: %d, uls: %d, ult: %d, lr_s: %d, lr_t: %d\n", tile, uls, ult, lrs, lrt);
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
   /*  double fdxt = (double)0x8000000F/(double)((uint32_t)(2047/(dxt-1))); // F for error
       uint32_t _dxt = (uint32_t)fdxt;*/

   // 0x00000800 -> 0x80000000 (so we can check the sign bit instead of the 11th bit)
   _dxt = dxt << 20;
   addr = segoffset(rdp.timg.addr) & BMASK;

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
   off = rdp.timg.addr + (ul_s << rdp.tiles[tile].size >> 1);
   dst = ((uint8_t*)rdp.tmem) + (rdp.tiles[tile].t_mem<<3);
   cnt = lr_s-ul_s+1;
   if (rdp.tiles[tile].size == 3)
      cnt <<= 1;

   if (rdp.timg.size == 3)
      LoadBlock32b(tile, ul_s, ul_t, lr_s, dxt);
   else
      loadBlock((uint32_t *)gfx.RDRAM, (uint32_t *)dst, off, _dxt, cnt);

   rdp.timg.addr += cnt << 3;
   rdp.tiles[tile].lr_t = ul_t + ((dxt*cnt)>>11);

   rdp.update |= UPDATE_TEXTURE;

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
      setTBufTex(rdp.tiles[tile].t_mem, cnt);
#endif
   //FRDP ("loadblock: tile: %d, ul_s: %d, ul_t: %d, lr_s: %d, dxt: %08lx -> %08lx\n", tile, ul_s, ul_t, lr_s, dxt, _dxt);
}

/*
* Sets the mode for comparing the alpha value of the pixel input
* to the blender (BL) with an alpha source.
*
* mode - alpha compare mode
* - G_AC_NONE - Do not compare
* - G_AC_THRESHOLD - Compare with the blend color alpha value
* - G_AC_DITHER - Compare with a random dither value
*/
#define gDPSetAlphaCompare(mode) \
{ \
   rdp.acmp = mode; \
   rdp.update |= UPDATE_ALPHA_COMPARE; \
}

/*
* Sets which depth source value to use for comparisons with
* the Z-buffer.
*
* source - depth source value.
* - G_ZS_PIXEL (Use the Z-value and 'deltaZ' value repeated for each pixel)
* - G_ZS_PRIM (Use the primitive depth register's Z value and 'delta Z' value)
*/
#define gDPSetDepthSource(source) \
{ \
   rdp.zsrc = source; \
   rdp.update |= UPDATE_ZBUF_ENABLED; \
}

/*
* Sets the rendering mode of the blender (BL). The BL can
* render a variety of Z-buffered, anti-aliased primitives.
*
* mode1 - the rendering mode set for first-cycle:
* - G_RM_ [ AA_ | RA_ ] [ ZB_ ]* (Rendering type)
* - G_RM_FOG_SHADE_A (Fog type)
* - G_RM_FOG_PRIM_A (Fog type)
* - G_RM_PASS (Pass type)
* - G_RM_NOOP (No operation type)
* mode2 - the rendering mode set for second cycle:
* - G_RM_ [ AA_ | RA_ ] [ ZB_ ]*2 (Rendering type)
* - G_RM_NOOP2 (No operation type)
*/
static void gDPSetRenderMode( uint32_t mode1, uint32_t mode2 )
{
   (void)mode1;
   (void)mode2;
   rdp.update |= UPDATE_FOG_ENABLED; //if blender has no fog bits, fog must be set off
   rdp.render_mode_changed |= rdp.rm ^ rdp.othermode_l;
   rdp.rm = rdp.othermode_l;
   if (settings.flame_corona && (rdp.rm == 0x00504341)) //hack for flame's corona
      rdp.othermode_l |= UPDATE_BIASLEVEL | UPDATE_LIGHTS;
   //FRDP ("rendermode: %08lx\n", rdp.othermode_l);  // just output whole othermode_l
}

/*
 * Draws a rectangle to the frame buffer using the color specified in the Set Fill Color
 * command. Before using the Fill Rectangle command, the RDP must be set up in fill mode
 * using the Cycle Type parameter in the Set Other Modes command.
 *
 * lr_x - Low X coordinate of rectangle (10.2 fixed point format)
 *
 * lr_y - Low Y coordinate of rectangle (10.2 fixed point format)
 *
 * ul_x - High X coordinate of rectangle (10.2 fixed point format)
 *
 * ul_y - High Y coordinate of rectangle (10.2 fixed point format)
 *
 */
static void gDPFillRectangle( int32_t ul_x, int32_t ul_y, int32_t lr_x, int32_t lr_y )
{
   int pd_multiplayer;
   int32_t s_ul_x, s_lr_x, s_ul_y, s_lr_y;

   if ((ul_x > lr_x) || (ul_y > lr_y)) // Wrong coordinates, skip
      return;

   pd_multiplayer = (settings.ucode == ucode_PerfectDark) 
      && (rdp.cycle_mode == G_CYC_FILL) && (rdp.fill_color == 0xFFFCFFFC);

   if ((rdp.cimg == rdp.zimg) || (fb_emulation_enabled && rdp.frame_buffers[rdp.ci_count-1].status == CI_ZIMG) || pd_multiplayer)
   {
      //LRDP("Fillrect - cleared the depth buffer\n");
      //Examples of places where this is used -
      //Zelda OOT - flame coronas, lens flare from the sun
      //http://www.emutalk.net/threads/15818-How-to-implement-quot-emulate-clear-quot-Answer-and-Question

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
         uint32_t y, x, zi_width_in_dwords, *dst;
         ul_x = min(max(ul_x, rdp.scissor_o.ul_x), rdp.scissor_o.lr_x);
         lr_x = min(max(lr_x, rdp.scissor_o.ul_x), rdp.scissor_o.lr_x);
         ul_y = min(max(ul_y, rdp.scissor_o.ul_y), rdp.scissor_o.lr_y);
         lr_y = min(max(lr_y, rdp.scissor_o.ul_y), rdp.scissor_o.lr_y);
         zi_width_in_dwords = rdp.ci_width >> 1;
         ul_x >>= 1;
         lr_x >>= 1;
         dst = (uint32_t*)(gfx.RDRAM+rdp.cimg);
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
      return;

#ifdef HAVE_HWFBE
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
      //LRDP("Fillrect - cleared the texture buffer\n");
      return;
   }
#endif

   update_scissor();

   if (settings.decrease_fillrect_edge && rdp.cycle_mode == G_CYC_1CYCLE)
   {
      lr_x--;
      lr_y--;
   }
   //FRDP("fillrect (%d,%d) -> (%d,%d), cycle mode: %d, #%d, #%d\n", ul_x, ul_y, lr_x, lr_y, rdp.cycle_mode, rdp.tri_n, rdp.tri_n+1);
   //FRDP("scissor (%d,%d) -> (%d,%d)\n", rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);

   // KILL the floating point error with 0.01f
   s_ul_x = (uint32_t)min(max(ul_x * rdp.scale_x + rdp.offset_x + 0.01f, rdp.scissor.ul_x), rdp.scissor.lr_x);
   s_lr_x = (uint32_t)min(max(lr_x * rdp.scale_x + rdp.offset_x + 0.01f, rdp.scissor.ul_x), rdp.scissor.lr_x);
   s_ul_y = (uint32_t)min(max(ul_y * rdp.scale_y + rdp.offset_y + 0.01f, rdp.scissor.ul_y), rdp.scissor.lr_y);
   s_lr_y = (uint32_t)min(max(lr_y * rdp.scale_y + rdp.offset_y + 0.01f, rdp.scissor.ul_y), rdp.scissor.lr_y);

   if (s_lr_x < 0)
      s_lr_x = 0;
   if (s_lr_y < 0)
      s_lr_y = 0;
   if ((uint32_t)s_ul_x > settings.res_x)
      s_ul_x = settings.res_x;
   if ((uint32_t)s_ul_y > settings.res_y)
      s_ul_y = settings.res_y;

   //FRDP (" - %d, %d, %d, %d\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y);

   {
      float Z;
      VERTEX v[4];

      grFogMode(GR_FOG_DISABLE);

      Z = (rdp.cycle_mode == G_CYC_FILL) ? 0.0f : set_sprite_combine_mode();

      // Make the vertices
      v[0].x  = s_ul_x;
      v[0].y  = s_ul_y;
      v[0].z  = Z;
      v[0].q  = 1;
      v[0].u0 = 0.0f;
      v[0].v0 = 0.0f;
      v[0].u1 = 0.0f;
      v[0].v1 = 0.0f;
      v[0].coord[0] = 0.0f;
      v[0].coord[1] = 0.0f;
      v[0].coord[2] = 0.0f;
      v[0].coord[3] = 0.0f;
      v[0].r  = 0;
      v[0].g  = 0;
      v[0].b  = 0;
      v[0].a  = 0;
      v[0].f  = 0.0f;
      v[0].vec[0] = 0.0f;
      v[0].vec[1] = 0.0f;
      v[0].vec[2] = 0.0f;
      v[0].vec[3] = 0.0f;
      v[0].sx = 0.0f;
      v[0].sy = 0.0f;
      v[0].sz = 0.0f;
      v[0].x_w = 0.0f;
      v[0].y_w = 0.0f;
      v[0].z_w = 0.0f;
      v[0].u0_w = 0.0f;
      v[0].v0_w = 0.0f;
      v[0].u1_w = 0.0f;
      v[0].v1_w = 0.0f;
      v[0].oow = 0.0f;
      v[0].not_zclipped = 0;
      v[0].screen_translated = 0;
      v[0].uv_scaled = 0;
      v[0].uv_calculated = 0;
      v[0].shade_mod = 0;
      v[0].color_backup = 0;
      v[0].ou = 0.0f;
      v[0].ov = 0.0f;
      v[0].number = 0;
      v[0].scr_off = 0;
      v[0].z_off = 0.0f;

      v[1].x  = s_lr_x;
      v[1].y  = s_ul_y;
      v[1].z  = Z;
      v[1].q  = 1;
      v[1].u0 = 0.0f;
      v[1].v0 = 0.0f;
      v[1].u1 = 0.0f;
      v[1].v1 = 0.0f;
      v[1].coord[0] = 0.0f;
      v[1].coord[1] = 0.0f;
      v[1].coord[2] = 0.0f;
      v[1].coord[3] = 0.0f;
      v[1].r  = 0;
      v[1].g  = 0;
      v[1].b  = 0;
      v[1].a  = 0;
      v[1].f  = 0.0f;
      v[1].vec[0] = 0.0f;
      v[1].vec[1] = 0.0f;
      v[1].vec[2] = 0.0f;
      v[1].sx = 0.0f;
      v[1].sy = 0.0f;
      v[1].sz = 0.0f;
      v[1].x_w = 0.0f;
      v[1].y_w = 0.0f;
      v[1].z_w = 0.0f;
      v[1].u0_w = 0.0f;
      v[1].v0_w = 0.0f;
      v[1].u1_w = 0.0f;
      v[1].v1_w = 0.0f;
      v[1].oow = 0.0f;
      v[1].not_zclipped = 0;
      v[1].screen_translated = 0;
      v[1].uv_scaled = 0;
      v[1].uv_calculated = 0;
      v[1].shade_mod = 0;
      v[1].color_backup = 0;
      v[1].ou = 0.0f;
      v[1].ov = 0.0f;
      v[1].number = 0;
      v[1].scr_off = 0;
      v[1].z_off = 0.0f;

      v[2].x  = s_ul_x;
      v[2].y  = s_lr_y;
      v[2].z  = Z;
      v[2].q  = 1;
      v[2].u0 = 0.0f;
      v[2].v0 = 0.0f;
      v[2].u1 = 0.0f;
      v[2].v1 = 0.0f;
      v[2].coord[0] = 0.0f;
      v[2].coord[1] = 0.0f;
      v[2].coord[2] = 0.0f; 
      v[2].coord[3] = 0.0f;
      v[2].r  = 0;
      v[2].g  = 0;
      v[2].b  = 0;
      v[2].a  = 0;
      v[2].f  = 0.0f;
      v[2].vec[0] = 0.0f;
      v[2].vec[1] = 0.0f;
      v[2].vec[2] = 0.0f;
      v[2].sx = 0.0f;
      v[2].sy = 0.0f;
      v[2].sz = 0.0f;
      v[2].x_w = 0.0f;
      v[2].y_w = 0.0f;
      v[2].z_w = 0.0f;
      v[2].u0_w = 0.0f;
      v[2].v0_w = 0.0f;
      v[2].u1_w = 0.0f;
      v[2].v1_w = 0.0f;
      v[2].oow = 0.0f;
      v[2].not_zclipped = 0;
      v[2].screen_translated = 0;
      v[2].uv_scaled = 0;
      v[2].uv_calculated = 0;
      v[2].shade_mod = 0;
      v[2].color_backup = 0;
      v[2].ou = 0.0f;
      v[2].ov = 0.0f;
      v[2].number = 0;
      v[2].scr_off = 0;
      v[2].z_off = 0.0f;

      v[3].x  = s_lr_x;
      v[3].y  = s_lr_y;
      v[3].z  = Z;
      v[3].q  = 1;
      v[3].u0 = 0.0;
      v[3].v0 = 0.0; 
      v[3].u1 = 0.0;
      v[3].v1 = 0.0;
      v[3].coord[0] = 0.0;
      v[3].coord[1] = 0.0;
      v[3].coord[2] = 0.0; 
      v[3].coord[3] = 0.0;
      v[3].r  = 0;
      v[3].g  = 0;
      v[3].b  = 0;
      v[3].a  = 0;
      v[3].f  = 0.0f;
      v[3].vec[0] = 0.0f;
      v[3].vec[1] = 0.0f;
      v[3].vec[2] = 0.0f;
      v[3].sx = 0.0f;
      v[3].sy = 0.0f;
      v[3].sz = 0.0f;
      v[3].x_w = 0.0f;
      v[3].y_w = 0.0f;
      v[3].z_w = 0.0f;
      v[3].u0_w = 0.0f;
      v[3].v0_w = 0.0f;
      v[3].u1_w = 0.0f;
      v[3].v1_w = 0.0f;
      v[3].oow = 0.0f;
      v[3].not_zclipped = 0;
      v[3].screen_translated = 0;
      v[3].uv_scaled = 0;
      v[3].uv_calculated = 0;
      v[3].shade_mod = 0;
      v[3].color_backup = 0;
      v[3].ou = 0.0f;
      v[3].ov = 0.0f;
      v[3].number = 0;
      v[3].scr_off = 0;
      v[3].z_off = 0.0f;

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
         grFogMode(GR_FOG_DISABLE);
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
         if ((rdp.othermode_l & FORCE_BL) && ((rdp.othermode_l >> 16) == 0x0550)) //special blender mode for Bomberman64
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

/* Sets the location of the current texture buffer in memory where
 * subsequent LoadTile commands source their data.
 *
 * format - Controls the input format of the texture buffer
 * in RDRAM.
 *
 * - 000 - RGBA
 *   001 - YUV
 *   010 - Color Index (CI)
 *   011 - Intensity Alpha (IA)
 *   011 - Intensity (I)
 *
 * size - Size of an individual pixel in terms of bits.
 *
 * - 00 (4 bits, Color index, IA, I)
 * - 01 (8 bits, Color Index, IA, I)
 * - 10 (16 bits, RGBA, YUV, IA)
 * - 11 (32 bits, RGBA)
 *
 * width - Width of texture buffer in memory minus one. This is
 * provided merely for convenience, so individual textures can be
 * easily copied out of a composite image map.
 *
 * address - The address in memory where the texture buffer resides.
 */
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
         uint16_t *t = (uint16_t*)(gfx.RDRAM+ucode5_texshiftaddr);
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
#ifdef HAVE_HWFBE
         if (!rdp.cur_image)
            CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
         else
            CloseTextureBuffer(true);
#else
         CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
#endif
         rdp.fb_drawn = true;
      }
   }

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled) //search this texture among drawn texture buffers
      FindTextureBuffer(rdp.timg.addr, rdp.timg.width);
#endif

   //FRDP("settextureimage: format: %s, size: %s, width: %d, addr: %08lx\n", format[rdp.timg.format], size[rdp.timg.size], rdp.timg.width, rdp.timg.addr);
}
