//forward decls
extern void LoadBlock32b(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt);
extern void RestoreScale(void);

extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int CI_SET;
extern uint32_t swapped_addr;

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
