typedef struct DRAWOBJECT_t
{
  float objX;
  float objY;
  float scaleW;
  float scaleH;
  int16_t imageW;
  int16_t imageH;

  uint16_t  imageStride;
  uint16_t  imageAdrs;
  uint8_t  imageFmt;
  uint8_t  imageSiz;
  uint8_t  imagePal;
  uint8_t  imageFlags;
} DRAWOBJECT;

struct MAT2D {
  float A, B, C, D;
  float X, Y;
  float BaseScaleX;
  float BaseScaleY;
} mat_2d = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};

// positional and texel coordinate clipping
#define CCLIP(ux,lx,ut,lt,uc,lc) \
		if (ux > lx || lx < uc || ux > lc) { rdp.tri_n += 2; return; } \
		if (ux < uc) { \
			float p = (uc-ux)/(lx-ux); \
			ut = p*(lt-ut)+ut; \
			ux = uc; \
		} \
		if (lx > lc) { \
			float p = (lc-ux)/(lx-ux); \
			lt = p*(lt-ut)+ut; \
			lx = lc; \
		}

#define CCLIP2(ux,lx,ut,lt,un,ln,uc,lc) \
		if (ux > lx || lx < uc || ux > lc) { rdp.tri_n += 2; return; } \
		if (ux < uc) { \
			float p = (uc-ux)/(lx-ux); \
			ut = p*(lt-ut)+ut; \
			un = p*(ln-un)+un; \
			ux = uc; \
		} \
		if (lx > lc) { \
			float p = (lc-ux)/(lx-ux); \
			lt = p*(lt-ut)+ut; \
			ln = p*(ln-un)+un; \
			lx = lc; \
		}

//forward decls
static void uc6_draw_polygons (VERTEX v[4]);
static void uc6_read_object_data (DRAWOBJECT *d);
static void uc6_init_tile(const DRAWOBJECT *d);
extern uint32_t dma_offset_mtx;
extern int32_t cur_mtx;
extern uint32_t dma_offset_mtx;
extern uint32_t dma_offset_vtx;
extern int32_t billboarding;

int dzdx = 0;
int deltaZ = 0;
VERTEX **org_vtx;

//software backface culling. Gonetz
// mega modifications by Dave2001

static int cull_tri(VERTEX **v) // type changed to VERTEX** [Dave2001]
{
   int i, draw;
   float x1, y1, x2, y2, area;

   if (v[0]->scr_off & v[1]->scr_off & v[2]->scr_off)
      return true;

   // Triangle can't be culled, if it need clipping
   draw = false;

   for (i=0; i<3; i++)
   {
      if (!v[i]->screen_translated)
      {
         v[i]->sx = rdp.view_trans[0] + v[i]->x_w * rdp.view_scale[0] + rdp.offset_x;
         v[i]->sy = rdp.view_trans[1] + v[i]->y_w * rdp.view_scale[1] + rdp.offset_y;
         v[i]->sz = rdp.view_trans[2] + v[i]->z_w * rdp.view_scale[2];
         v[i]->screen_translated = 1;
      }
      if (v[i]->w < 0.01f) //need clip_z. can't be culled now
         draw = 1;
   }

   rdp.u_cull_mode = (rdp.flags & CULLMASK);
   if (draw || rdp.u_cull_mode == 0 || rdp.u_cull_mode == CULLMASK) //no culling set
   {
      rdp.u_cull_mode >>= CULLSHIFT;
      return false;
   }

   x1 = v[0]->sx - v[1]->sx;
   y1 = v[0]->sy - v[1]->sy;
   x2 = v[2]->sx - v[1]->sx;
   y2 = v[2]->sy - v[1]->sy;
   area = y1 * x2 - x1 * y2;

   rdp.u_cull_mode >>= CULLSHIFT;

   switch (rdp.u_cull_mode)
   {
      case 1: // cull front
         if (area < 0.0f) //counter-clockwise, positive
            return true;
         break;
      case 2: // cull back
         if (area >= 0.0f) //clockwise, negative
            return true;
         break;
   }

   return false;
}

/*
 * Pops from one matrix stack.
 *
 * The modelview matrix stack is 10 levels deep, and the projection
 * matrix stack is 1 level deep (so it is impossible to pop a projection
 * matrix stack).
 *
 * param - the flag field identifying the matrix stack to pop
 *       - G_MTX_MODELVIEW  - modelview matrix stack
 *       - G_MTX_PROJECTION - projection matrix stack (not implemented)
 */
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

/*
 * Sets the viewport area. The viewport sructure elements have a 
 * 2-bit fraction required for scaling to sub-pixel positions.
 * This can be used to handle the fraction values in the viewport.
 *
 * vscale, vtrans are the screen coordinates. The aray indices
 * 0, 1, 2 correspond to x, y, z while array index 3 is used for
 * alignment.
 *
 * The viewport is the area the image occupies on the screen.
 *
 * v      - segment address to the viewport structure "Vp".
 * vscale - scale applied to the normalized homogeneous coordinates
 *          after 4x4 projection transformation.
 * vtrans - the offset added to the scaled value.
 *
 * FIXME: Not spec-conformant.
 */
static void gSPViewport(uint32_t v, bool correct_viewport)
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
   if (correct_viewport)
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
   //FRDP ("viewport scale(%d, %d, %d), trans(%d, %d, %d), from:%08lx\n", scale_x, scale_y, scale_z, trans_x, trans_y, trans_z, a);
}

/*
 * Enables/disables textures and sets the scaling value for texturing in the
 * RSP.
 *
 * The texture coordinates are computed by the RSP using the following formula:
 *
 * (Displayed texture coordinates) = (Scale value) * (Shifted texture coordinate values (see note))
 *
 * Shifted texture coordinate values: These are the values of the texture coordinates obtained
 * from the Vtx structure after they have been shifted by the "shift" parameter set in the texture
 * tile attributes.
 *
 * sc    - scaling value for the texture's s coordinate (16-bit unsigned precision, .16)
 * tc    - scaling value for the texture's t coordinate (16-bit unsigned precision, .16)
 * level - (maximum number of MIP-map levels) - 1
 * tile  - tile descriptor index (3-bit precision, 0~7)
 * on    - texture flag - G_ON (texture on) | G_OFF (texture off)
 */
static void gSPTexture(int32_t sc, int32_t tc, int32_t level,
      int32_t tile, int32_t on)
{
   TILE *tmp_tile;
   if (tile == 7 && (settings.hacks&hack_Supercross))
      tile = 0; //fix for supercross 2000
   rdp.mipmap_level = level;
   rdp.cur_tile = tile;

   rdp.tiles[tile].on = 0;

   if (!on)
      return;

   tmp_tile = (TILE*)&rdp.tiles[tile];
   tmp_tile->on = 1;
   tmp_tile->org_s_scale = sc;
   tmp_tile->org_t_scale = tc;
   tmp_tile->s_scale = (float)(sc+1)/65536.0f;
   tmp_tile->t_scale = (float)(tc+1)/65536.0f;
   tmp_tile->s_scale /= 32.0f;
   tmp_tile->t_scale /= 32.0f;

   rdp.update |= UPDATE_TEXTURE;

   //FRDP("uc0:texture: tile: %d, mipmap_lvl: %d, on: %d, s_scale: %f, t_scale: %f\n", tile, rdp.mipmap_level, on, tmp_tile->s_scale, tmp_tile->t_scale);
}

/*
 * Loads one Light structure into the RSP.
 *
 * Loads one Light structure at the specified position in the light buffer.
 * Use gSPNumLights to specify the number of lights to use in the lighting
 * calculation. When gSPNuLights specifies N number of lights, the 1st to
 * Nth lights are used as directional lights (color and direction), and N+1
 * light is used as the ambient light (color only).
 *
 * l - pointer to Light structure.
 * n - the light number that is replaced (1 to 8).
 */
static void gSPLight(void *ptr, uint32_t l, unsigned n)
{
   uint32_t address = RSP_SegmentToPhysical(l);

   // Get the data
   rdp.light[n].col[0] = (((uint8_t*)ptr)[(address+0)^3]) * 0.0039215689f;
   rdp.light[n].col[1] = (((uint8_t*)ptr)[(address+1)^3]) * 0.0039215689f;
   rdp.light[n].col[2] = (((uint8_t*)ptr)[(address+2)^3]) * 0.0039215689f;
   rdp.light[n].col[3] = 1.0f;
   // ** Thanks to Icepir8 for pointing this out **
   // Lighting must be signed byte instead of byte
   rdp.light[n].dir[0] = (float)(((int8_t*)ptr)[(address+8)^3]) / 127.0f;
   rdp.light[n].dir[1] = (float)(((int8_t*)ptr)[(address+9)^3]) / 127.0f;
   rdp.light[n].dir[2] = (float)(((int8_t*)ptr)[(address+10)^3]) / 127.0f;
   // **

   //rdp.update |= UPDATE_LIGHTS;

   //FRDP ("light: n: %d, r: %.3f, g: %.3f, b: %.3f, x: %.3f, y: %.3f, z: %.3f\n", i, rdp.light[i].r, rdp.light[i].g, rdp.light[i].b, rdp.light_vector[i][0], rdp.light_vector[i][1], rdp.light_vector[i][2]);
}

/*
 * Loads new matrix without performing multiplication.
 *
 * Loads a new matrix (indicated by mptr) on to the top of the RSP's matrix stack.
 * Without undergoing matrix multiplication, this new matrix replaces the single
 * matrix (the concatenated matrix of the modelview and projection matrices) used
 * for the entire transformation.
 *
 * There is no matrix pushing or popping on the model view and projection matrix
 * stacks, and the tops of the stack are not modified.
 *
 * mptr - The pointer to the matrix to load.
 */
