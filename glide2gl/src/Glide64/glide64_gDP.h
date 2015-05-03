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
