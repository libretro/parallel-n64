/*
Copyright (C) 2003 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "Blender.h"
#include "Config.h"
#include "RenderBase.h"
#include "../../Graphics/GBI.h"

#include "../../Graphics/RDP/gDP_state.h"


#define BLEND_NOOP              0x0000

#define BLEND_NOOP5             0xcc48  /* Fog * 0 + Mem * 1 */
#define BLEND_NOOP4             0xcc08  /* Fog * 0 + In * 1 */
#define BLEND_FOG_ASHADE        0xc800
#define BLEND_FOG_3             0xc000  /* Fog * AIn + In * 1-A */
#define BLEND_FOG_MEM           0xc440  /* Fog * AFog + Mem * 1-A */
#define BLEND_FOG_APRIM         0xc400  /* Fog * AFog + In * 1-A */

#define BLEND_BLENDCOLOR        0x8c88
#define BLEND_BI_AFOG           0x8400  /* Bl * AFog + In * 1-A */
#define BLEND_BI_AIN            0x8040  /* Bl * AIn + Mem * 1-A */

#define BLEND_MEM               0x4c40  /* Mem*0 + Mem*(1-0)?! */
#define BLEND_FOG_MEM_3         0x44c0  /* Mem * AFog + Fog * 1-A */

#define BLEND_NOOP3             0x0c48  /* In * 0 + Mem * 1 */
#define BLEND_PASS              0x0c08  /* In * 0 + In * 1 */
#define BLEND_FOG_MEM_IN_MEM    0x0440  /* In * AFog + Mem * 1-A */
#define BLEND_FOG_MEM_FOG_MEM   0x04c0  /* In * AFog + Fog * 1-A */
#define BLEND_OPA               0x0044  /* In * AIn + Mem * AMem */
#define BLEND_XLU               0x0040
#define BLEND_MEM_ALPHA_IN      0x4044  /* Mem * AIn + Mem * AMem */