static void gSPForceMatrix(uint32_t mptr)
{
   uint32_t address = RSP_SegmentToPhysical(mptr);
   load_matrix(rdp.combined, address);

   // do not update the combined matrix!
   rdp.update &= ~UPDATE_MULT_MAT;

   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[0][0], rdp.combined[0][1], rdp.combined[0][2], rdp.combined[0][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[1][0], rdp.combined[1][1], rdp.combined[1][2], rdp.combined[1][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[2][0], rdp.combined[2][1], rdp.combined[2][2], rdp.combined[2][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.combined[3][0], rdp.combined[3][1], rdp.combined[3][2], rdp.combined[3][3]);
}

/*
 * Fog formula: alpha(fog) = (eyespace z) * fm  + fo  CLAMPED 0 to 255
 *
 * NOTE: (eyespace z) ranges from -1 to 1
 *
 * fm - Z multiplier
 * fo - Z offset
 */
static void gSPFogFactor(int16_t fm, int16_t fo)
{
   rdp.fog_multiplier = fm;
   rdp.fog_offset = fo;
   //FRDP ("fog: multiplier: %f, offset: %f\n", rdp.fog_multiplier, rdp.fog_offset);
}

/*
 * Specifies the size ratio between the clipping box and the scissoring
 * box in order to boost performance.
 *
 * Following explanation assumes scissoring box is the same size as the viewport
 * region:
 *
 * Scissoring is performed by rasterizing the entire triangle and then deleting
 * pixels which lay outside the screen (this consumes processing time in the RDP).
 *
 * Clipping is performed by converting a large triangle that protrudes out of the
 * clipping box, into a number of small triangles, all of which fit inside the clipping
 * box (this consumes RSP processing time in the RSP).
 *
 * When the clipping box is near in size to the scissoring box there are more occasions
 * for clipping, which places a greater burden on the RSP. In addition, RDP processing
 * speed declines because there is also more rendering.
 *
 * Conversely, when the clipping box is much larger than the scissoring box, ther are
 * fewer occasions for clipping so the burden on the RSP declines. However, RDP
 * processing speed will also decline because there are more occasions for scissoring
 * (which is performed by the RDP).
 *
 * r - the relative size (Clipping box: scissoring box)
 *   - FRUSTRATIO_1 (1:1)
 *   - FRUSTRATIO_2 (2:1)
 *   - FRUSTRATIO_3 (3:1)
 *   - FRUSTRATIO_4 (4:1)
 *   - FRUSTRATIO_5 (5:1)
 *   - FRUSTRATIO_6 (6:1)
 *
 * FIXME - not consistent with glN64
 */
static void gSPClipRatio(uint32_t w0, uint32_t w1)
{
   if (((w0 >> 8) & 0xFFFF) == G_MW_CLIP)
   {
      rdp.clip_ratio = squareRoot((float)w1);
      rdp.update |= UPDATE_VIEWPORT;
   }
   //FRDP ("clip %08lx, %08lx\n", w0, w1);
}

/*
 * Sets the geometry pipeline modes enabled.
 *
 * Sets modes for culling, lighting, specular highlights, reflection mapping,
 * fog, etc. use gSPClearGeometryMode to disable the modes.
 *
 * Numerous specifications can be set for mode with a bit sum of the following
 * flags:
 *
 * mode - the geometry pipeline mode:
 *      - G_SHADE          - Enables calculation of vertex color for a triangle.
 *      - G_LIGHTING       - Enables lighting calculations.
 *      - G_SHADING_SMOOTH - Enables Gouraud shading.
 *                           When this is not enabled, flat shading is used for the
 *                           triangle, based on the color of one vertex (see gSP1Triangle).
 *                           G_SHADE must be enabled to calculate vertex color.
 *      - G_ZBUFFER        - Enables Z buffer calculations.
 *                           Other Z buffer-related parameters for the frame buffer must
 *                           also be set.
 *      - G_TEXTURE_GEN    - Enables automatic generation of the texture's s, t coordinates.
 *                           Spherical mapping based on the normal vector is used.
 *      - G_TEXTURE_GEN_LINEAR  - Enables automatic generation of the texture's, t coordinates.
 *      - G_CULL_FRONT     - Enables front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_CULL_BACK      - Enables back-face culling.
 *      - G_CULL_BOTH      - Enables both back-face and front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_FOG            - Enables generation of vertex alpha coordinate fog parameters.
 *      - G_CLIPPING       - Enables clipping.
 *                           This mode is enabled in the initial state. When disabled, clipping
 *                           is not performed. This mode can only be used with F3DLX and F3DLX.NoN.
 *
 * Other elements which have their own commands also exist for the RSP rendering state. These
 * involve chaning the state with something more complicated than a single bit, or using other
 * commands to optimize the RSP geometry engine.
 */
static void gSPSetGeometryMode(uint32_t mode)
{
   rdp.geom_mode |= mode;

   // TODO - should we look at all these state changes at this point?
   // This isn't done in gln64

   if (mode & G_ZBUFFER)  // Z-Buffer enable
   {
      if (!(rdp.flags & ZBUF_ENABLED))
      {
         rdp.flags |= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }

   //Added by Gonetz
   if (mode & G_FOG)      // Fog enable
   {
      if (!(rdp.flags & FOG_ENABLED))
      {
         rdp.flags |= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
   //FRDP("uc0:setgeometrymode %08lx; result: %08lx\n", mode, rdp.geom_mode);
   if (settings.ucode == 5)
      return;

   if (mode & CULL_FRONT)  // Front culling
   {
      if (!(rdp.flags & CULL_FRONT))
      {
         rdp.flags |= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   if (mode & CULL_BACK)  // Back culling
   {
      if (!(rdp.flags & CULL_BACK))
      {
         rdp.flags |= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
}

/*
 * Disables the geometry pipeline modes.
 *
 * Disables pipeline modes for culling, lighting, specular highlights reflection
 * mapping, fog, etc. To enable these various modes, use gSPSetGeometryMode.
 *
 * By disabling G_CLIPPING, processing time can be reduced because the data required
 * for clipping decision does not need to be calculated by gSPVertex. Also, clipping
 * is performd in GSP1Triangle, GSP2Triangles and gSP1Quadrangle.
 *
 * mode - the geometry pipeline mode:
 *      - G_SHADE          - Disables calculation of vertex color for a triangle.
 *      - G_LIGHTING       - Disables lighting calculations.
 *      - G_SHADING_SMOOTH - Disables Gouraud shading.
 *                           When this is not enabled, flat shading is used for the
 *                           triangle, based on the color of one vertex (see gSP1Triangle).
 *                           G_SHADE must be enabled to calculate vertex color.
 *      - G_ZBUFFER        - Disables Z buffer calculations.
 *                           Other Z buffer-related parameters for the frame buffer must
 *                           also be set.
 *      - G_TEXTURE_GEN    - Disables automatic generation of the texture's s, t coordinates.
 *                           Spherical mapping based on the normal vector is used.
 *      - G_TEXTURE_GEN_LINEAR  - Disables automatic generation of the texture's, t coordinates.
 *      - G_CULL_FRONT     - Disables front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_CULL_BACK      - Disables back-face culling.
 *      - G_CULL_BOTH      - Disables both back-face and front-face culling.
 *                           * Does not support F3DLX.Rej but does support F3DLX2.Rej.
 *      - G_FOG            - Disables generation of vertex alpha coordinate fog parameters.
 *      - G_CLIPPING       - Disables clipping.
 *                           This mode is enabled in the initial state. When disabled, clipping
 *                           is not performed. This mode can only be used with F3DLX and F3DLX.NoN.
 */
static void gSPClearGeometryMode(uint32_t mode)
{
   rdp.geom_mode &= (~mode);

   // TODO - should we look at all these state changes at this point?
   // This isn't done in gln64
   
   if (mode & G_ZBUFFER)  // Z-Buffer enable
   {
      if (rdp.flags & ZBUF_ENABLED)
      {
         rdp.flags ^= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }

   //Added by Gonetz
   if (mode & G_FOG)      // Fog enable
   {
      if (rdp.flags & FOG_ENABLED)
      {
         rdp.flags ^= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }

   if (settings.ucode == 5)
      return;

   if (mode & CULL_FRONT)  // Front culling
   {
      if (rdp.flags & CULL_FRONT)
      {
         rdp.flags ^= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   if (mode & CULL_BACK)  // Back culling
   {
      if (rdp.flags & CULL_BACK)
      {
         rdp.flags ^= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   //FRDP("uc0:cleargeometrymode %08lx\n", mode);
}

/*
 * Calls a child display list from the current display list.
 *
 * This kind of display list hierarchy can be used to make
 * re-use of display lists or to structure the display list
 * data in a way that reflects the actual rendering model.
 *
 * The display list hierarchy can have up to 10 steps (18 
 * steps for gSPf3DEX.fifo.o and gSPF3DEX.NoN.fifo.o).
 *
 * d - Segment address of the child display list.
 */
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

/*
 * Branches from the current display list to a child display list.
 *
 * This dffers from gSPDisplayList in that the display list to be
 * used is not pushed onto the calling stack. In other words, this
 * macro's call is a "jump" and not a "call" like the gSPDisplayList
 * macro.
 *
 * dl - pointer to the child display list.
 */
static void gSPBranchList(uint32_t dl)
{
   uint32_t address = RSP_SegmentToPhysical(dl);

   // Don't execute display list
   // This fixes partially Gauntlet: Legends (first condition)
   if (address == rdp.pc[rdp.pc_i] - 8 || rdp.pc_i >= 9)
      return;

   rdp.pc[rdp.pc_i] = address;  // just jump to the address
}

/*
 * Ends a display list.
 *
 * When this macro is executed, the RSP pops the display list stack and,
 * if the display list stack is empty, ends the graphics process.
 */
static void gSPEndDisplayList(void)
{
   if (rdp.pc_i > 0)
      rdp.pc_i --;
   else
      rdp.halt = 1; // Halt execution here
}

/*
 * Does volume culling.
 *
 * This macro measuers whether or not the viewing volume and bounding volumes
 * intersect. If the bounding volume of an object is completely outside of the
 * viewing volume, this function operates like the gSPEndDisplayList macro,
 * and the remaining portion of the display list is skipped.
 *
 * v0 - Index of first vertex to check (v0 < vn).
 * v1 - Index of the last vertex check (vn > v0).
 */
static void gSPCullDisplayList(uint32_t v0, uint32_t vn)
{
   uint32_t i, cond;
   VERTEX *v;

   if (vn < v0)
      return;

   cond = 0;

   for (i = v0; i <= vn; i++)
   {
      v = (VERTEX*)&rdp.vtx[i];

      /*
       * Check if completely off the screen
       * (quick frustrum clipping for 90 FOV)
       */

      if (v->x >= -v->w)
         cond |= X_CLIP_MAX;
      if (v->x <= v->w)
         cond |= X_CLIP_MIN;
      if (v->y >= -v->w)
         cond |= Y_CLIP_MAX;
      if (v->y <= v->w)
         cond |= Y_CLIP_MIN;
      if (v->w >= 0.1f)
         cond |= Z_CLIP_MAX;

      if (cond == 0x1F)
         return;
   }

   gSPEndDisplayList();
}

/*
 * Sets the segment register and the base address.
 *
 * All pointers in the display list in the Reality Co-Processor (RCP)
 * are segment addresses composed of a segment base and offset address.
 * The actual addresses are calculated by the Reality Signal Processor (RSP)
 * using the following translation:
 *
 * Physical address = Base address [Segment ID] + Offset
 *
 * seg  - Segment ID (0-15)
 * base - Base physical address
 */
static INLINE void gSPSegment( int32_t seg, int32_t base )
{
    rdp.segment[seg] = base;
    //FRDP ("segment: %08lx -> seg%d\n", seg, base);
}

/*
 * Specifies the number of Light structures to load into the RSP.
 *
 * Specifies the number of lights used in lighting calculations. This macro is
 * used in association with gSPLight, which loads the light that will be
 * actually used. When this macro specifies N number of lights, the 1st to Nth lights
 * are used as directional lights (color and direction), and Nth + 1 light is used
 * as the ambient light (color only). 
 *
 * To only use ambient light, set the "n" argument to NUMLIGHTS_0 so the first
 * light is set to the ambient light color.
 *
 * n - the number of diffuse lights to use for lighting at one time:
 *   - NUMLIGHTS_0 (0 diffuse lights)
 *   - NUMLIGHTS_1 (1 diffuse light)
 *   - NUMLIGHTS_2 (2 diffuse lights)
 *   - NUMLIGHTS_3 (3 diffuse lights)
 *   - NUMLIGHTS_4 (4 diffuse lights)
 *   - NUMLIGHTS_5 (5 diffuse lights)
 *   - NUMLIGHTS_6 (6 diffuse lights)
 *   - NUMLIGHTS-7 (7 diffuse lights)
 */
static INLINE void gSPNumLights(int32_t n)
{
   rdp.num_lights = (n <= 8) ? n : 0;
   rdp.update |= UPDATE_LIGHTS;
   //FRDP ("numlights: %d\n", rdp.num_lights);
}

/*
 * Changes the color of the specified light without using DMA
 * (as a result, no extra memory is required).
 *
 * n           - the light number whose color is being modified
 *             - LIGHT_1 - First light
 *             - LIGHT_2 - Second light
 *             - LIGHT_3 - Third light
 *             - LIGHT_4 - Fourth light
 *             - LIGHT_5 - Fifth light
 *             - LIGHT_6 - Sixth light
 *             - LIGHT_7 - Seventh light
 *             - LIGHT_8 - Eight light
 * packedColor - the new light color (32-bit value 0xRRGGBB??)
 *               (?? is ignored)
 */
static void gSPLightColor( uint32_t n, uint32_t packedColor)
{
   rdp.light[n].col[0] = _SHIFTR( packedColor, 24, 8 ) * 0.0039215689f;
   rdp.light[n].col[1] = _SHIFTR( packedColor, 16, 8 ) * 0.0039215689f;
   rdp.light[n].col[2] = _SHIFTR( packedColor, 8, 8 )  * 0.0039215689f;
   rdp.light[n].col[3] = 255;
   //FRDP ("lightcol light:%d, %08lx\n", n, w1);
}

/*
 * Modifies part of the vertex data after the data has been sent to the RSP by
 * gSPVertex. The new value that is to be assigned to the part described by
 * 'where' is specified as follows in 'val':
 *
 * Color (G_MW0_POINT_RGBA):
 *   R, G, B and alpha (4 bytes each) from high-order byte to low-order byte.
 * Texture coordinate s, t values (G_MW0_POINT_ST):
 *   High-order 16 bits are the s coordinate value. Low-order 16 bits are the
 *   t coordinate value (s13.2)
 * Screen coordinate x, y values (G_MW0_POINT_XYSCREEN):
 *   High-order 16 bits are the s coordinate value. Low-order 16 bits are the
 *   y coordinate value (s13.2)
 *   * The upper-left corner of the screen is (0,0). Positive x values increase
 *   to the right, and positive y value increase downward.
 * Screen coordinate z value (G_MW0_POINT_ZSCREEN):
 *   All 32 bits are the z-coordinate value (16.6, 0x00000000~0x03ff0000) 
 *
 * vtx   - specifies which RSP vertex to modify
 * where - specifies which part of vertex data to modify:
 *         G_MW0_POINT_RGBA (Color)
 *         G_MW0_POINT_ST (Texture coordinate s, t values)
 *         G_MW0_POINT_XYSCREEN (Screen coordinate x, y values)
 *         G_MW0_POINT_ZSCREEN (Screen coordinate z value)
 * val   - new value (32-bit integer) for the data part specified by where.
 */
static void gSPModifyVertex(uint32_t vtx, uint32_t where,  uint32_t val)
{
   VERTEX *v = &rdp.vtx[vtx];

   switch (where)
   {
      case G_MWO_POINT_RGBA:    // RGBA
         v->r = (uint8_t)(val >> 24);
         v->g = (uint8_t)((val >> 16) & 0xFF);
         v->b = (uint8_t)((val >> 8) & 0xFF);
         v->a = (uint8_t)(val & 0xFF);
         v->shade_mod = 0;

         //FRDP ("RGBA: %d, %d, %d, %d\n", v->r, v->g, v->b, v->a);
         break;

      case G_MWO_POINT_ST:    // ST
         {
            float scale = rdp.Persp_en ? 0.03125f : 0.015625f;
            v->ou = (float)((int16_t)(val>>16)) * scale;
            v->ov = (float)((int16_t)(val&0xFFFF)) * scale;
            v->uv_calculated = 0xFFFFFFFF;
            v->uv_scaled = 1;
         }
         //FRDP ("u/v: (%04lx, %04lx), (%f, %f)\n", (int16_t)(val>>16), (int16_t)(val&0xFFFF), v->ou, v->ov);
         break;

      case G_MWO_POINT_XYSCREEN:    // XY screen
         {
            float scr_x = (float)((int16_t)(val>>16)) / 4.0f;
            float scr_y = (float)((int16_t)(val&0xFFFF)) / 4.0f;
            v->screen_translated = 2;
            v->sx = scr_x * rdp.scale_x + rdp.offset_x;
            v->sy = scr_y * rdp.scale_y + rdp.offset_y;
            if (v->w < 0.01f)
            {
               v->w = 1.0f;
               v->oow = 1.0f;
               v->z_w = 1.0f;
            }
            v->sz = rdp.view_trans[2] + v->z_w * rdp.view_scale[2];

            v->scr_off = 0;
            if (scr_x < 0) v->scr_off |= 1;
            if (scr_x > rdp.vi_width) v->scr_off |= 2;
            if (scr_y < 0) v->scr_off |= 4;
            if (scr_y > rdp.vi_height) v->scr_off |= 8;
            if (v->w < 0.1f) v->scr_off |= 16;

            //FRDP ("x/y: (%f, %f)\n", scr_x, scr_y);
         }
         break;

      case G_MWO_POINT_ZSCREEN:    // Z screen
         {
            float scr_z = (float)((int16_t)(val>>16));
            v->z_w = (scr_z - rdp.view_trans[2]) / rdp.view_scale[2];
            v->z = v->z_w * v->w;
            //FRDP ("z: %f\n", scr_z);
         }
         break;
   }
   //FRDP ("uc0:modifyvtx: vtx: %d, where: 0x%02lx, val: %08lx - ", vtx, where, val);
}

static void gSPClipVertex(uint32_t i)
{
   if (rdp.vtxbuf[i].x > rdp.clip_max_x)
      rdp.clip |= CLIP_XMAX;
   if (rdp.vtxbuf[i].x < rdp.clip_min_x)
      rdp.clip |= CLIP_XMIN;
   if (rdp.vtxbuf[i].y > rdp.clip_max_y)
      rdp.clip |= CLIP_YMAX;
   if (rdp.vtxbuf[i].y < rdp.clip_min_y)
      rdp.clip |= CLIP_YMIN;
}

/*
 * Conditionally branches the display list.
 *
 * When the depth value of the vertex specified by vtx is less than or 
 * equal to zval, the display list is branhed to the display list indicated
 * by branchdl. when it is more than zval,nothing is done. This provides
 * an easy way to perform LOD processing on a model.
 *
 * branchdl - pointer to the display list branch.
 * vtx      - Vertex (index in the vertex buffer).
 * zval     - The Z value which becomes the branch condition.
 * near     - the location of the near plane (a value specified by either guPerspective
 *            or guOrtho).
 * far      - the location of a far plane (a value specified by either guPerspective
 *            or guOrtho).
 * flag     - The projection method:
 *          - G_BZ_PERSP (Perspective projection)
 *          - G_BZ_ORTHO (Orthogonal projection)
 *
 * FIXME - not spec conformant.
 */
static void gSPBranchLessZ( uint32_t branchdl, uint32_t vtx, float zval )
{
   uint32_t address = RSP_SegmentToPhysical( branchdl );
   if( fabs(rdp.vtx[vtx].z) <= zval )
      rdp.pc[rdp.pc_i] = address;
   //FRDP ("uc1:branch_less_z, addr: %08lx\n", address);
}

static void gSPCombineMatrices(void)
{
   MulMatrices(rdp.model, rdp.proj, rdp.combined);
   rdp.update ^= UPDATE_MULT_MAT;
}

static void draw_tri (VERTEX **vtx, uint16_t linew)
{
   int i;
   deltaZ = dzdx = 0;
   if (linew == 0 && (fb_depth_render_enabled || (rdp.rm & ZMODE_DECAL) == ZMODE_DECAL))
   {
      float X0 = vtx[0]->sx / rdp.scale_x;
      float Y0 = vtx[0]->sy / rdp.scale_y;
      float X1 = vtx[1]->sx / rdp.scale_x;
      float Y1 = vtx[1]->sy / rdp.scale_y;
      float X2 = vtx[2]->sx / rdp.scale_x;
      float Y2 = vtx[2]->sy / rdp.scale_y;
      float diffy_02 = Y0 - Y2;
      float diffy_12 = Y1 - Y2;
      float diffx_02 = X0 - X2;
      float diffx_12 = X1 - X2;

      float denom = (diffx_02 * diffy_12 - diffx_12 * diffy_02);
      if(denom*denom > 0.0)
      {
         float diffz_02 = vtx[0]->sz - vtx[2]->sz;
         float diffz_12 = vtx[1]->sz - vtx[2]->sz;
         float fdzdx = (diffz_02 * diffy_12 - diffz_12 * diffy_02) / denom;
         if ((rdp.rm & ZMODE_DECAL) == ZMODE_DECAL)
         {
            // Calculate deltaZ per polygon for Decal z-mode
            float fdzdy = (diffz_02 * diffx_12 - diffz_12 * diffx_02) / denom;
            float fdz = fabs(fdzdx) + fabs(fdzdy);
            if ((settings.hacks & hack_Zelda) && (rdp.rm & 0x800))
               fdz *= 4.0;  // Decal mode in Zelda sometimes needs mutiplied deltaZ to work correct, e.g. roads
            deltaZ = max(8, (int)fdz);
         }
         dzdx = (int)(fdzdx * 65536.0); }
   }

   org_vtx = vtx;

   for (i = 0; i < 3; i++)
   {
      VERTEX *v = vtx[i];

      if (v->uv_calculated != rdp.tex_ctr)
      {
         //FRDP(" * CALCULATING VERTEX U/V: %d\n", v->number);
         v->uv_calculated = rdp.tex_ctr;

         if (!(rdp.geom_mode & G_LIGHTING))
         {
            if (!(rdp.geom_mode & UPDATE_SCISSOR))
            {
               if (rdp.geom_mode & G_SHADE)
                  glideSetVertexFlatShading(v, vtx, rdp.cmd1);
               else
                  glideSetVertexPrimShading(v, rdp.prim_color);
            }
         }

         // Fix texture coordinates
         if (!v->uv_scaled)
         {
            v->ou *= rdp.tiles[rdp.cur_tile].s_scale;
            v->ov *= rdp.tiles[rdp.cur_tile].t_scale;
            v->uv_scaled = 1;
            if (!rdp.Persp_en)
            {
               //          v->oow = v->w = 1.0f;
               v->ou *= 0.5f;
               v->ov *= 0.5f;
            }
         }
         v->u1 = v->u0 = v->ou;
         v->v1 = v->v0 = v->ov;

         if (rdp.tex >= 1 && rdp.cur_cache[0])
         {
#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[0])
            {
               v->u0 += rdp.aTBuffTex[0]->u_shift + rdp.aTBuffTex[0]->tile_uls;
               v->v0 += rdp.aTBuffTex[0]->v_shift + rdp.aTBuffTex[0]->tile_ult;
            }
#endif

            if (rdp.tiles[rdp.cur_tile].shift_s)
            {
               if (rdp.tiles[rdp.cur_tile].shift_s > 10)
                  v->u0 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile].shift_s));
               else
                  v->u0 /= (float)(1 << rdp.tiles[rdp.cur_tile].shift_s);
            }
            if (rdp.tiles[rdp.cur_tile].shift_t)
            {
               if (rdp.tiles[rdp.cur_tile].shift_t > 10)
                  v->v0 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile].shift_t));
               else
                  v->v0 /= (float)(1 << rdp.tiles[rdp.cur_tile].shift_t);
            }

#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[0])
            {
               if (rdp.aTBuffTex[0]->tile_uls != (int)rdp.tiles[rdp.cur_tile].f_ul_s)
                  v->u0 -= rdp.tiles[rdp.cur_tile].f_ul_s;
               if (rdp.aTBuffTex[0]->tile_ult != (int)rdp.tiles[rdp.cur_tile].f_ul_t || (settings.hacks&hack_Megaman))
                  v->v0 -= rdp.tiles[rdp.cur_tile].f_ul_t; //required for megaman (boss special attack)
               v->u0 *= rdp.aTBuffTex[0]->u_scale;
               v->v0 *= rdp.aTBuffTex[0]->v_scale;
#ifdef EXTREME_LOGGING
               FRDP("tbuff_tex t0: (%f, %f)->(%f, %f)\n", v->ou, v->ov, v->u0, v->v0);
#endif
            }
            else
#endif
            {
               v->u0 -= rdp.tiles[rdp.cur_tile].f_ul_s;
               v->v0 -= rdp.tiles[rdp.cur_tile].f_ul_t;
               v->u0 = rdp.cur_cache[0]->c_off + rdp.cur_cache[0]->c_scl_x * v->u0;
               v->v0 = rdp.cur_cache[0]->c_off + rdp.cur_cache[0]->c_scl_y * v->v0;
            }
            v->u0_w = v->u0 / v->w;
            v->v0_w = v->v0 / v->w;
         }

         if (rdp.tex >= 2 && rdp.cur_cache[1])
         {
#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[1])
            {
               v->u1 += rdp.aTBuffTex[1]->u_shift + rdp.aTBuffTex[1]->tile_uls;
               v->v1 += rdp.aTBuffTex[1]->v_shift + rdp.aTBuffTex[1]->tile_ult;
            }
#endif
            if (rdp.tiles[rdp.cur_tile+1].shift_s)
            {
               if (rdp.tiles[rdp.cur_tile+1].shift_s > 10)
                  v->u1 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile+1].shift_s));
               else
                  v->u1 /= (float)(1 << rdp.tiles[rdp.cur_tile+1].shift_s);
            }
            if (rdp.tiles[rdp.cur_tile+1].shift_t)
            {
               if (rdp.tiles[rdp.cur_tile+1].shift_t > 10)
                  v->v1 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile+1].shift_t));
               else
                  v->v1 /= (float)(1 << rdp.tiles[rdp.cur_tile+1].shift_t);
            }

#ifdef HAVE_HWFBE
            if (rdp.aTBuffTex[1])
            {
               if (rdp.aTBuffTex[1]->tile_uls != (int)rdp.tiles[rdp.cur_tile].f_ul_s)
                  v->u1 -= rdp.tiles[rdp.cur_tile].f_ul_s;
               v->u1 *= rdp.aTBuffTex[1]->u_scale;
               v->v1 *= rdp.aTBuffTex[1]->v_scale;
#ifdef EXTREME_LOGGING
               FRDP("tbuff_tex t1: (%f, %f)->(%f, %f)\n", v->ou, v->ov, v->u1, v->v1);
#endif
            }
            else
#endif
            {
               v->u1 -= rdp.tiles[rdp.cur_tile+1].f_ul_s;
               v->v1 -= rdp.tiles[rdp.cur_tile+1].f_ul_t;
               v->u1 = rdp.cur_cache[1]->c_off + rdp.cur_cache[1]->c_scl_x * v->u1;
               v->v1 = rdp.cur_cache[1]->c_off + rdp.cur_cache[1]->c_scl_y * v->v1;
            }

            v->u1_w = v->u1 / v->w;
            v->v1_w = v->v1 / v->w;
         }
         //      FRDP(" * CALCULATING VERTEX U/V: %d  u0: %f, v0: %f, u1: %f, v1: %f\n", v->number, v->u0, v->v0, v->u1, v->v1);
      }
#ifdef EXTREME_LOGGING
      FRDP("draw_tri. v[%d] ou=%f, ov = %f\n", i, v->ou, v->ov);
#endif
      if (v->shade_mod != cmb.shade_mod_hash)
         apply_shade_mods (v);
   } //for

   rdp.clip = 0;

   if ((vtx[0]->scr_off & 16) ||
         (vtx[1]->scr_off & 16) ||
         (vtx[2]->scr_off & 16))
      rdp.clip |= CLIP_WMIN;

   vtx[0]->not_zclipped = vtx[1]->not_zclipped = vtx[2]->not_zclipped = 1;

   // Set vertex buffers
   rdp.vtxbuf = rdp.vtx1;  // copy from v to rdp.vtx1
   rdp.vtxbuf2 = rdp.vtx2;
   rdp.vtx_buffer = 0;
   rdp.n_global = 3;

   rdp.vtxbuf[0] = *vtx[0];
   rdp.vtxbuf[0].number = 1;
   rdp.vtxbuf[1] = *vtx[1];
   rdp.vtxbuf[1].number = 2;
   rdp.vtxbuf[2] = *vtx[2];
   rdp.vtxbuf[2].number = 4;

   do_triangle_stuff (linew, false);
}

static void cull_trianglefaces(VERTEX **v, unsigned iterations, bool do_update, bool do_cull, int32_t wd)
{
   int32_t i, vcount;
   bool updated_once = false;
   vcount = 0;

   for (i = 0; i < iterations; i++)
   {
      if (do_cull && !cull_tri(v + vcount))
      {
         if (do_update && !updated_once)
         {
            update();
            updated_once = true;
         }
         draw_tri (v + vcount, wd);
      }
      else if (!do_cull)
         draw_tri (v + vcount, wd);
      rdp.tri_n ++;
      vcount += 3;
   }
}

/*
 * Generates one line with the specified width using the vertices
 * v0, v1 loaded in the internal vertex buffer by the gSPVertex
 * macro.
 *
 * The line width wd is specified in half-pixel units. Note that when
 * the line is actually generated, 1.5 pixel (the minimum line width)
 * is added to this specified width. In other words, specifying 0
 * half-pixels draws a 1.5 pixel-wide line, specifying 1 half-pixel
 * (0.5-pixel) draws a 2.0 pixel-wide line, etc.
 *
 * For other features of this macro, see GSPLine3D. This macro works
 * only when line microcode (L3DEX, L3DEX2) is loaded.
 *
 * v0   - vertex buffer index of the first coordinate (0-31).
 * v1   - vertex buffer index of the second coordinate (0-31).
 * wd   - Line width (0~255, in half-pixel units).
 * flag - The line flat-shading index (0~1):
 *        0 - Draws line using the v0 color.
 *        1 - Draws line using the v1 color.
 */
static void gSPLineW3D(int32_t v0, int32_t v1, int32_t wd, int32_t flag)
{
   VERTEX *v[3];
   v[0] = &rdp.vtx[v1];
   v[1] = &rdp.vtx[v0];
   v[2] = &rdp.vtx[v0];

   uint32_t cull_mode = (rdp.flags & CULLMASK) >> CULLSHIFT;
   rdp.flags |= CULLMASK;
   rdp.update |= UPDATE_CULL_MODE;

   cull_trianglefaces(v, 1, true, true, wd);

   rdp.flags ^= CULLMASK;
   rdp.flags |= cull_mode << CULLSHIFT;
   rdp.update |= UPDATE_CULL_MODE;

   //FRDP("uc0:line3d v0:%d, v1:%d, width:%d\n", v0, v1, wd);
}

/*
 * Draws 1 triangle.
 *
 * Generates a single triangle face (using the vertices v0, v1, and v2) in the
 * internal vertex buffer loaded by the gSPvertex macro. The flag (0, 1, or 2)
 * identifies which of the three vertices contains the normal or the color for the
 * face (for flat shading).
 *
 * v0   - vertex buffer index of the first coordinate (variable ranges per microcode).
 * v1   - vertex buffer index of the second coordinate (variable ranges per microcode).
 * v2   - vertex buffer index of the third coordinate (variable ranges per microcode).
 * flag - used for flat shading; ordinal ID of the vertex parameter to use for 
 *        shading - triangle face flag 0~2
 *
 * FIXME: not spec-conformant (do_update)
 */
static void gsSP1Triangle(int32_t v0, int32_t v1, int32_t v2, int32_t flag, bool do_update)
{
   if (rdp.skip_drawing)
      return;

   VERTEX *v[3];

   v[0] = &rdp.vtx[v0]; 
   v[1] = &rdp.vtx[v1];
   v[2] = &rdp.vtx[v2];

   cull_trianglefaces(v, 1, do_update, true, 0);

   //FRDP("gsSP1Triangle #%d - %d, %d, %d\n", rdp.tri_n, v1, v2, v3);
}

/*
 * Draws 2 triangles.
 *
 * Generates the first triangle face (using the vertices v00, v01, and v02) in the
 * internal vertex buffer loaded by the gSPvertex macro.
 *
 * Generates the second triangle face (using the vertices v10, v11, and v12) in the
 * internal vertex buffer loaded by the gSPVertex macro. 
 *
 * flag0 (0, 1, or 2)
 * identifies which of the three vertices (v00, v01, v02) contains the normal or the color for the
 * face (for flat shading).
 *
 * v01   - vertex buffer index 1st triangle (variable ranges per microcode).
 * v01   - vertex buffer index triangle (variable ranges per microcode).
 * v02   - vertex buffer index triangle(variable ranges per microcode).
 * flag  - Triangle face flag 0~2 (for v00, v01, v02)
 * v10   - vertex buffer index 2nd triangle (variable ranges per microcode).
 * v11   - vertex buffer index 2nd triangle (variable ranges per microcode).
 * v12   - vertex buffer index 2nd triangle (variable ranges per microcode).
 * flag1 - Triangle face flag 0~2 (for v10, v11, v12)
 *
 */
static void gsSP2Triangles(uint32_t v00, uint32_t v01, uint32_t v02, uint32_t flag0, uint32_t v10, uint32_t v11, uint32_t v12, uint32_t flag1)
{
   VERTEX *v[6];
   
   if (rdp.skip_drawing)
      return;

   v[0] = &rdp.vtx[v00];
   v[1] = &rdp.vtx[v01];
   v[2] = &rdp.vtx[v02];
   v[3] = &rdp.vtx[v10];
   v[4] = &rdp.vtx[v11];
   v[5] = &rdp.vtx[v12];

   cull_trianglefaces(v, 2, true, true, 0);
   //FRDP("uc1:quad3d #%d, #%d\n", rdp.tri_n, rdp.tri_n+1);
}

// Draw four triangle faces
static void gsSP4Triangles(uint32_t v00, uint32_t v01, uint32_t v02, uint32_t flag0,
      uint32_t v10, uint32_t v11, uint32_t v12, uint32_t flag1,
      uint32_t v20, uint32_t v21, uint32_t v22, uint32_t flag2,
      uint32_t v30, uint32_t v31, uint32_t v32, uint32_t flag3
      )
{
   VERTEX *v[12];

   if (rdp.skip_drawing)
      return;

   v[0]  = &rdp.vtx[v00];
   v[1]  = &rdp.vtx[v01];
   v[2]  = &rdp.vtx[v02];
   v[3]  = &rdp.vtx[v10];
   v[4]  = &rdp.vtx[v11];
   v[5]  = &rdp.vtx[v12];
   v[6]  = &rdp.vtx[v20];
   v[7]  = &rdp.vtx[v21];
   v[8]  = &rdp.vtx[v22];
   v[9]  = &rdp.vtx[v30];
   v[10] = &rdp.vtx[v31];
   v[11] = &rdp.vtx[v32];

   cull_trianglefaces(v, 4, true, true, 0);

   //FRDP("uc8:tri4 (#%d - #%d), %d-%d-%d, %d-%d-%d, %d-%d-%d, %d-%d-%d\n", rdp.tri_n, rdp.tri_n+3, ((w0 >> 23) & 0x1F), ((w0 >> 18) & 0x1F), ((((w0 >> 15) & 0x7) << 2) | ((w1 >> 30) &0x3)), ((w0 >> 10) & 0x1F), ((w0 >> 5) & 0x1F), ((w0 >> 0) & 0x1F), ((w1 >> 25) & 0x1F), ((w1 >> 20) & 0x1F), ((w1 >> 15) & 0x1F), ((w1 >> 10) & 0x1F), ((w1 >> 5) & 0x1F), ((w1 >> 0) & 0x1F));
}

//angrylion's macro, helps to cut overflowed values.
#define SIGN16(x) (((x) & 0x8000) ? ((x) | ~0xffff) : ((x) & 0xffff))

/*
 * Tells RDP to draw a textured 2D rectangle. Afterwards you can call the gDPFillRectangle
 * command to fill rectangles with a solid color.
 *
 * The rectangle drawn is iclusive of the coordinates specified in copy mode. In one-cycle
 * or two-cycle mode, the rectangle is drawn exclusive of the bottom and right edges in order to
 * provide proper anti-aliasing in these modes.
 *
 * ul_x - High X coordinate of rectangle (10.2 fixed point format)
 *
 * ul_y - High Y coordinate of rectangle (10.2 fixed point format)
 *
 * lr_x - Low X coordinate of rectangle (10.2 fixed point format)
 *
 * lr_y - Low Y coordinate of rectangle (10.2 fixed point format)
 *
 * tile - The texture tile number that selects one of 8 texture tile descriptors in the RDP.
 *
 * off_x_i - Specify the texture coordinate of the upper-left corner of the 
 *        rectangle (10.5 fixed point format).
 *
 * off_y_i - Specify the texture coordinate of the upper-left corner of the
 *        rectangle (10.5 fixed point format).
 *
 * dsdx - Specifies change in s per change in x (10.5 fixed point format).
 *
 * dtdy - Specifies change in s per change in y (10.5 fixed point format).
 *
 * flip - Specifies whether texture rectangle needs flipping.
 */
static void gSPTextureRectangle(uint32_t ul_x, uint32_t ul_y, uint32_t lr_x, uint32_t lr_y,
      uint32_t tile, int32_t off_x_i, int32_t off_y_i, int32_t _dsdx, int32_t _dtdy,
      uint32_t flip)
{
   uint32_t prev_tile;
   float Z, dsdx, dtdy, s_ul_x, s_lr_x, s_ul_y, s_lr_y, off_size_x, off_size_y;
   int i;

   rdp.texrecting = 1;

   prev_tile = rdp.cur_tile;
   rdp.cur_tile = tile;

   Z = set_sprite_combine_mode ();

   rdp.texrecting = 0;

   if (!rdp.cur_cache[0])
   {
      rdp.cur_tile = prev_tile;
      rdp.tri_n += 2;
      return;
   }

   // ****
   // ** Texrect offset by Gugaman **
   //
   //integer representation of texture coordinate.
   //needed to detect and avoid overflow after shifting
   dsdx = (float)(int16_t)(_dsdx) / 1024.0f;
   dtdy = (float)(int16_t)(_dtdy) / 1024.0f;
   if (off_x_i & 0x8000) //check for sign bit
      off_x_i |= ~0xffff; //make it negative
   //the same as for off_x_i
   if (off_y_i & 0x8000)
      off_y_i |= ~0xffff;

   if (rdp.cycle_mode == G_CYC_COPY)
      dsdx /= 4.0f;

   s_ul_x = ul_x * rdp.scale_x + rdp.offset_x;
   s_lr_x = lr_x * rdp.scale_x + rdp.offset_x;
   s_ul_y = ul_y * rdp.scale_y + rdp.offset_y;
   s_lr_y = lr_y * rdp.scale_y + rdp.offset_y;

   //FRDP("texrect (%.2f, %.2f, %.2f, %.2f), tile: %d, #%d, #%d\n", ul_x, ul_y, lr_x, lr_y, tile, rdp.tri_n, rdp.tri_n+1);
   //FRDP ("(%f, %f) -> (%f, %f), s: (%d, %d) -> (%d, %d)\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y, rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);
   //FRDP("\toff_x: %f, off_y: %f, dsdx: %f, dtdy: %f\n", off_x_i/32.0f, off_y_i/32.0f, dsdx, dtdy);

   if (flip == 0xE5 )
   {
      //texrectflip
      off_size_x = (lr_y - ul_y - 1) * dsdx;
      off_size_y = (lr_x - ul_x - 1) * dtdy;
   }
   else
   {
      off_size_x = (lr_x - ul_x - 1) * dsdx;
      off_size_y = (lr_y - ul_y - 1) * dtdy;
   }

   struct {
      float ul_u, ul_v, lr_u, lr_v;
   } texUV[2]; //struct for texture coordinates

   //calculate texture coordinates
   for (i = 0; i < 2; i++)
   {
      if (rdp.cur_cache[i] && (rdp.tex & (i+1)))
      {
         float sx = 1, sy = 1;
         int x_i = off_x_i, y_i = off_y_i;
         TILE *tile = &rdp.tiles[rdp.cur_tile + i];
         //shifting
         if (tile->shift_s)
         {
            if (tile->shift_s > 10)
            {
               uint8_t iShift = (16 - tile->shift_s);
               x_i <<= iShift;
               sx = (float)(1 << iShift);
            }
            else
            {
               uint8_t iShift = tile->shift_s;
               x_i >>= iShift;
               sx = 1.0f/(float)(1 << iShift);
            }
         }
         if (tile->shift_t)
         {
            if (tile->shift_t > 10)
            {
               uint8_t iShift = (16 - tile->shift_t);
               y_i <<= iShift;
               sy = (float)(1 << iShift);
            }
            else
            {
               uint8_t iShift = tile->shift_t;
               y_i >>= iShift;
               sy = 1.0f/(float)(1 << iShift);
            }
         }

#ifdef HAVE_HWFBE
         if (rdp.aTBuffTex[i]) //hwfbe texture
         {
            float t0_off_x;
            float t0_off_y;
            if (off_x_i + off_y_i == 0)
            {
               t0_off_x = tile->ul_s;
               t0_off_y = tile->ul_t;
            }
            else
            {
               t0_off_x = off_x_i/32.0f;
               t0_off_y = off_y_i/32.0f;
            }
            t0_off_x += rdp.aTBuffTex[i]->u_shift;// + tile->ul_s; //commented for Paper Mario motion blur
            t0_off_y += rdp.aTBuffTex[i]->v_shift;// + tile->ul_t;
            texUV[i].ul_u = t0_off_x * sx;
            texUV[i].ul_v = t0_off_y * sy;

            texUV[i].lr_u = texUV[i].ul_u + off_size_x * sx;
            texUV[i].lr_v = texUV[i].ul_v + off_size_y * sy;

            texUV[i].ul_u *= rdp.aTBuffTex[i]->u_scale;
            texUV[i].ul_v *= rdp.aTBuffTex[i]->v_scale;
            texUV[i].lr_u *= rdp.aTBuffTex[i]->u_scale;
            texUV[i].lr_v *= rdp.aTBuffTex[i]->v_scale;
            FRDP("tbuff_tex[%d] ul_u: %f, ul_v: %f, lr_u: %f, lr_v: %f\n",
                  i, texUV[i].ul_u, texUV[i].ul_v, texUV[i].lr_u, texUV[i].lr_v);
         }
         else //common case
#endif
         {
            //kill 10.5 format overflow by SIGN16 macro
            texUV[i].ul_u = SIGN16(x_i) / 32.0f;
            texUV[i].ul_v = SIGN16(y_i) / 32.0f;

            texUV[i].ul_u -= tile->f_ul_s;
            texUV[i].ul_v -= tile->f_ul_t;

            texUV[i].lr_u = texUV[i].ul_u + off_size_x * sx;
            texUV[i].lr_v = texUV[i].ul_v + off_size_y * sy;

            texUV[i].ul_u = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_x * texUV[i].ul_u;
            texUV[i].lr_u = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_x * texUV[i].lr_u;
            texUV[i].ul_v = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_y * texUV[i].ul_v;
            texUV[i].lr_v = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_y * texUV[i].lr_v;
         }
      }
      else
      {
         texUV[i].ul_u = texUV[i].ul_v = texUV[i].lr_u = texUV[i].lr_v = 0;
      }
   }
   rdp.cur_tile = prev_tile;

   // ****

   FRDP ("  scissor: (%d, %d) -> (%d, %d)\n", rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);

   CCLIP2 (s_ul_x, s_lr_x, texUV[0].ul_u, texUV[0].lr_u, texUV[1].ul_u, texUV[1].lr_u, (float)rdp.scissor.ul_x, (float)rdp.scissor.lr_x);
   CCLIP2 (s_ul_y, s_lr_y, texUV[0].ul_v, texUV[0].lr_v, texUV[1].ul_v, texUV[1].lr_v, (float)rdp.scissor.ul_y, (float)rdp.scissor.lr_y);

   FRDP ("  draw at: (%f, %f) -> (%f, %f)\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y);

   VERTEX vstd[4] = {
      { s_ul_x, s_ul_y, Z, 1.0f, texUV[0].ul_u, texUV[0].ul_v, texUV[1].ul_u, texUV[1].ul_v, {0, 0, 0, 0}, 255 },
      { s_lr_x, s_ul_y, Z, 1.0f, texUV[0].lr_u, texUV[0].ul_v, texUV[1].lr_u, texUV[1].ul_v, {0, 0, 0, 0}, 255 },
      { s_ul_x, s_lr_y, Z, 1.0f, texUV[0].ul_u, texUV[0].lr_v, texUV[1].ul_u, texUV[1].lr_v, {0, 0, 0, 0}, 255 },
      { s_lr_x, s_lr_y, Z, 1.0f, texUV[0].lr_u, texUV[0].lr_v, texUV[1].lr_u, texUV[1].lr_v, {0, 0, 0, 0}, 255 } };

   if ( flip == 0xE5 )
   {
      //texrectflip
      vstd[1].u0 = texUV[0].ul_u;
      vstd[1].v0 = texUV[0].lr_v;
      vstd[1].u1 = texUV[1].ul_u;
      vstd[1].v1 = texUV[1].lr_v;

      vstd[2].u0 = texUV[0].lr_u;
      vstd[2].v0 = texUV[0].ul_v;
      vstd[2].u1 = texUV[1].lr_u;
      vstd[2].v1 = texUV[1].ul_v;
   }

   VERTEX *vptr = vstd;
   int n_vertices = 4;

   AllowShadeMods (vptr, n_vertices);
   for (i=0; i<n_vertices; i++)
      apply_shade_mods (&vptr[i]);

   {
      if (rdp.fog_mode >= FOG_MODE_BLEND)
      {
         float fog;
         if (rdp.fog_mode == FOG_MODE_BLEND)
            fog = 1.0f/max(1, rdp.fog_color&0xFF);
         else
            fog = 1.0f/max(1, (~rdp.fog_color)&0xFF);

         for (i = 0; i < n_vertices; i++)
            vptr[i].f = fog;

         grFogMode (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT);
      }

      ConvertCoordsConvert (vptr, n_vertices);

      grDrawVertexArrayContiguous (GR_TRIANGLE_STRIP, n_vertices, vptr, sizeof(VERTEX));

      rdp.tri_n += 2;
   }
}


static void pre_update(void)
{
   // This is special, not handled in update(), but here
   // Matrix Pre-multiplication idea by Gonetz (Gonetz@ngs.ru)
   if (rdp.update & UPDATE_MULT_MAT)
      gSPCombineMatrices();

   // This is special, not handled in update()
   if (rdp.update & UPDATE_LIGHTS)
   {
      uint32_t l;
      rdp.update ^= UPDATE_LIGHTS;

      // Calculate light vectors
      for (l = 0; l < rdp.num_lights; l++)
      {
         InverseTransformVector(&rdp.light[l].dir[0], rdp.light_vector[l], rdp.model);
         NormalizeVector (rdp.light_vector[l]);
      }
   }
}

/*
 * Loads into the RSP vertex buffer the vertices that will be used by the 
 * gSP1Triangle commands to generate polygons.
 *
 * v  - Segment address of the vertex list  pointer to a list of vertices.
 * n  - Number of vertices (1 - 32).
 * v0 - Starting index in vertex buffer where vertices are to be loaded into.
 */
static void gSPVertex(uint32_t addr, uint32_t n, uint32_t v0)
{
   int i;
   float x, y, z;

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i>>4)];
      int16_t *rdram = (int16_t*)gfx.RDRAM;
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      vert->flags  = ((uint16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 3)^1];
      vert->ou = (float)rdram[(((addr+i) >> 1) + 4)^1];
      vert->ov = (float)rdram[(((addr+i) >> 1) + 5)^1];
      vert->uv_scaled = 0;
      vert->a    = ((uint8_t*)gfx.RDRAM)[(addr+i + 15)^3];

      vert->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      vert->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      vert->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      vert->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
      vert->x_w = vert->x * vert->oow;
      vert->y_w = vert->y * vert->oow;
      vert->z_w = vert->z * vert->oow;
      CalculateFog (vert);

      vert->scr_off = 0;
      if (vert->x < -vert->w)
         vert->scr_off |= 1;
      if (vert->x > vert->w)
         vert->scr_off |= 2;
      if (vert->y < -vert->w)
         vert->scr_off |= 4;
      if (vert->y > vert->w)
         vert->scr_off |= 8;
      if (vert->w < 0.1f)
         vert->scr_off |= 16;
#if 0
      if (vert->z_w > 1.0f)
         vert->scr_off |= 32;
#endif

      if (rdp.geom_mode & G_LIGHTING)
      {
         int8_t *rdram = (int8_t*)gfx.RDRAM;
         vert->vec[0] = rdram[(addr+i + 12)^3];
         vert->vec[1] = rdram[(addr+i + 13)^3];
         vert->vec[2] = rdram[(addr+i + 14)^3];
         if (rdp.geom_mode & G_TEXTURE_GEN)
         {
            if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
               calc_linear (vert);
            else
               calc_sphere (vert);
         }

         if (settings.ucode == 2 && rdp.geom_mode & 0x00400000)
         {
            float tmpvec[3] = {x, y, z};
            calc_point_light (vert, tmpvec);
         }
         else
         {
            NormalizeVector (vert->vec);
            calc_light (vert);
         }
      }
      else
      {
         uint8_t *rdram = (uint8_t*)gfx.RDRAM;
         vert->r = rdram[(addr+i + 12)^3];
         vert->g = rdram[(addr+i + 13)^3];
         vert->b = rdram[(addr+i + 14)^3];
      }
      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
   }
   //FRDP ("rsp:vertex v0:%d, n:%d, from: %08lx\n", v0, n, addr);
}

#ifdef HAVE_NEON
#include <arm_neon.h>

static void gSPVertexNEON(uint32_t addr, uint32_t n, uint32_t v0)
{
   int i;
   float x, y, z;
   float32x4_t comb0, comb1, comb2, comb3;
   float32x4_t v_xyzw;

   comb0 = vld1q_f32(rdp.combined[0]);
   comb1 = vld1q_f32(rdp.combined[1]);
   comb2 = vld1q_f32(rdp.combined[2]);
   comb3 = vld1q_f32(rdp.combined[3]);

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i>>4)];
      int16_t *rdram = (int16_t*)gfx.RDRAM;
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      vert->flags  = ((uint16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 3)^1];
      vert->ou = (float)rdram[(((addr+i) >> 1) + 4)^1];
      vert->ov = (float)rdram[(((addr+i) >> 1) + 5)^1];
      vert->uv_scaled = 0;
      vert->a    = ((uint8_t*)gfx.RDRAM)[(addr+i + 15)^3];

      v_xyzw  = vmulq_n_f32(comb0,x)+vmulq_n_f32(comb1,y)+vmulq_n_f32(comb2,z)+comb3;
      vert->x = vgetq_lane_f32(v_xyzw,0);
      vert->y = vgetq_lane_f32(v_xyzw,1);
      vert->z = vgetq_lane_f32(v_xyzw,2);
      vert->w = vgetq_lane_f32(v_xyzw,3);

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
      v_xyzw = vmulq_n_f32(v_xyzw,vert->oow);
      vert->x_w=vgetq_lane_f32(v_xyzw,0);
      vert->y_w=vgetq_lane_f32(v_xyzw,1);
      vert->z_w=vgetq_lane_f32(v_xyzw,2);
      CalculateFog (vert);

      vert->scr_off = 0;
      if (vert->x < -vert->w)
         vert->scr_off |= 1;
      if (vert->x > vert->w)
         vert->scr_off |= 2;
      if (vert->y < -vert->w)
         vert->scr_off |= 4;
      if (vert->y > vert->w)
         vert->scr_off |= 8;
      if (vert->w < 0.1f)
         vert->scr_off |= 16;
