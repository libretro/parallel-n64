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


#ifndef __RICE_RDP_GFX_H__
#define __RICE_RDP_GFX_H__

#define RSP_SPNOOP              0   // handle 0 gracefully 
#define RSP_MTX                 1
#define RSP_RESERVED0           2   // unknown 
#define RSP_MOVEMEM             3   // move a block of memory (up to 4 words) to dmem 
#define RSP_VTX                 4
#define RSP_RESERVED1           5   // unknown 
#define RSP_DL                  6
#define RSP_RESERVED2           7   // unknown 
#define RSP_RESERVED3           8   // unknown 
#define RSP_SPRITE2D            9   // sprite command 
#define RSP_SPRITE2D_BASE       9   // sprite command


#define RSP_1ST                 0xBF
#define RSP_TRI1                (RSP_1ST-0)
#define RSP_CULLDL              (RSP_1ST-1)
#define RSP_POPMTX              (RSP_1ST-2)
#define RSP_MOVEWORD            (RSP_1ST-3)
#define RSP_TEXTURE             (RSP_1ST-4)
#define RSP_SETOTHERMODE_H      (RSP_1ST-5)
#define RSP_SETOTHERMODE_L      (RSP_1ST-6)
#define RSP_ENDDL               (RSP_1ST-7)
#define RSP_SETGEOMETRYMODE     (RSP_1ST-8)
#define RSP_CLEARGEOMETRYMODE   (RSP_1ST-9)
#define RSP_LINE3D              (RSP_1ST-10)
#define RSP_RDPHALF_1           (RSP_1ST-11)
#define RSP_RDPHALF_2           (RSP_1ST-12)
#define RSP_RDPHALF_CONT        (RSP_1ST-13)

#define RSP_MODIFYVTX           (RSP_1ST-13)
#define RSP_TRI2                (RSP_1ST-14)
#define RSP_BRANCH_Z            (RSP_1ST-15)
#define RSP_LOAD_UCODE          (RSP_1ST-16)

#define RSP_SPRITE2D_SCALEFLIP    (RSP_1ST-1)
#define RSP_SPRITE2D_DRAW         (RSP_1ST-2)

#define RSP_ZELDAVTX                1
#define RSP_ZELDAMODIFYVTX          2
#define RSP_ZELDACULLDL             3
#define RSP_ZELDABRANCHZ            4
#define RSP_ZELDATRI1               5
#define RSP_ZELDATRI2               6
#define RSP_ZELDALINE3D             7
#define RSP_ZELDARDPHALF_2          0xf1
#define RSP_ZELDASETOTHERMODE_H     0xe3
#define RSP_ZELDASETOTHERMODE_L     0xe2
#define RSP_ZELDARDPHALF_1          0xe1
#define RSP_ZELDASPNOOP             0xe0
#define RSP_ZELDAENDDL              0xdf
#define RSP_ZELDADL                 0xde
#define RSP_ZELDALOAD_UCODE         0xdd
#define RSP_ZELDAMOVEMEM            0xdc
#define RSP_ZELDAMOVEWORD           0xdb
#define RSP_ZELDAMTX                0xda
#define RSP_ZELDAGEOMETRYMODE       0xd9
#define RSP_ZELDAPOPMTX             0xd8
#define RSP_ZELDATEXTURE            0xd7
#define RSP_ZELDASUBMODULE          0xd6

// 4 is something like a conditional DL
#define RSP_DMATRI  0x05
#define G_DLINMEM   0x07

// RDP commands:
#define RDP_NOOP            0xc0
#define RDP_SETCIMG         0xff
#define RDP_SETZIMG         0xfe
#define RDP_SETTIMG         0xfd
#define RDP_SETCOMBINE      0xfc
#define RDP_SETENVCOLOR     0xfb
#define RDP_SETPRIMCOLOR    0xfa
#define RDP_SETBLENDCOLOR   0xf9
#define RDP_SETFOGCOLOR     0xf8
#define RDP_SETFILLCOLOR    0xf7
#define RDP_FILLRECT        0xf6
#define RDP_SETTILE         0xf5
#define RDP_LOADTILE        0xf4
#define RDP_LOADBLOCK       0xf3
#define RDP_SETTILESIZE     0xf2
#define RDP_LOADTLUT        0xf0
#define RDP_RDPSETOTHERMODE 0xef
#define RDP_SETPRIMDEPTH    0xee
#define RDP_SETSCISSOR      0xed
#define RDP_SETCONVERT      0xec
#define RDP_SETKEYR         0xeb
#define RDP_SETKEYGB        0xea
#define RDP_FULLSYNC        0xe9
#define RDP_TILESYNC        0xe8
#define RDP_PIPESYNC        0xe7
#define RDP_LOADSYNC        0xe6
#define RDP_TEXRECT_FLIP    0xe5
#define RDP_TEXRECT         0xe4




#define RSP_ZELDA_MTX_MODELVIEW     0x00
#define RSP_ZELDA_MTX_PROJECTION    0x04
#define RSP_ZELDA_MTX_MUL           0x00
#define RSP_ZELDA_MTX_LOAD          0x02
#define RSP_ZELDA_MTX_PUSH          0x00
#define RSP_ZELDA_MTX_NOPUSH        0x01



