//forward decls
extern void LoadBlock32b(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt);
extern void RestoreScale(void);

extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int CI_SET;
extern uint32_t swapped_addr;

static void gDPSetScissor_G64( uint32_t mode, float ulx, float uly, float lrx, float lry )
{
   rdp.scissor_o.ul_x = (uint32_t)ulx;
   rdp.scissor_o.ul_y = (uint32_t)ulx;
   rdp.scissor_o.lr_x = (uint32_t)lrx;
   rdp.scissor_o.lr_y = (uint32_t)lry;
   if (rdp.ci_count)
   {
      COLOR_IMAGE *cur_fb = (COLOR_IMAGE*)&rdp.frame_buffers[rdp.ci_count-1];
      if (rdp.scissor_o.lr_x - rdp.scissor_o.ul_x > (uint32_t)(cur_fb->width >> 1))
      {
         if (cur_fb->height == 0 || (cur_fb->width >= rdp.scissor_o.lr_x-1 && cur_fb->width <= rdp.scissor_o.lr_x+1))
            cur_fb->height = rdp.scissor_o.lr_y;
      }
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

   if (rdp.timg.size == G_IM_SIZ_32b)
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