#if 0
      if (vert->z_w > 1.0f)
         vert->scr_off |= 32;
#endif

      if (rdp.geom_mode & G_LIGHTING)
      {
         int8_t *rdram = (int8_t*)gfx.RDRAM;
         vert->vec[0] = rdram[(addr+i + 12)^3];
         vert->vec[1] = rdram[(addr+i + 13)^3];
         vert->vec[2] = rdram[(addr+i + 14)^3];
         if (rdp.geom_mode & G_TEXTURE_GEN)
         {
            if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
               calc_linear (vert);
            else
               calc_sphere (vert);
         }

         if (settings.ucode == 2 && rdp.geom_mode & 0x00400000)
         {
            float tmpvec[3] = {x, y, z};
            calc_point_light (vert, tmpvec);
         }
         else
         {
            NormalizeVector (vert->vec);
            calc_light (vert);
         }
      }
      else
      {
         uint8_t *rdram = (uint8_t*)gfx.RDRAM;
         vert->r = rdram[(addr+i + 12)^3];
         vert->g = rdram[(addr+i + 13)^3];
         vert->b = rdram[(addr+i + 14)^3];
      }
      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
   }
   //FRDP ("rsp:vertex v0:%d, n:%d, from: %08lx\n", v0, n, addr);
}
#endif