//
// RSP_SETOTHERMODE_L sft: shift count

#define RSP_SETOTHERMODE_SHIFT_ALPHACOMPARE     0
#define RSP_SETOTHERMODE_SHIFT_ZSRCSEL          2
#define RSP_SETOTHERMODE_SHIFT_RENDERMODE       3
#define RSP_SETOTHERMODE_SHIFT_BLENDER          16

//
// RSP_SETOTHERMODE_H sft: shift count

#define RSP_SETOTHERMODE_SHIFT_BLENDMASK        0   // unsupported 
#define RSP_SETOTHERMODE_SHIFT_ALPHADITHER      4
#define RSP_SETOTHERMODE_SHIFT_RGBDITHER        6

#define RSP_SETOTHERMODE_SHIFT_COMBKEY          8
#define RSP_SETOTHERMODE_SHIFT_TEXTCONV         9
#define RSP_SETOTHERMODE_SHIFT_TEXTFILT         12
#define RSP_SETOTHERMODE_SHIFT_TEXTLUT          14
#define RSP_SETOTHERMODE_SHIFT_TEXTLOD          16
#define RSP_SETOTHERMODE_SHIFT_TEXTDETAIL       17
#define RSP_SETOTHERMODE_SHIFT_TEXTPERSP        19
#define RSP_SETOTHERMODE_SHIFT_CYCLETYPE        20
#define RSP_SETOTHERMODE_SHIFT_COLORDITHER      22  // unsupported in HW 2.0 
#define RSP_SETOTHERMODE_SHIFT_PIPELINE         23

// RSP_SETOTHERMODE_H gPipelineMode 
#define RSP_PIPELINE_MODE_1PRIMITIVE        (1 << RSP_SETOTHERMODE_SHIFT_PIPELINE)
#define RSP_PIPELINE_MODE_NPRIMITIVE        (0 << RSP_SETOTHERMODE_SHIFT_PIPELINE)

// RSP_SETOTHERMODE_H gSetCycleType 
#define CYCLE_TYPE_1        0
#define CYCLE_TYPE_2        1
#define CYCLE_TYPE_COPY     2
#define CYCLE_TYPE_FILL     3

// RSP_SETOTHERMODE_H gSetTextureLUT 
#define TLUT_FMT_NONE           (0 << RSP_SETOTHERMODE_SHIFT_TEXTLUT)
#define TLUT_FMT_UNKNOWN        (1 << RSP_SETOTHERMODE_SHIFT_TEXTLUT)
#define TLUT_FMT_RGBA16         (2 << RSP_SETOTHERMODE_SHIFT_TEXTLUT)
#define TLUT_FMT_IA16           (3 << RSP_SETOTHERMODE_SHIFT_TEXTLUT)

// RSP_SETOTHERMODE_H gSetTextureFilter 
#define RDP_TFILTER_POINT       (0 << RSP_SETOTHERMODE_SHIFT_TEXTFILT)
#define RDP_TFILTER_AVERAGE     (3 << RSP_SETOTHERMODE_SHIFT_TEXTFILT)
#define RDP_TFILTER_BILERP      (2 << RSP_SETOTHERMODE_SHIFT_TEXTFILT)

// RSP_SETOTHERMODE_L gSetAlphaCompare 
#define RDP_ALPHA_COMPARE_NONE          (0 << RSP_SETOTHERMODE_SHIFT_ALPHACOMPARE)
#define RDP_ALPHA_COMPARE_THRESHOLD     (1 << RSP_SETOTHERMODE_SHIFT_ALPHACOMPARE)
#define RDP_ALPHA_COMPARE_DITHER        (3 << RSP_SETOTHERMODE_SHIFT_ALPHACOMPARE)

// RSP_SETOTHERMODE_L gSetRenderMode 
#define Z_COMPARE           0x0010
#define Z_UPDATE            0x0020

//
// flags for RSP_SETGEOMETRYMODE
//
#define G_ZBUFFER               0x00000001
#define G_TEXTURE_ENABLE        0x00000002  // Microcode use only 
#define G_SHADE                 0x00000004  // Enable Gouraud interp 
//
#define G_SHADING_SMOOTH        0x00000200  // Flat or smooth shaded 
#define G_CULL_FRONT            0x00001000
#define G_CULL_BACK             0x00002000
#define G_CULL_BOTH             0x00003000  // To make code cleaner 
#define G_FOG                   0x00010000
#define G_LIGHTING              0x00020000
#define G_TEXTURE_GEN           0x00040000
#define G_TEXTURE_GEN_LINEAR    0x00080000
#define G_LOD                   0x00100000  // NOT IMPLEMENTED 

//
// G_SETIMG fmt: set image formats
//
#define TXT_FMT_RGBA    0
#define TXT_FMT_YUV     1
#define TXT_FMT_CI      2
#define TXT_FMT_IA      3
#define TXT_FMT_I       4

//
// G_SETIMG siz: set image pixel size
//
#define TXT_SIZE_4b     0
#define TXT_SIZE_8b     1
#define TXT_SIZE_16b    2
#define TXT_SIZE_32b    3

