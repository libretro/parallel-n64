//forward decls
extern void LoadBlock32b(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt);
extern void RestoreScale(void);

extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int CI_SET;
extern uint32_t swapped_addr;

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