/*
 * An S2DEX microcode macro that loads the texture data
 * into the uObjTxtr data structure by refering to the
 * texture loading parameters held by three data structures.
 *
 * It must be used before you can draw sprites.
 *
 * It processes the following three different texture types
 * (methods) distinguished by the uObjTxtr structure's type
 * member:
 *
 * - G_OBJLT_TXTRBLOCK - Texture load using LoadBlock
 * - G_OBJLT_TXTRTILE  - Texture load using LoadTile
 * - G_OBJLT_TLUT      - TLUT load
 *
 * Texture loading by using LoadBlock can be faster than
 * texture loading by using LoadTile. However, there is a 
 * limitation to loadable texture width.
 *
 * tx - Address to the texture load data structure
 */
static void gSPObjLoadTxtr(uint32_t tx )
{
   uint32_t addr, type;
   addr = segoffset(tx) >> 1;
   type = ((uint32_t*)gfx.RDRAM)[(addr + 0) >> 1];                      // 0, 1

   rdp.s2dex_tex_loaded = true;
   rdp.update |= UPDATE_TEXTURE;

   switch (type)
   {
      case G_OBJLT_TLUT:
         {
            uint32_t image;
            uint16_t phead, pnum;

            image = segoffset(((uint32_t*)gfx.RDRAM)[(addr + 2) >> 1]);   // 2, 3
            phead = ((uint16_t *)gfx.RDRAM)[(addr + 4) ^ 1] - 256;        // 4
            pnum  = ((uint16_t *)gfx.RDRAM)[(addr + 5) ^ 1] + 1;          // 5

            load_palette (image, phead, pnum);
            //FRDP ("palette addr: %08lx, start: %d, num: %d\n", image, phead, pnum);
         }
         break;
      case G_OBJLT_TXTRBLOCK:
         {
            uint32_t image;
            uint16_t tmem, tsize, tline;
            image = segoffset(((uint32_t*)gfx.RDRAM)[(addr + 2) >> 1]);   // 2, 3
            tmem  = ((uint16_t *)gfx.RDRAM)[(addr + 4) ^ 1];      // 4
            tsize = ((uint16_t *)gfx.RDRAM)[(addr + 5) ^ 1];      // 5
            tline = ((uint16_t *)gfx.RDRAM)[(addr + 6) ^ 1];      // 6

            FRDP ("addr: %08lx, tmem: %08lx, size: %d\n", image, tmem, tsize);
            rdp.timg.addr = image;
            rdp.timg.width = 1;
            rdp.timg.size = 1;

            rdp.tiles[7].t_mem = tmem;
            rdp.tiles[7].size = 1;
            rdp.cmd0 = 0;
            rdp.cmd1 = 0x07000000 | (tsize << 14) | tline;

            if (!rdp.skip_drawing)
               gDPLoadBlock(
                     ((rdp.cmd1 >> 24) & 0x07), 
                     (rdp.cmd0 >> 14) & 0x3FF, /* ul_s */
                     (rdp.cmd0 >>  2) & 0x3FF, /* ul_t */
                     (rdp.cmd1 >> 14) & 0x3FF, /* lr_s */
                     (rdp.cmd1 & 0x0FFF) /* dxt */
                     );
         }
         break;
      case G_OBJLT_TXTRTILE:
         {
            int32_t line;
            uint32_t image;
            uint16_t tmem, twidth, theight;
            image   = segoffset(((uint32_t*)gfx.RDRAM)[(addr + 2) >> 1]);   // 2, 3
            tmem    = ((uint16_t *)gfx.RDRAM)[(addr + 4) ^ 1];      // 4
            twidth  = ((uint16_t *)gfx.RDRAM)[(addr + 5) ^ 1];      // 5
            theight = ((uint16_t *)gfx.RDRAM)[(addr + 6) ^ 1];      // 6

            FRDP ("tile addr: %08lx, tmem: %08lx, twidth: %d, theight: %d\n", image, tmem, twidth, theight);

            line = (twidth + 1) >> 2;

            rdp.timg.addr = image;
            rdp.timg.width = line << 3;
            rdp.timg.size = 1;

            rdp.tiles[7].t_mem = tmem;
            rdp.tiles[7].line = line;
            rdp.tiles[7].size = 1;

            rdp.cmd0 = 0;
            rdp.cmd1 = 0x07000000 | (twidth << 14) | (theight << 2);

            gDPLoadTile(
                  ((rdp.cmd1 >> 24) & 0x07), /* tile */
                  ((rdp.cmd0 >> 14) & 0x03FF), /* ul_s */
                  ((rdp.cmd0 >> 2 ) & 0x03FF), /*ul_t */
                  ((rdp.cmd1 >> 14) & 0x03FF), /* lr_s */
                  ((rdp.cmd1 >> 2 ) & 0x03FF) /* lr_t */
                  );
         }
         break;
   }
   //LRDP("uc6:obj_loadtxtr ");
}