//
// Texturing macros
//

#define RDP_TXT_LOADTILE    7
#define RDP_TXT_RENDERTILE  0

#define RDP_TXT_NOMIRROR    0
#define RDP_TXT_WRAP        0
#define RDP_TXT_MIRROR      0x1
#define RDP_TXT_CLAMP       0x2
#define RDP_TXT_NOMASK      0
#define RDP_TXT_NOLOD       0



//
// MOVEMEM indices
//
// Each of these indexes an entry in a dmem table
// which points to a 1-4 word block of dmem in
// which to store a 1-4 word DMA.
//
//
#define RSP_GBI1_MV_MEM_VIEWPORT    0x80
#define RSP_GBI1_MV_MEM_LOOKATY     0x82
#define RSP_GBI1_MV_MEM_LOOKATX     0x84
#define RSP_GBI1_MV_MEM_L0          0x86
#define RSP_GBI1_MV_MEM_L1          0x88
#define RSP_GBI1_MV_MEM_L2          0x8a
#define RSP_GBI1_MV_MEM_L3          0x8c
#define RSP_GBI1_MV_MEM_L4          0x8e
#define RSP_GBI1_MV_MEM_L5          0x90
#define RSP_GBI1_MV_MEM_L6          0x92
#define RSP_GBI1_MV_MEM_L7          0x94
#define RSP_GBI1_MV_MEM_TXTATT      0x96
#define RSP_GBI1_MV_MEM_MATRIX_1    0x9e    // NOTE: this is in moveword table 
#define RSP_GBI1_MV_MEM_MATRIX_2    0x98
#define RSP_GBI1_MV_MEM_MATRIX_3    0x9a
#define RSP_GBI1_MV_MEM_MATRIX_4    0x9c

# define RSP_GBI2_MV_MEM__VIEWPORT  8
# define RSP_GBI2_MV_MEM__LIGHT     10
# define RSP_GBI2_MV_MEM__POINT     12
# define RSP_GBI2_MV_MEM__MATRIX    14      // NOTE: this is in moveword table
# define RSP_GBI2_MV_MEM_O_LOOKATX  (0*24)
# define RSP_GBI2_MV_MEM_O_LOOKATY  (1*24)
# define RSP_GBI2_MV_MEM_O_L0       (2*24)
# define RSP_GBI2_MV_MEM_O_L1       (3*24)
# define RSP_GBI2_MV_MEM_O_L2       (4*24)
# define RSP_GBI2_MV_MEM_O_L3       (5*24)
# define RSP_GBI2_MV_MEM_O_L4       (6*24)
# define RSP_GBI2_MV_MEM_O_L5       (7*24)
# define RSP_GBI2_MV_MEM_O_L6       (8*24)
# define RSP_GBI2_MV_MEM_O_L7       (9*24)


//
// MOVEWORD indices
//
// Each of these indexes an entry in a dmem table
// which points to a word in dmem in dmem where
// an immediate word will be stored.
//
//
#define RSP_MOVE_WORD_MATRIX    0x00    // NOTE: also used by movemem 
#define RSP_MOVE_WORD_NUMLIGHT  0x02
#define RSP_MOVE_WORD_CLIP      0x04
#define RSP_MOVE_WORD_SEGMENT   0x06
#define RSP_MOVE_WORD_FOG       0x08
#define RSP_MOVE_WORD_LIGHTCOL  0x0a
#define RSP_MOVE_WORD_POINTS    0x0c
#define RSP_MOVE_WORD_PERSPNORM 0x0e

//
// These are offsets from the address in the dmem table
// 
#define RSP_MV_WORD_OFFSET_NUMLIGHT         0x00
#define RSP_MV_WORD_OFFSET_CLIP_RNX         0x04
#define RSP_MV_WORD_OFFSET_CLIP_RNY         0x0c
#define RSP_MV_WORD_OFFSET_CLIP_RPX         0x14
#define RSP_MV_WORD_OFFSET_CLIP_RPY         0x1c
#define RSP_MV_WORD_OFFSET_FOG              0x00    
#define RSP_MV_WORD_OFFSET_POINT_RGBA       0x10
#define RSP_MV_WORD_OFFSET_POINT_ST         0x14
#define RSP_MV_WORD_OFFSET_POINT_XYSCREEN   0x18
#define RSP_MV_WORD_OFFSET_POINT_ZSCREEN    0x1c



// Flags to inhibit pushing of the display list (on branch)
#define RSP_DLIST_PUSH      0x00
#define RSP_DLIST_NOPUSH    0x01


//
// RSP_MTX: parameter flags
//
#define RSP_MATRIX_MODELVIEW    0x00
#define RSP_MATRIX_PROJECTION   0x01

#define RSP_MATRIX_MUL          0x00
#define RSP_MATRIX_LOAD         0x02

#define RSP_MATRIX_NOPUSH       0x00
#define RSP_MATRIX_PUSH         0x04

#define MAX_DL_STACK_SIZE   32
#define MAX_DL_COUNT        1000000

#endif  // __RICE_RDP_GFX_H__

