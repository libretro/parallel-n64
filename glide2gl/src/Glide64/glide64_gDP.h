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
 * Will stall the pipeline until the last command that reads or writes
 * from the frame buffer finishes. This is useful for ensuring an entire
 * scene is fully drawn before swapping frame buffers or reusing a frame
 * buffer as a texture image.
 *
 */
static void gDPFullSync(void)
{
   // Set an interrupt to allow the game to continue
   *gfx.MI_INTR_REG |= 0x20;
   gfx.CheckInterrupts();

#ifdef EXTREME_LOGGING
   LRDP("gDPFullSync\n");
#endif
}

/*
* Selects the color for chroma key operations. Conceptually,
* the equation used for keying is:
*
* key = clamp(0.0, -abs((X - center) * scale) + width, 1.0)
*
* The alpha key is the minimum of KeyR, KeyG, or KeyB.
*
* cR - color center defines the intensity at which key is active (0-255).
* sR - color scale is (1.0/(size of soft edge))*255. The scale is in the
* range 0-255. For hard-edge keying, set scale to 255.
* wR - color width is (Size of half the key window including the soft edge
* )* scale. The width is expressed in a 12-bit 4.8 format. If the width
* is greater than 1.0, keying is disabled for that channel.
*/
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

   //FRDP("setkeygb. cG=%02lx, sG=%02lx, cB=%02lx, sB=%02lx\n", cG, sG, cB, sB);
}

/* Tells the RDP to rasterize geometry falling inside the scissor
 * box. Coordinates are given with respect to screen space.
 *
 * ulx - X coordinate of the top left corner of the scissor box. 
 *
 * uly - Y coordinate of the top left corner of the scissor box
 *
 * lrx - X coordinate of the bottom right corner of the scissor box.
 *
 * lry - Y coordinate of the bottom right corner of the scissor box.
 */
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

/*
* Sets the primitive depth (Z) in the RDP.
* Sets the Z-value (as well as the deltaZ value) to be used for the
* entire primitive RDP. The Z format used in the blender (B) is
* 15.3 fixed point. The primitive Z is a 16-bit register.
*
* z  - Z-value (16-bit precision, s15).
* dz - deltaZ value (16-bit precision, s15).
*/
static void gDPSetPrimDepth( uint16_t z, uint16_t dz )
{
   rdp.prim_depth = z;
   rdp.prim_dz = dz;

   //FRDP("setprimdepth: %d\n", rdp.prim_depth);
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

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
      setTBufTex(rdp.tiles[tile].t_mem, rdp.tiles[tile].line*height);
#endif

   //FRDP("loadtile: tile: %d, uls: %d, ult: %d, lr_s: %d, lr_t: %d\n", tile, uls, ult, lrs, lrt);
}

/*
 * Tells the RDP what color to use when rasterizing a Fill Rectangle
 * command. For 32 bit color mode, this is the RGBA value to set each
 * pixel to.
 *
 * For 16bit color mode, this is two 16 bit colors packed into one 32bit field.
 *
 * c - Color to fill non-textured rectangles with.
 */
static void gDPSetFillColor(uint32_t c)
{
   rdp.fill_color = c;
   rdp.update |= UPDATE_ALPHA_COMPARE | UPDATE_COMBINE;

   //FRDP("setfillcolor: %08lx\n", c);
}

/*
* Sets the RDP's fog color. The fog color is a general-use color register
* in the blender (BL).
*
* r - red component of RGBA color (8-bit precision, 0~255).
* g - green component of RGBA color (8-bit precision, 0~255).
* b - blue component of RGBA color (8-bit precision, 0~255).
* a - alpha component of RGBA color (8-bit precision, 0~255).
*/
static void gDPSetFogColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
   uint32_t w1 = rdp.cmd1;
   rdp.fog_color = w1;
   rdp.update |= UPDATE_COMBINE | UPDATE_FOG_ENABLED;

   //FRDP("setfogcolor - %08lx\n", w1);
}