/*
 * Draws rotating spites using the data in the 2D matrix.
 * One of the three sprite-drawing macros that are part of
 * the S2DEX microcode.
 * FIXME - not spec conformant.
 * */
static void gSPObjSprite(void)
{
   int i;
   float Z, ul_x, lr_x, ul_y, lr_y, ul_u, ul_v, lr_u, lr_v;
   DRAWOBJECT d;

   //LRDP ("uc6:obj_sprite ");

   uc6_read_object_data(&d);
   uc6_init_tile(&d);

   Z = set_sprite_combine_mode();

   ul_x = d.objX;
   lr_x = d.objX + d.imageW / d.scaleW;
   ul_y = d.objY;
   lr_y = d.objY + d.imageH / d.scaleH;
   lr_u = 255.0f * rdp.cur_cache[0]->scale_x;
   lr_v = 255.0f * rdp.cur_cache[0]->scale_y;

   ul_u = 0.5f;
   ul_v = 0.5f;
   if (d.imageFlags & G_BG_FLAG_FLIPS) /* flipS */
   {
      ul_u = lr_u;
      lr_u = 0.5f;
   }
   if (d.imageFlags & G_BG_FLAG_FLIPT) /* flipT */
   {
      ul_v = lr_v;
      lr_v = 0.5f;
   }

   // Make the vertices
   //    FRDP("scale_x: %f, scale_y: %f\n", rdp.cur_cache[0]->scale_x, rdp.cur_cache[0]->scale_y);

   VERTEX v[4] = {
      { ul_x, ul_y, Z, 1, ul_u, ul_v },
      { lr_x, ul_y, Z, 1, lr_u, ul_v },
      { ul_x, lr_y, Z, 1, ul_u, lr_v },
      { lr_x, lr_y, Z, 1, lr_u, lr_v }
   };

   for (i = 0; i < 4; i++)
   {
      float x = v[i].x;
      float y = v[i].y;
      v[i].x = (x * mat_2d.A + y * mat_2d.B + mat_2d.X) * rdp.scale_x;
      v[i].y = (x * mat_2d.C + y * mat_2d.D + mat_2d.Y) * rdp.scale_y;
   }

   uc6_draw_polygons (v);
}