/* ======================================================================== */
void Blender_InitBlenderMode(CBlender *b)                    /* Set Alpha Blender mode */
{
   /* 1. Z_COMPARE        -- Enable / Disable Z-Buffer compare */
   /* 1   -   Enable ZBuffer */
   /* 0   -   Disable ZBuffer */

   /* 2. Z_UPDATE        -- Enable / Disable Z-Buffer update */
   /* 1   -   Enable ZBuffer writeable */
   /* 0   -   Z-buffer not writeable */

   /* 3. AA_EN and IM_RD        -- Anti-Alias */
   /* AA_EN           -   Enable anti-aliasing */
   /* AA_EN | IM_RD   -   Reduced anti-aliasing */
   /* IM_RD           -   ?? */
   /* -               -   Disable anti-aliasing */

   /* 4.  ZMode */
   /* #define ZMODE_OPA   0           -- Usually used with Z_COMPARE and Z_UPDATE */
   /* or used without neither Z_COMPARE or Z_UPDATE */
   /* if used with Z_COMPARE and Z_UPDATE, then this is */
   /* the regular ZBuffer mode, with compare and update */
   
   /* #define ZMODE_INTER 0x400 */
   /* #define ZMODE_XLU   0x800       -- Usually used with Z_COMPARE, but not with Z_UPDATE */
   /* Do only compare, no Z-buffer update. */
   /* Not output if the z value is the same */
   
   /* #define ZMODE_DEC   0xc00       -- Usually used with Z_COMPARE, but not with Z_UPDATE */
   /* Do only compare, no update, but because this is */
   /* decal mode, so image should be updated even */
   /* the z value is the same as compared. */

   /*  Alpha Blender Modes */

   
   /* 6. FORCE_BL     - Alpha blending at blender stage */
   /* 1   -   Enable alpha blending at blender */
   /* 0   -   Disable alpha blending at blender */
   
   /* Alpha blending at blender is usually used to render XLU surface */
   /* if enabled, then use the blending setting of C1 and C2 */

   /* 7. ALPHA_CVG_SEL    - Output full alpha from the color combiner, usually not used together */
   /* with FORCE_BL. If it is used together with FORCE_BL, then ignore this */

   /* 8. CVG_X_ALPHA      - Before output the color from color combiner, mod it with alpha */

   /* 9. TEX_EDGE         - Ignore this */

   /* 10.CLR_ON_CVG       - Used with XLU surfaces, ignore it */

   /* 11.CVG_DST */
   /* #define CVG_DST_CLAMP   0       -   Usually used with OPA surface */
   /* #define CVG_DST_WRAP    0x100   -   Usually used with XLU surface or OPA line */
   /* #define CVG_DST_FULL    0x200   -   ? */
   /* #define CVG_DST_SAVE    0x300   -   ? */
   
   /* Possible Blending Inputs: */
   
   /* In  -   Input from color combiner */
   /* Mem -   Input from current frame buffer */
   /* Fog -   Fog generator */
   /* BL  -   Blender */
   
   /* Possible Blending Factors: */
   /* A-IN    -   Alpha from color combiner */
   /* A-MEM   -   Alpha from current frame buffer */
   /* (1-A)   - */
   /* A-FOG   -   Alpha of fog color */
   /* A-SHADE -   Alpha of shade */
   /* 1   -   1 */
   /* 0   -   0 */
   uint32_t blendmode_1 = (uint32_t)( gRDP.otherMode.blender & 0xcccc );
   uint32_t blendmode_2 = (uint32_t)( gRDP.otherMode.blender & 0x3333 );
   uint32_t cycletype   = gRDP.otherMode.cycle_type;

   switch( cycletype )
   {
      case G_CYC_FILL:
         /* b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO); */
         /* b->ops->Enable(b); */
         b->ops->Disable(b);
         break;
      case G_CYC_COPY:
         /* b->ops->Disable(b); */
         b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
         b->ops->Enable(b);
         break;
      case G_CYC_2CYCLE:
         if( gRDP.otherMode.force_bl && gRDP.otherMode.z_cmp )
         {
            b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
            b->ops->Enable(b);
            break;
         }

         /*
            if( gRDP.otherMode.alpha_cvg_sel && gRDP.otherMode.cvg_x_alpha==0 )
            {
            b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
            b->ops->Enable(b);
            break;
            }
            */

         switch( blendmode_1+blendmode_2 )
         {
            case BLEND_PASS+(BLEND_PASS>>2):    /* In * 0 + In * 1 */
            case BLEND_FOG_APRIM+(BLEND_PASS>>2):
               b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               if( gRDP.otherMode.alpha_cvg_sel )
               {
                  b->ops->Enable(b);
               }
               else
               {
                  b->ops->Disable(b);
               }

               RiceRender_SetAlphaTestEnable(((gDP.otherMode.l >> G_MDSFT_ALPHACOMPARE) & 0x3) == 1);
               break;
            case BLEND_PASS+(BLEND_OPA>>2):
               /* 0x0c19 */
               /* Cycle1:  In * 0 + In * 1 */
               /* Cycle2:  In * AIn + Mem * AMem */
               if( gRDP.otherMode.cvg_x_alpha && gRDP.otherMode.alpha_cvg_sel )
               {
                  b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
                  b->ops->Enable(b);
               }
               else
               {
                  b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
                  b->ops->Enable(b);
               }
               break;
            case BLEND_PASS + (BLEND_XLU>>2):
               /* 0x0c18 */
               /* Cycle1:  In * 0 + In * 1 */
               /* Cycle2:  In * AIn + Mem * 1-A */
            case BLEND_FOG_ASHADE + (BLEND_XLU>>2):
               /* Cycle1:   Fog * AShade + In * 1-A */
               /* Cycle2:   In * AIn + Mem * 1-A */
            case BLEND_FOG_APRIM + (BLEND_XLU>>2):
               /* Cycle1:   Fog * AFog + In * 1-A */
               /* Cycle2:   In * AIn + Mem * 1-A */
               /* case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2): */
               /* Cycle1:   In * AFog + Fog * 1-A */
               /* Cycle2:   In * AIn + Mem * AMem */
            case BLEND_FOG_MEM_FOG_MEM + (BLEND_PASS>>2):
               /* Cycle1:   In * AFog + Fog * 1-A */
               /* Cycle2:   In * 0 + In * 1 */
            case BLEND_XLU + (BLEND_XLU>>2):
               /* Cycle1:   Fog * AFog + In * 1-A */
               /* Cycle2:   In * AIn + Mem * 1-A */
            case BLEND_BI_AFOG + (BLEND_XLU>>2):
               /* Cycle1:   Bl * AFog + In * 1-A */
               /* Cycle2:   In * AIn + Mem * 1-A */
            case BLEND_XLU + (BLEND_FOG_MEM_IN_MEM>>2):
               /* Cycle1:   In * AIn + Mem * 1-A */
               /* Cycle2:   In * AFog + Mem * 1-A */
            case BLEND_PASS + (BLEND_FOG_MEM_IN_MEM>>2):
               /* Cycle1:   In * 0 + In * 1 */
               /* Cycle2:   In * AFog + Mem * 1-A */
               b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
               b->ops->Enable(b);
               break;
            case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2):
               /* Cycle1:   In * AFog + Fog * 1-A */
               /* Cycle2:   In * AIn + Mem * AMem */
               b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               b->ops->Enable(b);
               break;

            case BLEND_FOG_APRIM + (BLEND_OPA>>2):
               /* For Golden Eye */
               /* Cycle1:   Fog * AFog + In * 1-A */
               /* Cycle2:   In * AIn + Mem * AMem */
            case BLEND_FOG_ASHADE + (BLEND_OPA>>2):
               /* Cycle1:   Fog * AShade + In * 1-A */
               /* Cycle2:   In * AIn + Mem * AMem */
            case BLEND_BI_AFOG + (BLEND_OPA>>2):
               /* Cycle1:   Bl * AFog + In * 1-A */
               /* Cycle2:   In * AIn + Mem * 1-AMem */
            case BLEND_FOG_ASHADE + (BLEND_NOOP>>2):
               /* Cycle1:   Fog * AShade + In * 1-A */
               /* Cycle2:   In * AIn + In * 1-A */
            case BLEND_NOOP + (BLEND_OPA>>2):
               /* Cycle1:   In * AIn + In * 1-A */
               /* Cycle2:   In * AIn + Mem * AMem */
            case BLEND_NOOP4 + (BLEND_NOOP>>2):
               /* Cycle1:   Fog * AIn + In * 1-A */
               /* Cycle2:   In * 0 + In * 1 */
            case BLEND_FOG_ASHADE+(BLEND_PASS>>2):
               /* Cycle1:   Fog * AShade + In * 1-A */
               /* Cycle2:   In * 0 + In * 1 */
            case BLEND_FOG_3+(BLEND_PASS>>2):
               b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               b->ops->Enable(b);
               break;
            case BLEND_FOG_ASHADE+0x0301:
               /* c800 - Cycle1:   Fog * AShade + In * 1-A */
               /* 0301 - Cycle2:   In * 0 + In * AMem */
               b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_ZERO);
               b->ops->Enable(b);
               break;
            case 0x0c08+0x1111:
               /* 0c08 - Cycle1:   In * 0 + In * 1 */
               /* 1111 - Cycle2:   Mem * AFog + Mem * AMem */
               b->ops->BlendFunc(b, BLEND_ZERO, BLEND_DESTALPHA);
               b->ops->Enable(b);
               break;
            default:
               if( blendmode_2 == (BLEND_PASS>>2) )
               {
                  b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               }
               else
               {
                  b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
               }
               b->ops->Enable(b);
               break;
         }
         break;
      default:    /* 1/2 Cycle or Copy */
         if( gRDP.otherMode.force_bl && gRDP.otherMode.z_cmp && blendmode_1 != BLEND_FOG_ASHADE )
         {
            b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
            b->ops->Enable(b);
            break;
         }
         if( gRDP.otherMode.force_bl && options.enableHackForGames == HACK_FOR_COMMANDCONQUER )
         {
            b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
            b->ops->Enable(b);
            break;
         }


         switch ( blendmode_1 )
            /* switch ( blendmode_2<<2 ) */
         {
            case BLEND_XLU: /* IN * A_IN + MEM * (1-A_IN) */
            case BLEND_BI_AIN:  /* Bl * AIn + Mem * 1-A */
            case BLEND_FOG_MEM: /* c440 - Cycle1:   Fog * AFog + Mem * 1-A */
            case BLEND_FOG_MEM_IN_MEM:  /* c440 - Cycle1:   In * AFog + Mem * 1-A */
            case BLEND_BLENDCOLOR:  /* Bl * 0 + Bl * 1 */
            case 0x00c0:    /* In * AIn + Fog * 1-A */
               b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
               b->ops->Enable(b);
               break;
            case BLEND_MEM_ALPHA_IN:    /* Mem * AIn + Mem * AMem */
               b->ops->BlendFunc(b, BLEND_ZERO, BLEND_DESTALPHA);
               b->ops->Enable(b);
               break;
            case BLEND_PASS:    /* IN * 0 + IN * 1 */
               b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               if( gRDP.otherMode.alpha_cvg_sel )
               {
                  b->ops->Enable(b);
               }
               else
               {
                  b->ops->Disable(b);
               }
               break;
            case BLEND_OPA:     /* IN * A_IN + MEM * A_MEM */
               if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
               {
                  b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
               }
               else
               {
                  b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               }
               b->ops->Enable(b);
               break;
            case BLEND_NOOP:        /* IN * A_IN + IN * (1 - A_IN) */
            case BLEND_FOG_ASHADE:  /* Fog * AShade + In * 1-A */
            case BLEND_FOG_MEM_3:   /* Mem * AFog + Fog * 1-A */
            case BLEND_BI_AFOG:     /* Bl * AFog + In * 1-A */
               b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
               b->ops->Enable(b);
               break;
            case BLEND_FOG_APRIM:   /* Fog * AFog + In * 1-A */
               b->ops->BlendFunc(b, BLEND_INVSRCALPHA, BLEND_ZERO);
               b->ops->Enable(b);
               break;
            case BLEND_NOOP3:       /* In * 0 + Mem * 1 */
            case BLEND_NOOP5:       /* Fog * 0 + Mem * 1 */
               b->ops->BlendFunc(b, BLEND_ZERO, BLEND_ONE);
               b->ops->Enable(b);
               break;
            case BLEND_MEM:     /* Mem * 0 + Mem * 1-A */
               /* WaveRace */
               b->ops->BlendFunc(b, BLEND_ZERO, BLEND_ONE);
               b->ops->Enable(b);
               break;
            default:
               b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
               b->ops->Enable(b);
               RiceRender_SetAlphaTestEnable(1);
               break;
         }
   }
}

void Blender_NormalAlphaBlender(CBlender *b)
{
   b->ops->BlendFunc(b, BLEND_SRCALPHA, BLEND_INVSRCALPHA);
   b->ops->Enable(b);
}

void Blender_DisableAlphaBlender(CBlender *b)
{
   b->ops->BlendFunc(b, BLEND_ONE, BLEND_ZERO);
   b->ops->Enable(b);
}
