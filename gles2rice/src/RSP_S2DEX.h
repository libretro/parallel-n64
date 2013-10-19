/*
Copyright (C) 2002 Rice1964

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

#ifndef _RSP_S2DEX_H_
#define _RSP_S2DEX_H_

#define S2DEX_BG_1CYC           0x01
#define S2DEX_BG_COPY           0x02
#define S2DEX_OBJ_RECTANGLE     0x03
#define S2DEX_OBJ_SPRITE        0x04
#define S2DEX_OBJ_MOVEMEM       0x05
#define S2DEX_SELECT_DL         0xb0
#define S2DEX_OBJ_RENDERMODE    0xb1
#define S2DEX_OBJ_RECTANGLE_R   0xb2
#define S2DEX_OBJ_LOADTXTR      0xc1
#define S2DEX_OBJ_LDTX_SPRITE   0xc2
#define S2DEX_OBJ_LDTX_RECT     0xc3
#define S2DEX_OBJ_LDTX_RECT_R   0xc4
#define S2DEX_RDPHALF_0         0xe4

#define S2DEX_OBJLT_TXTRBLOCK   0x00001033
#define S2DEX_OBJLT_TXTRTILE    0x00fc1034
#define S2DEX_OBJLT_TLUT        0x00000030
#define S2DEX_BGLT_LOADBLOCK    0x0033
#define S2DEX_BGLT_LOADTILE     0xfff4

typedef struct //Intel Format
{
  uint32    type;  // Struct classification type. Should always be 0x1033
  uint32    image; // Texture address within the DRAM.

  uint16    tmem;  // TMEM load address
  uint16    tsize; // Texture size
  uint16    tline; // Texture line width for one line.

  uint16    sid;   // State ID
  uint32    flag;  // State flag
  uint32    mask;  // State mask
} uObjTxtrBlock;

typedef struct //Intel Format
{
  uint32    type;    // Struct classification type. Should always be 0xfc1034
  uint32    image;   // Texture address within the DRAM.

  uint16    tmem;    // TMEM load address
  uint16    twidth;  // Texture width
  uint16    theight; // Texture height

  uint16    sid;     // State ID. Always a multiple of 4. Can be 0, 4, 8, or 12.
  uint32    flag;    // State flag
  uint32    mask;    // State mask
  
} uObjTxtrTile;      // 24 bytes

typedef struct // Intel Format
{
  uint32    type;  // Struct classification type, should always be 0x30
  uint32    image; // Texture address within the DRAM.
  
  uint16    pnum;  // Loaded palette number - 1
  uint16    phead; // Number indicating the first loaded palette.
  
  uint16    zero;  // Always 0
  
  uint16    sid;   // State ID. Always a multiple of 4. Can be 0, 4, 8, or 12.
  uint32    flag;  // State flag
  uint32    mask;  // State mask
  
} uObjTxtrTLUT;    // 24 bytes

typedef union
{
  uObjTxtrBlock  block;
  uObjTxtrTile   tile;
  uObjTxtrTLUT   tlut;
} uObjTxtr;

typedef struct // Intel format
{
  uint16  scaleW;      // X-axis scale factor.
  short   objX;        // X-coordinate of the upper-left position of the sprite.
  
  uint16  paddingX;    // Unused, always 0.
  uint16  imageW;      // Texture width
  
  uint16  scaleH;      // Y-axis scale factor.
  short   objY;        // Y-coordinate of the upper-left position of the sprite.
  
  uint16  paddingY;    // Unused, always 0.
  uint16  imageH;      // Texture height
  
  uint16  imageAdrs;   // Texture address within the TMEM.
  uint16  imageStride; // Number of bytes from one row of pixels in memory to the next row of pixels (ie. Stride)

  uint8   imageFlags;  // Flag used for image manipulations
  uint8   imagePal;    // Palette number. Can be from 0 - 7
  uint8   imageSiz;    // Texel size
  uint8   imageFmt;    // Texel format
} uObjSprite;


typedef struct 
{
  uObjTxtr      txtr;
  uObjSprite    sprite;
} uObjTxSprite;     /* 48 bytes */

typedef struct // Intel format
{
  s32       A, B, C, D;

  short     Y;
  short     X;

  uint16   BaseScaleY;
  uint16   BaseScaleX;
} uObjMtx;

typedef struct  //Intel format
{
  float   A, B, C, D;
  float   X;
  float   Y;
  float   BaseScaleX;
  float   BaseScaleY;
} uObjMtxReal;

typedef struct
{
  short    Y;
  short    X;
  uint16   BaseScaleY;
  uint16   BaseScaleX;
} uObjSubMtx;


typedef struct // Intel Format
{
  uint16    imageW;     // Texture width
  uint16    imageX;     // X-coordinate of the upper-left position of the texture.

  uint16    frameW;     // Width of the frame to be transferred.
  short     frameX;     // X-coordinate of the upper-left position of the frame to be transferred.

  uint16    imageH;     // Texture height
  uint16    imageY;     // Y-coordinate of the upper-left position of the texture.

  uint16    frameH;     // Height of the frame to be transferred.
  short     frameY;     // Y-coordinate of the upper-left position of the frame to be transferred.  

  uint32    imagePtr;   // Texture address within the DRAM.

  uint8     imageSiz;   // Texel size
  uint8     imageFmt;   // Texel format
  uint16    imageLoad;  // Can either be S2DEX_BGLT_LOADBLOCK (0x0033) or #define S2DEX_BGLT_LOADTILE (0xfff4).

  uint16    imageFlip;  // Inverts the image if 0x01 is set.
  uint16    imagePal;   // Palette number

  uint16    tmemH;
  uint16    tmemW;
  uint16    tmemLoadTH;
  uint16    tmemLoadSH;
  uint16    tmemSize;
  uint16    tmemSizeW;
} uObjBg;

// Intel Format
typedef struct
{
  uint16    imageW;     // Texture width
  uint16    imageX;     // X-coordinate of the upper-left position of the texture.

  uint16    frameW;     // Width of the frame to be transferred.
  short     frameX;     // X-coordinate of the upper-left position of the frame to be transferred.

  uint16    imageH;     // Texture height
  uint16    imageY;     // Y-coordinate of the upper-left position of the texture.

  uint16    frameH;     // Height of the frame to be transferred.
  short     frameY;     // Y-coordinate of the upper-left position of the frame to be transferred.   

  uint32    imagePtr;   // Texture address within the DRAM.

  uint8     imageSiz;   // Texel size
  uint8     imageFmt;   // Texel format
  uint16    imageLoad;  // Can either be S2DEX_BGLT_LOADBLOCK (0x0033) or S2DEX_BGLT_LOADTILE (0xfff4)

  uint16    imageFlip;  // Inverts the image if 0x01 is set.
  uint16    imagePal;   // Palette number

  uint16    scaleH;     // Y-axis scale factor
  uint16    scaleW;     // X-axis scale factor

  s32       imageYorig; // Starting point in the original image.
  uint8     padding[4];
} uObjScaleBg;

#endif