/*
 * Performs the texture load operation and then
 * draws a rotating sprite by using the data stored
 * in the 2D matrix.
 *
 * It is one of the three compound-processing macros
 * that are part of the S2DEX microcode. Essentially,
 * this macro does the work of two macros (gSPObjLoadTxtr
 * and gSPObjSprite) with one macro
 *
 * txsp - pointer to theUObjTxSprite structure that holds
 *        the texture loading and sprite drawing data
 */
static void gSPObjLoadTxSprite(uint32_t txsp)
{
   gSPObjLoadTxtr(txsp);
   rdp.cmd1 = txsp + 24;
   gSPObjSprite();
   //LRDP("uc6:obj_ldtx_sprite\n");
}

/* 
 * An S2DEX microcode macro that loads the
 * 2D matrix data that exists in the uObjMtx data
 * structure to the 2D matrix area in the RSP.
 */
static void gSPObjMatrix(uint32_t mtx)
{
  uint32_t addr = segoffset(mtx) >> 1;
  
  mat_2d.A = ((int*)gfx.RDRAM)[(addr+0)>>1] / 65536.0f;
  mat_2d.B = ((int*)gfx.RDRAM)[(addr+2)>>1] / 65536.0f;
  mat_2d.C = ((int*)gfx.RDRAM)[(addr+4)>>1] / 65536.0f;
  mat_2d.D = ((int*)gfx.RDRAM)[(addr+6)>>1] / 65536.0f;
  mat_2d.X = ((int16_t*)gfx.RDRAM)[(addr+8)^1] / 4.0f;
  mat_2d.Y = ((int16_t*)gfx.RDRAM)[(addr+9)^1] / 4.0f;
  mat_2d.BaseScaleX = ((uint16_t*)gfx.RDRAM)[(addr+10)^1] / 1024.0f;
  mat_2d.BaseScaleY = ((uint16_t*)gfx.RDRAM)[(addr+11)^1] / 1024.0f;

  //FRDP ("mat_2d\nA: %f, B: %f, c: %f, D: %f\nX: %f, Y: %f\nBaseScaleX: %f, BaseScaleY: %f\n", mat_2d.A, mat_2d.B, mat_2d.C, mat_2d.D, mat_2d.X, mat_2d.Y, mat_2d.BaseScaleX, mat_2d.BaseScaleY);
}