/*
* Sets the RDP's blend color. The blend color register is a general-use
* color register in the blender (BL). This blend color register can, for
* example, set he alpah component to give a constant transparency to an
* object when fog is being used.
*
* r - red component of RGBA color (8-bit precision, 0~255).
* g - green component of RGBA color (8-bit precision, 0~255).
* b - blue component of RGBA color (8-bit precision, 0~255).
* a - alpha component of RGBA color (8-bit precision, 0~255).
*/
static void gDPSetBlendColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a )
{
   uint32_t w1 = rdp.cmd1;
   rdp.blend_color = w1;
   rdp.update |= UPDATE_COMBINE;

   //FRDP("setblendcolor: %08lx\n", w1);
}

/*
* Sets the RDP's primitive color, which is used as an input
* sourcxe by the color combiner (CC). The primitive color
* register set by this macro becomes a general-purpose color
* of the CC, expressing the constant flat-shaded surface color
* applied by the primitive.
*
* It is also used for rendering specular highlights. The contents
* of the primitive color register determine the highlight color.
*
* In 2-cycle mode, they indicate the color of the first specular
* highlight (Hilite1). Furthermore, 'm' and 'l' are used for
* trilinear interpolation in the MIP-mapping process.
*
*
* m - minimum value when LOD is less than 1.0 (.8, 0~255 texel/pixel ratios).
* l - LOD factor for interpolation of third axis (.8, 0~255).
* r - red component of RGBA color (8-bit precision, 0~255).
* g - green component of RGBA color (8-bit precision, 0~255).
* b - blue component of RGBA color (8-bit precision, 0~255).
* a - alpha component of RGBA color (8-bit precision, 0~255).
*/
static void gDPSetPrimColor( uint32_t m, uint32_t l, uint32_t r, uint32_t g, uint32_t b, uint32_t a )
{
   uint32_t w0 = rdp.cmd0;
   uint32_t w1 = rdp.cmd1;
   rdp.prim_color = w1;
   rdp.prim_lodmin = (w0 >> 8) & 0xFF;
   rdp.prim_lodfrac = max(w0 & 0xFF, rdp.prim_lodmin);
   rdp.update |= UPDATE_COMBINE;

   //FRDP("setprimcolor: %08lx, lodmin: %d, lodfrac: %d\n", w1, rdp.prim_lodmin, rdp.prim_lodfrac);
}