/* 
 * An S2DEX microcode macro that loads the
 * 2D matrix data that exists in the uObjMtx data
 * structure to the 2D submatrix area in the RSP.
 */
static void gSPObjSubMatrix(uint32_t mtx)
{
   uint32_t addr = segoffset(mtx) >> 1;

   mat_2d.X = ((int16_t*)gfx.RDRAM)[(addr+0)^1] / 4.0f;
   mat_2d.Y = ((int16_t*)gfx.RDRAM)[(addr+1)^1] / 4.0f;
   mat_2d.BaseScaleX = ((uint16_t*)gfx.RDRAM)[(addr+2)^1] / 1024.0f;
   mat_2d.BaseScaleY = ((uint16_t*)gfx.RDRAM)[(addr+3)^1] / 1024.0f;

   //FRDP ("submatrix\nX: %f, Y: %f\nBaseScaleX: %f, BaseScaleY: %f\n", mat_2d.X, mat_2d.Y, mat_2d.BaseScaleX, mat_2d.BaseScaleY);
}

/*
 * Sets or clears the RDP 'othermode'.
 *
 * It is the underlying macro used by the RSP geometry engine to
 * update and cache the RDP rendering state. All of the RDP state-setting
 * macros are built using this macro.
 *
 * cmd - Can be set to either G_SETOTHERMODE_H or G_SETOTHERMODE_L.
 * sft - Shift value to create the mask with.
 * len - Length of field (mask)
 * data - Data to set or clear (TODO - stub for now)
 */
static void gSPSetOtherMode(int32_t cmd, int32_t sft, int32_t len, uint32_t data)
{
   uint32_t mask = 0;
   int i = len;
   for (; i; i--)
      mask = (mask << 1) | 1;
   mask <<= sft;

   rdp.cmd1 &= mask;

   switch (cmd)
   {
      case G_SETOTHERMODE_H:
         rdp.othermode_h &= ~mask;
         rdp.othermode_h |= rdp.cmd1;

         if (mask & 0x00000030)  // alpha dither mode
         {
            rdp.alpha_dither_mode = (rdp.othermode_h >> G_MDSFT_ALPHADITHER) & 0x3;
            //FRDP ("alpha dither mode: %s\n", str_dither[rdp.alpha_dither_mode]);
         }

#ifndef NDEBUG
         if (mask & 0x000000C0)  // rgb dither mode
         {
            uint32_t dither_mode = (rdp.othermode_h >> G_MDSFT_RGBDITHER) & 0x3;
            //FRDP ("rgb dither mode: %s\n", str_dither[dither_mode]);
         }
#endif

         if (mask & 0x00003000)  // filter mode
         {
            rdp.filter_mode = (int)((rdp.othermode_h & 0x00003000) >> 12);
            rdp.update |= UPDATE_TEXTURE;
            //FRDP ("filter mode: %s\n", str_filter[rdp.filter_mode]);
         }

         if (mask & 0x0000C000)  // tlut mode
         {
            rdp.tlut_mode = (uint8_t)((rdp.othermode_h & 0x0000C000) >> 14);
            //FRDP ("tlut mode: %s\n", str_tlut[rdp.tlut_mode]);
         }

         if (mask & 0x00300000)  // cycle type
         {
            rdp.cycle_mode = (uint8_t)((rdp.othermode_h & 0x00300000) >> 20);
            rdp.update |= UPDATE_ZBUF_ENABLED;
            //FRDP ("cycletype: %d\n", rdp.cycle_mode);
         }

         if (mask & G_LOD)  // LOD enable
         {
            rdp.LOD_en = (rdp.othermode_h & G_LOD) ? true : false;
            //FRDP ("LOD_en: %d\n", rdp.LOD_en);
         }

         if (mask & G_TEXTURE_GEN_LINEAR)  // Persp enable
         {
            if (rdp.persp_supported)
               rdp.Persp_en = (rdp.othermode_h & G_TEXTURE_GEN_LINEAR) ? true : false;
            //FRDP ("Persp_en: %d\n", rdp.Persp_en);
         }

#ifndef NDEBUG
         if (mask & 0x0FFC60F0F)  // unknown portions, LARGE
         {
            //FRDP ("UNKNOWN PORTIONS: shift: %d, len: %d, unknowns: %08lx\n", shift, len, unk);
         }
#endif
         break;
      case G_SETOTHERMODE_L:
         rdp.othermode_l &= ~mask;
         rdp.othermode_l |= rdp.cmd1;

         if (mask & 0x00000003)
            gDPSetAlphaCompare(rdp.cmd1 >> G_MDSFT_ALPHACOMPARE);

         if (mask & ZBUF_COMPARE)
            gDPSetDepthSource(rdp.cmd1 >> G_MDSFT_ZSRCSEL);

         if (mask & 0xFFFFFFF8) // rendermode / blender bits
            gDPSetRenderMode(rdp.cmd1 & 0xCCCCFFFF, rdp.cmd1 & 0x3333FFFF);
         break;
   }
}

/*
 * Update the matrix element without using multiplication.
 */
static void gSPInsertMatrix(uint32_t w0, uint32_t w1)
{
   int32_t index_x, index_y;
   index_x = (w0 & 0x1F) >> 1;
   index_y = index_x >> 2;
   index_x &= 3;

   // do matrix pre-mult so it's re-updated next time
   if (rdp.update & UPDATE_MULT_MAT)
      gSPCombineMatrices();

   rdp.combined[index_y][index_x] = (int16_t)(w1 >> 16);
   rdp.combined[index_y][index_x+1] = (int16_t)(w1 & 0xFFFF);

   if (w0 & 0x20)  // fractional part
   {
      rdp.combined[index_y][index_x] /= 65536.0f;
      rdp.combined[index_y][index_x] += (float)(int)rdp.combined[index_y][index_x];

      rdp.combined[index_y][index_x+1] /= 65536.0f;
      rdp.combined[index_y][index_x+1] += (float)(int)rdp.combined[index_y][index_x+1];
   }

   //LRDP("matrix\n");
}

static void gSPDMATriangles(uint32_t tris, uint32_t n)
{
   int32_t i, start, v0, v1, v2, flags;
   uint32_t addr = segoffset(tris) & BMASK;

   for (i = 0; i < n; i++)
   {
      VERTEX *v[3];

      start = i << 4;
      v0 = gfx.RDRAM[addr+start];
      v1 = gfx.RDRAM[addr+start+1];
      v2 = gfx.RDRAM[addr+start+2];

      v[0] = &rdp.vtx[v0];
      v[1] = &rdp.vtx[v1];
      v[2] = &rdp.vtx[v2];

      flags = gfx.RDRAM[addr+start+3];

      if (flags & 0x40)
      { // no cull
         rdp.flags &= ~CULLMASK;
         grCullMode (GR_CULL_DISABLE);
      }
      else
      {        // front cull
         rdp.flags &= ~CULLMASK;
         if (rdp.view_scale[0] < 0)
         {
            rdp.flags |= CULL_BACK;   // agh, backwards culling
            grCullMode (GR_CULL_POSITIVE);
         }
         else
         {
            rdp.flags |= CULL_FRONT;
            grCullMode (GR_CULL_NEGATIVE);
         }
      }
      start += 4;

      v[0]->ou = ((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 5] / 32.0f;
      v[0]->ov = ((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 4] / 32.0f;
      v[1]->ou = ((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 3] / 32.0f;
      v[1]->ov = ((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 2] / 32.0f;
      v[2]->ou = ((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 1] / 32.0f;
      v[2]->ov = ((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 0] / 32.0f;

      v[0]->uv_calculated = 0xFFFFFFFF;
      v[1]->uv_calculated = 0xFFFFFFFF;
      v[2]->uv_calculated = 0xFFFFFFFF;

      gsSP1Triangle(v0, v1, v2, 0, true);
   }
}

static void gSPDMAMatrix(uint32_t matrix, uint8_t index, uint8_t multiply)
{
   uint32_t addr = dma_offset_mtx + (segoffset(matrix) & BMASK);

   if (multiply)
   {
      DECLAREALIGN16VAR(m[4][4]);
      load_matrix(m, addr);
      DECLAREALIGN16VAR(m_src[4][4]);
      memcpy (m_src, rdp.dkrproj[0], 64);
      MulMatrices(m, m_src, rdp.dkrproj[index]);
   }
   else
      load_matrix(rdp.dkrproj[index], addr);

   rdp.update |= UPDATE_MULT_MAT;
}

static void gSPDMAVertex(uint32_t v, uint32_t n, uint32_t v0)
{
   int32_t prj, start, i;
   float x, y, z;
   uint32_t addr = dma_offset_vtx + (segoffset(v) & BMASK);

   prj = cur_mtx;
   start = 0;

   for (i = v0; i < v0 + n; i++)
   {
      start = (i-v0) * 10;
      VERTEX *v = &rdp.vtx[i];
      x   = ((int16_t*)gfx.RDRAM)[(((addr+start) >> 1) + 0)^1];
      y   = ((int16_t*)gfx.RDRAM)[(((addr+start) >> 1) + 1)^1];
      z   = ((int16_t*)gfx.RDRAM)[(((addr+start) >> 1) + 2)^1];

      v->x = x*rdp.dkrproj[prj][0][0] + y*rdp.dkrproj[prj][1][0] + z*rdp.dkrproj[prj][2][0] + rdp.dkrproj[prj][3][0];
      v->y = x*rdp.dkrproj[prj][0][1] + y*rdp.dkrproj[prj][1][1] + z*rdp.dkrproj[prj][2][1] + rdp.dkrproj[prj][3][1];
      v->z = x*rdp.dkrproj[prj][0][2] + y*rdp.dkrproj[prj][1][2] + z*rdp.dkrproj[prj][2][2] + rdp.dkrproj[prj][3][2];
      v->w = x*rdp.dkrproj[prj][0][3] + y*rdp.dkrproj[prj][1][3] + z*rdp.dkrproj[prj][2][3] + rdp.dkrproj[prj][3][3];

      if (billboarding)
      {
         v->x += rdp.vtx[0].x;
         v->y += rdp.vtx[0].y;
         v->z += rdp.vtx[0].z;
         v->w += rdp.vtx[0].w;
      }

      if (fabs(v->w) < 0.001)
         v->w = 0.001f;

      v->oow = 1.0f / v->w;
      v->x_w = v->x * v->oow;
      v->y_w = v->y * v->oow;
      v->z_w = v->z * v->oow;

      v->uv_calculated = 0xFFFFFFFF;
      v->screen_translated = 0;
      v->shade_mod = 0;

      v->scr_off = 0;
      if (v->x < -v->w)
         v->scr_off |= 1;
      if (v->x > v->w)
         v->scr_off |= 2;
      if (v->y < -v->w)
         v->scr_off |= 4;
      if (v->y > v->w)
         v->scr_off |= 8;
      if (v->w < 0.1f)
         v->scr_off |= 16;
      if (fabs(v->z_w) > 1.0)
         v->scr_off |= 32;

      v->r = ((uint8_t*)gfx.RDRAM)[(addr+start + 6)^3];
      v->g = ((uint8_t*)gfx.RDRAM)[(addr+start + 7)^3];
      v->b = ((uint8_t*)gfx.RDRAM)[(addr+start + 8)^3];
      v->a = ((uint8_t*)gfx.RDRAM)[(addr+start + 9)^3];
      CalculateFog (v);

      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i, v->x, v->y, v->z, v->w, v->z_w, v->r, v->g, v->b, v->a);
   }
}

static INLINE void gSPSetDMAOffsets(uint32_t mtxoffset, uint32_t vtxoffset)
{
   dma_offset_mtx = mtxoffset;
   dma_offset_vtx = vtxoffset;
}

static INLINE void gSPInterpolateVertex(VERTEX *dest, float percent, VERTEX *first, VERTEX *second)
{
   dest->r = first->r + percent * (second->r - first->r);
   dest->g = first->g + percent * (second->g - first->g);
   dest->b = first->b + percent * (second->b - first->b);
   dest->a = first->a + percent * (second->a - first->a);
   dest->f = first->f + percent * (second->f - first->f);
}