/*
* Sets the framebuffer area to be used as the Z-buffer.
*
* This macro must be used if Z buffering is enabled. Image width
* (width) and pixel size (siz) do not need to be set when the
* Z buffer area is secured, because the value set by gDPSetColorImage
* is used for 'width' and the 'siz' is always G_IM_SIZ_16b (16 bits per cycle).
*
* address - address of the image (64-byte alignment)
*/
static void gDPSetDepthImage( uint32_t address )
{
   rdp.zimg = address;
   rdp.zi_width = rdp.ci_width;

   //FRDP("setdepthimage - %08lx\n", rdp.zimg);
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
static INLINE void gDPSetAlphaCompare( uint32_t mode )
{
   rdp.acmp = mode;
   rdp.update |= UPDATE_ALPHA_COMPARE;
   //FRDP ("alpha compare %s\n", ACmp[rdp.acmp]);
}

/*
* Sets which depth source value to use for comparisons with
* the Z-buffer.
*
* source - depth source value.
* - G_ZS_PIXEL (Use the Z-value and 'deltaZ' value repeated for each pixel)
* - G_ZS_PRIM (Use the primitive depth register's Z value and 'delta Z' value)
*/
static INLINE void gDPSetDepthSource( uint32_t source )
{
   rdp.zsrc = source;
   rdp.update |= UPDATE_ZBUF_ENABLED;
   //FRDP ("z-src sel: %s\n", str_zs[rdp.zsrc]);
   //FRDP ("z-src sel: %08lx\n", rdp.zsrc);
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
         grClipWindow (rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);
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

/*
* Sets the RDP's environment color, which is used as an input source for
* linaer interpolation by the color combiner (CC). The environment color
* register set by this macro becomes a general-purpose color register of the
* CC, expressing the environment of the scene.
*
* It is also used for rendering a second specular highlight (Hilite2). The
* value in this environment color register determines the highlight color
* for Hilite2.
*
* r - red component of RGBA color (8-bit precision, 0~255)
* g - red component of RGBA color (8-bit precision, 0~255)
* b - red component of RGBA color (8-bit precision, 0~255)
* a - red component of RGBA color (8-bit precision, 0~255)
*/
static void gDPSetEnvColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a )
{
   uint32_t w1 = rdp.cmd1;
   rdp.env_color = w1;
   rdp.update |= UPDATE_COMBINE;

   //FRDP("setenvcolor: %08lx\n", w1);
}

/*
 * Sets the location of the frame buffer in memory where the DP should rasterize
 * geometry. The Color Format bits are normally set to RGBA and the Pixel Size 
 * set to match the bit dpeth specified in the VI_CONTROL_REG register.
 *
 * fmt - Controls the output format of the rasterized image
 *
 * - 000 - RGBA
 * - 001 - YUV
 * - 010 - Color Index (CI)
 * - 011 - Intensity Alpha (IA)
 * - 011 - Intensity (I)
 *
 * size - Size of an individual pixel in terms of bits
 *
 * - 00  - 4 bits
 * - 01  - 8 bits (Color Index)
 * - 10  - 16 bits (RGBA)
 * - 11  - 32 bits (RGBA)
 *
 * width - Width of frame buffer in memory minus one. This width should correspond
 * to the width of the frame buffer given to the VI_H_WIDTH_REG register.
 *
 * img - The address in memory where the frame buffer resides.
 */
static void gDPSetColorImage(int32_t fmt, int32_t siz, int32_t width, int32_t img)
{
   int i;

   if (fb_emulation_enabled && (rdp.num_of_ci < NUMTEXBUF))
   {
      COLOR_IMAGE *cur_fb, *prev_fb, *next_fb;
      cur_fb  = (COLOR_IMAGE*)&rdp.frame_buffers[rdp.ci_count];
      prev_fb = (COLOR_IMAGE*)&rdp.frame_buffers[rdp.ci_count?rdp.ci_count-1:0];
      next_fb = (COLOR_IMAGE*)&rdp.frame_buffers[rdp.ci_count+1];

      switch (cur_fb->status)
      {
         case CI_MAIN:
            {

               if (rdp.ci_count == 0)
               {
                  if ((rdp.ci_status == CI_AUX)) //for PPL
                  {
                     float sx = rdp.scale_x;
                     float sy = rdp.scale_y;
                     rdp.scale_x = 1.0f;
                     rdp.scale_y = 1.0f;
                     CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
                     rdp.scale_x = sx;
                     rdp.scale_y = sy;
                  }
#ifdef HAVE_HWFBE
                  if (fb_hwfbe_enabled)
                  {
                     if (rdp.copy_ci_index && (settings.hacks&hack_PMario))   // tidal wave
                        OpenTextureBuffer(&rdp.frame_buffers[rdp.main_ci_index]);
                  }
                  else
#endif
                  {
                     if ((rdp.num_of_ci > 1) &&
                           (next_fb->status == CI_AUX) &&
                           (next_fb->width >= cur_fb->width))
                     {
                        rdp.scale_x = 1.0f;
                        rdp.scale_y = 1.0f;
                     }
                  }
               }
#ifdef HAVE_HWFBE
               else if (!rdp.motionblur && fb_hwfbe_enabled && !SwapOK && (rdp.ci_count <= rdp.copy_ci_index))
               {
                  if (next_fb->status == CI_AUX_COPY)
                     OpenTextureBuffer(&rdp.frame_buffers[rdp.main_ci_index]);
                  else
                     OpenTextureBuffer(&rdp.frame_buffers[rdp.copy_ci_index]);
               }
               else if (fb_hwfbe_enabled && prev_fb->status == CI_AUX)
               {
                  if (rdp.motionblur)
                  {
                     rdp.cur_image = &(rdp.texbufs[rdp.cur_tex_buf].images[0]);
                     grRenderBuffer( GR_BUFFER_TEXTUREBUFFER_EXT );
                     grTextureBufferExt( rdp.cur_image->tmu, rdp.cur_image->tex_addr, rdp.cur_image->info.smallLodLog2, rdp.cur_image->info.largeLodLog2,
                           rdp.cur_image->info.aspectRatioLog2, rdp.cur_image->info.format, GR_MIPMAPLEVELMASK_BOTH );
                  }
                  else if (rdp.read_whole_frame)
                  {
                     OpenTextureBuffer(&rdp.frame_buffers[rdp.main_ci_index]);
                  }
               }
#endif
               //else if (rdp.ci_status == CI_AUX && !rdp.copy_ci_index)
               //  CloseTextureBuffer(false);

               rdp.skip_drawing = false;
            }
            break;
         case CI_COPY:
            {
               if (!rdp.motionblur || (settings.frame_buffer&fb_motionblur))
               {
                  if (cur_fb->width == rdp.ci_width)
                  {
#ifdef HAVE_HWFBE
                     if (fb_hwfbe_enabled && CopyTextureBuffer(prev_fb, cur_fb))
                     {
                        //                      if (CloseTextureBuffer(TRUE))
                        //*
                        if ((settings.hacks&hack_Zelda) && (rdp.frame_buffers[rdp.ci_count+2].status == CI_AUX) && !rdp.fb_drawn) //hack for photo camera in Zelda MM
                        {
                           CopyFrameBuffer (GR_BUFFER_TEXTUREBUFFER_EXT);
                           rdp.fb_drawn = true;
                           memcpy(gfx.RDRAM+cur_fb->addr,gfx.RDRAM+rdp.cimg, (cur_fb->width*cur_fb->height)<<cur_fb->size>>1);
                        }
                        //*/
                     }
                     else
#endif
                     {
                        if (!rdp.fb_drawn || prev_fb->status == CI_COPY_SELF)
                        {
                           CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
                           rdp.fb_drawn = true;
                        }
                        memcpy(gfx.RDRAM+cur_fb->addr,gfx.RDRAM+rdp.cimg, (cur_fb->width*cur_fb->height)<<cur_fb->size>>1);
                     }
                  }
#ifdef HAVE_HWFBE
                  else if (fb_hwfbe_enabled)
                     CloseTextureBuffer(true);
#endif
               }
               else
                  memset(gfx.RDRAM+cur_fb->addr, 0, cur_fb->width*cur_fb->height*rdp.ci_size);
               rdp.skip_drawing = true;
            }
            break;
         case CI_AUX_COPY:
            {
               rdp.skip_drawing = false;
#ifdef HAVE_HWFBE
               if (fb_hwfbe_enabled && CloseTextureBuffer(prev_fb->status != CI_AUX_COPY))
                  ;
               else
#endif
                  if (!rdp.fb_drawn)
                  {
                     CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
                     rdp.fb_drawn = true;
                  }
#ifdef HAVE_HWFBE
               if (fb_hwfbe_enabled)
                  OpenTextureBuffer(cur_fb);
#endif
            }
            break;
         case CI_OLD_COPY:
            {
               if (!rdp.motionblur || (settings.frame_buffer&fb_motionblur))
               {
                  if (cur_fb->width == rdp.ci_width)
                  {
                     memcpy(gfx.RDRAM+cur_fb->addr,gfx.RDRAM+rdp.maincimg[1].addr, (cur_fb->width*cur_fb->height)<<cur_fb->size>>1);
                  }
                  //rdp.skip_drawing = true;
               }
               else
               {
                  memset(gfx.RDRAM+cur_fb->addr, 0, (cur_fb->width*cur_fb->height)<<rdp.ci_size>>1);
               }
            }
            break;
            /*
               else if (rdp.frame_buffers[rdp.ci_count].status == ci_main_i)
               {
            // CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
            rdp.scale_x = rdp.scale_x_bak;
            rdp.scale_y = rdp.scale_y_bak;
            rdp.skip_drawing = false;
            }
            */
         case CI_AUX:
            {
               if (
#ifdef HAVE_HWFBE
                     !fb_hwfbe_enabled &&
#endif
                     cur_fb->format != G_IM_FMT_RGBA)
                  rdp.skip_drawing = true;
               else
               {
                  rdp.skip_drawing = false;
#ifdef HAVE_HWFBE
                  if (fb_hwfbe_enabled && OpenTextureBuffer(cur_fb))
                     ;
                  else
#endif
                  {
                     if (cur_fb->format != 0)
                        rdp.skip_drawing = true;
                     if (rdp.ci_count == 0)
                     {
                        //           if (rdp.num_of_ci > 1)
                        //           {
                        rdp.scale_x = 1.0f;
                        rdp.scale_y = 1.0f;
                        //           }
                     }
                     else if (!fb_hwfbe_enabled && (prev_fb->status == CI_MAIN) &&
                           (prev_fb->width == cur_fb->width)) // for Pokemon Stadium
                        CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
                  }
               }
               cur_fb->status = CI_AUX;
            }
            break;
         case CI_ZIMG:
#ifdef HAVE_HWFBE
            if (settings.ucode != ucode_PerfectDark)
            {
               if (fb_hwfbe_enabled && !rdp.copy_ci_index && (rdp.copy_zi_index || (settings.hacks&hack_BAR)))
               {
                  GrLOD_t LOD = GR_LOD_LOG2_1024;
                  if (settings.scr_res_x > 1024)
                     LOD = GR_LOD_LOG2_2048;
                  grTextureAuxBufferExt( rdp.texbufs[0].tmu, rdp.texbufs[0].begin, LOD, LOD,
                        GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH );
                  grAuxBufferExt( GR_BUFFER_TEXTUREAUXBUFFER_EXT );
                  LRDP("rdp_setcolorimage - set texture depth buffer to TMU0\n");
               }
            }
#endif
            rdp.skip_drawing = true;
            break;
         case CI_ZCOPY:
            if (settings.ucode != ucode_PerfectDark)
            {
#ifdef HAVE_HWFBE
               if (fb_hwfbe_enabled && !rdp.copy_ci_index && rdp.copy_zi_index == rdp.ci_count)
               {
                  CopyDepthBuffer();
               }
#endif
               rdp.skip_drawing = true;
            }
            break;
         case CI_USELESS:
            rdp.skip_drawing = true;
            break;
         case CI_COPY_SELF:
#ifdef HAVE_HWFBE
            if (fb_hwfbe_enabled && (rdp.ci_count <= rdp.copy_ci_index) && (!SwapOK || settings.swapmode == 2))
               OpenTextureBuffer(cur_fb);
#endif
            rdp.skip_drawing = false;
            break;
         default:
            rdp.skip_drawing = false;
      }

      if ((rdp.ci_count > 0) && (prev_fb->status >= CI_AUX)) //for Pokemon Stadium
      {
         if (!fb_hwfbe_enabled && prev_fb->format == G_IM_FMT_RGBA)
            CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
         else if ((settings.hacks&hack_Knockout) && prev_fb->width < 100)
            CopyFrameBuffer (GR_BUFFER_TEXTUREBUFFER_EXT);
      }

      if (
#ifdef HAVE_HWFBE
            !fb_hwfbe_enabled &&
#endif
            cur_fb->status == CI_COPY)
      {
         if (!rdp.motionblur && (rdp.num_of_ci > rdp.ci_count+1) && (next_fb->status != CI_AUX))
         {
            RestoreScale();
         }
      }
      if (
#ifdef HAVE_HWFBE
            !fb_hwfbe_enabled &&
#endif
            cur_fb->status == CI_AUX)
      {
         if (cur_fb->format == 0)
         {
            if ((settings.hacks&hack_PPL) && (rdp.scale_x < 1.1f))  //need to put current image back to frame buffer
            {
               int y, x, width, height;
               uint16_t *ptr_dst, *ptr_src, c;
               width = cur_fb->width;
               height = cur_fb->height;
               ptr_dst = (uint16_t*)malloc(width * height * sizeof(uint16_t));
               ptr_src = (uint16_t*)(gfx.RDRAM+cur_fb->addr);

               for (y = 0; y < height; y++)
               {
                  for (x = 0; x < width; x++)
                  {
                     c = ((ptr_src[(x + y * width)^1]) >> 1) | 0x8000;
                     ptr_dst[x + y * width] = c;
                  }
               }
               grLfbWriteRegion(GR_BUFFER_BACKBUFFER,
                     (uint32_t)rdp.offset_x,
                     (uint32_t)rdp.offset_y,
                     GR_LFB_SRC_FMT_555,
                     width,
                     height,
                     FXFALSE,
                     width<<1,
                     ptr_dst);

               free(ptr_dst);
            }
            /*
               else  //just clear buffer
               {

               grColorMask(FXTRUE, FXTRUE);
               grBufferClear (0, 0, 0xFFFF);
               }
               */
         }
      }

      if ((cur_fb->status == CI_MAIN) && (rdp.ci_count > 0))
      {
         int to_org_res = true;
         for (i = rdp.ci_count + 1; i < rdp.num_of_ci; i++)
         {
            if ((rdp.frame_buffers[i].status != CI_MAIN) && (rdp.frame_buffers[i].status != CI_ZIMG) && (rdp.frame_buffers[i].status != CI_ZCOPY))
            {
               to_org_res = false;
               break;
            }
         }
         if (to_org_res)
         {
            LRDP("return to original scale\n");
            rdp.scale_x = rdp.scale_x_bak;
            rdp.scale_y = rdp.scale_y_bak;
#ifdef HAVE_HWFBE
            if (fb_hwfbe_enabled && !rdp.read_whole_frame)
               CloseTextureBuffer(false);
#endif
         }
#ifdef HAVE_HWFBE
         if (fb_hwfbe_enabled && !rdp.read_whole_frame && (prev_fb->status >= CI_AUX) && (rdp.ci_count > rdp.copy_ci_index))
            CloseTextureBuffer(false);
#endif

      }
      rdp.ci_status = cur_fb->status;
      rdp.ci_count++;
   }

   rdp.ocimg = rdp.cimg;
   rdp.cimg = img;
   rdp.ci_width = width;
   if (fb_emulation_enabled)
      rdp.ci_height = rdp.frame_buffers[rdp.ci_count-1].height;
   else if (rdp.ci_width == 32)
      rdp.ci_height = 32;
   else
      rdp.ci_height = rdp.scissor_o.lr_y;
   if (rdp.zimg == rdp.cimg)
   {
      rdp.zi_width = rdp.ci_width;
      //    int zi_height = min((int)rdp.zi_width*3/4, (int)rdp.vi_height);
      //    rdp.zi_words = rdp.zi_width * zi_height;
   }
   rdp.ci_size = siz;
   rdp.ci_end = rdp.cimg + ((rdp.ci_width*rdp.ci_height)<<(rdp.ci_size-1));


   if (fmt != G_IM_FMT_RGBA) //can't draw into non RGBA buffer
   {
#ifdef HAVE_HWFBE
      if (!rdp.cur_image)
#endif
      {
#ifdef HAVE_HWFBE
         if (fb_hwfbe_enabled && rdp.ci_width <= 64)
            OpenTextureBuffer(&rdp.frame_buffers[rdp.ci_count - 1]);
         else
#endif
            if (fmt > 2)
               rdp.skip_drawing = true;
         return;
      }
   }
   else
   {
      if (!fb_emulation_enabled)
         rdp.skip_drawing = false;
   }

   CI_SET = true;
   if (settings.swapmode > 0)
   {
      if (rdp.zimg == rdp.cimg)
         rdp.updatescreen = 1;

      int viSwapOK = ((settings.swapmode == 2) && (rdp.vi_org_reg == *gfx.VI_ORIGIN_REG)) ? false : true;
      if ((rdp.zimg != rdp.cimg) && (rdp.ocimg != rdp.cimg) && SwapOK && viSwapOK
#ifdef HAVE_HWFBE
            && !rdp.cur_image
#endif
         )
      {
         if (fb_emulation_enabled)
            rdp.maincimg[0] = rdp.frame_buffers[rdp.main_ci_index];
         else
            rdp.maincimg[0].addr = rdp.cimg;
         rdp.last_drawn_ci_addr = (settings.swapmode == 2) ? swapped_addr : rdp.maincimg[0].addr;
         swapped_addr = rdp.cimg;
         newSwapBuffers();
         rdp.vi_org_reg = *gfx.VI_ORIGIN_REG;
         SwapOK = false;
#ifdef HAVE_HWFBE
         if (fb_hwfbe_enabled)
         {
            if (rdp.copy_ci_index && (rdp.frame_buffers[rdp.ci_count-1].status != CI_ZIMG))
            {
               int idx = (rdp.frame_buffers[rdp.ci_count].status == CI_AUX_COPY) ? rdp.main_ci_index : rdp.copy_ci_index;
               OpenTextureBuffer(&rdp.frame_buffers[idx]);
               if (rdp.frame_buffers[rdp.copy_ci_index].status == CI_MAIN) //tidal wave
                  rdp.copy_ci_index = 0;
               //FRDP("attempt open tex buffer. status: %s, addr: %08lx\n", CIStatus[rdp.frame_buffers[idx].status], rdp.frame_buffers[idx].addr);
            }
            else if (rdp.read_whole_frame && !rdp.cur_image)
               OpenTextureBuffer(&rdp.frame_buffers[rdp.main_ci_index]);
         }
#endif
      }
   }
   //FRDP("setcolorimage - %08lx, width: %d,  height: %d, format: %d, size: %d\n", w1, rdp.ci_width, rdp.ci_height, fmt, rdp.ci_size);
   //FRDP("cimg: %08lx, ocimg: %08lx, SwapOK: %d\n", rdp.cimg, rdp.ocimg, SwapOK);
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

/* Sets the matrix coefficients used to convert YUV pixels into RGB. Conceptually, the equations are
* shown below:
*
* R = C0 * (Y-16) + C1 * V
* G = C0 * (Y-16) + C2 * U - C3 * V
* B = C0 * (Y-16) + C4 * U
*
* k0 - K0 term of the YUV-RGB conversion matrix (9-bit precision, -256~255).
* k1 - K1 term of the YUV-RGB conversion matrix (9-bit precision, -256~255).
* k2 - K2 term of the YUV-RGB conversion matrix (9-bit precision, -256~255).
* k3 - K3 term of the YUV-RGB conversion matrix (9-bit precision, -256~255).
* k4 - K4 term of the YUV-RGB conversion matrix (9-bit precision, -256~255).
* k5 - K5 term of the YUV-RGB conversion matrix (9-bit precision, -256~255).
*/
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

/*
* Sets the color combiner (CC) mode. Makes detailed
* settings regarding the input sources to the CC.
* Although the CC mainly combines colors, it also
* performs post-color space conversion processing and
* sets chroma keying (currently unsupported) an LOD
* processes. These combinations and setup processes
* are accomplished inside the CC by linearly interpolating
* various input sources.
*
* In 1-cycle mode, mode1 and mode2 must be set to the
* same mode.
*
* In 2-cycle mode, although the CC can execute linear
* interpolation calculations twice, texture and shading
* color modulation processes are usually executed in the
* second cycle.
*
* mode1 - CC mode for the first cycle.
* mode2 - CC mode for the second cycle.
*/
static void gDP_SetCombine(uint32_t w0, uint32_t w1)
{
   rdp.c_a0  = (uint8_t)((w0 >> 20) & 0xF);
   rdp.c_b0  = (uint8_t)((w1 >> 28) & 0xF);
   rdp.c_c0  = (uint8_t)((w0 >> 15) & 0x1F);
   rdp.c_d0  = (uint8_t)((w1 >> 15) & 0x7);
   rdp.c_Aa0 = (uint8_t)((w0 >> 12) & 0x7);
   rdp.c_Ab0 = (uint8_t)((w1 >> 12) & 0x7);
   rdp.c_Ac0 = (uint8_t)((w0 >> 9)  & 0x7);
   rdp.c_Ad0 = (uint8_t)((w1 >> 9)  & 0x7);

   rdp.c_a1  = (uint8_t)((w0 >> 5)  & 0xF);
   rdp.c_b1  = (uint8_t)((w1 >> 24) & 0xF);
   rdp.c_c1  = (uint8_t)((w0 >> 0)  & 0x1F);
   rdp.c_d1  = (uint8_t)((w1 >> 6)  & 0x7);
   rdp.c_Aa1 = (uint8_t)((w1 >> 21) & 0x7);
   rdp.c_Ab1 = (uint8_t)((w1 >> 3)  & 0x7);
   rdp.c_Ac1 = (uint8_t)((w1 >> 18) & 0x7);
   rdp.c_Ad1 = (uint8_t)((w1 >> 0)  & 0x7);

   rdp.cycle1 = (rdp.c_a0<<0)  | (rdp.c_b0<<4)  | (rdp.c_c0<<8)  | (rdp.c_d0<<13)|
      (rdp.c_Aa0<<16)| (rdp.c_Ab0<<19)| (rdp.c_Ac0<<22)| (rdp.c_Ad0<<25);
   rdp.cycle2 = (rdp.c_a1<<0)  | (rdp.c_b1<<4)  | (rdp.c_c1<<8)  | (rdp.c_d1<<13)|
      (rdp.c_Aa1<<16)| (rdp.c_Ab1<<19)| (rdp.c_Ac1<<22)| (rdp.c_Ad1<<25);

   rdp.update |= UPDATE_COMBINE;

   //FRDP("setcombine\na0=%s b0=%s c0=%s d0=%s\nAa0=%s Ab0=%s Ac0=%s Ad0=%s\na1=%s b1=%s c1=%s d1=%s\nAa1=%s Ab1=%s Ac1=%s Ad1=%s\n", Mode0[rdp.c_a0], Mode1[rdp.c_b0], Mode2[rdp.c_c0], Mode3[rdp.c_d0], Alpha0[rdp.c_Aa0], Alpha1[rdp.c_Ab0], Alpha2[rdp.c_Ac0], Alpha3[rdp.c_Ad0], Mode0[rdp.c_a1], Mode1[rdp.c_b1], Mode2[rdp.c_c1], Mode3[rdp.c_d1], Alpha0[rdp.c_Aa1], Alpha1[rdp.c_Ab1], Alpha2[rdp.c_Ac1], Alpha3[rdp.c_Ad1]);
}
