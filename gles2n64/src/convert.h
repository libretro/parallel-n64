#ifndef CONVERT_H
#define CONVERT_H

#include "Types.h"

static const volatile unsigned char Five2Eight[32] =
{
      0, // 00000 = 00000000
      8, // 00001 = 00001000
     16, // 00010 = 00010000
     25, // 00011 = 00011001
     33, // 00100 = 00100001
     41, // 00101 = 00101001
     49, // 00110 = 00110001
     58, // 00111 = 00111010
     66, // 01000 = 01000010
     74, // 01001 = 01001010
     82, // 01010 = 01010010
     90, // 01011 = 01011010
     99, // 01100 = 01100011
    107, // 01101 = 01101011
    115, // 01110 = 01110011
    123, // 01111 = 01111011
    132, // 10000 = 10000100
    140, // 10001 = 10001100
    148, // 10010 = 10010100
    156, // 10011 = 10011100
    165, // 10100 = 10100101
    173, // 10101 = 10101101
    181, // 10110 = 10110101
    189, // 10111 = 10111101
    197, // 11000 = 11000101
    206, // 11001 = 11001110
    214, // 11010 = 11010110
    222, // 11011 = 11011110
    230, // 11100 = 11100110
    239, // 11101 = 11101111
    247, // 11110 = 11110111
    255  // 11111 = 11111111
};

static const volatile unsigned char Four2Eight[16] =
{
      0, // 0000 = 00000000
     17, // 0001 = 00010001
     34, // 0010 = 00100010
     51, // 0011 = 00110011
     68, // 0100 = 01000100
     85, // 0101 = 01010101
    102, // 0110 = 01100110
    119, // 0111 = 01110111
    136, // 1000 = 10001000
    153, // 1001 = 10011001
    170, // 1010 = 10101010
    187, // 1011 = 10111011
    204, // 1100 = 11001100
    221, // 1101 = 11011101
    238, // 1110 = 11101110
    255  // 1111 = 11111111
};

static const volatile unsigned char Three2Four[8] =
{
     0, // 000 = 0000
     2, // 001 = 0010
     4, // 010 = 0100
     6, // 011 = 0110
     9, // 100 = 1001
    11, // 101 = 1011
    13, // 110 = 1101
    15, // 111 = 1111
};

static const volatile unsigned char Three2Eight[8] =
{
      0, // 000 = 00000000
     36, // 001 = 00100100
     73, // 010 = 01001001
    109, // 011 = 01101101
    146, // 100 = 10010010
    182, // 101 = 10110110
    219, // 110 = 11011011
    255, // 111 = 11111111
};

static const volatile unsigned char Two2Eight[4] =
{
   0, // 00 = 00000000
   85, // 01 = 01010101
   170, // 10 = 10101010
   255  // 11 = 11111111
};

static const volatile unsigned char One2Four[2] =
{
   0, // 0 = 0000
   15, // 1 = 1111
};

static const volatile unsigned char One2Eight[2] =
{
   0, // 0 = 00000000
   255, // 1 = 11111111
};

static INLINE void UnswapCopy( void *src, void *dest, u32 numBytes )
{
   int i, numDWords, trailingBytes;
   // copy leading bytes
   int leadingBytes = ((intptr_t)src) & 3;
   if (leadingBytes != 0)
   {
      leadingBytes = 4-leadingBytes;
      if ((unsigned int)leadingBytes > numBytes)
         leadingBytes = numBytes;
      numBytes -= leadingBytes;

      src = (void *)((intptr_t)src ^ 3);
      for (i = 0; i < leadingBytes; i++)
      {
         *(u8 *)(dest) = *(u8 *)(src);
         dest = (void *)((intptr_t)dest+1);
         src  = (void *)((intptr_t)src -1);
      }
      src = (void *)((intptr_t)src+5);
   }

   // copy dwords
   numDWords = numBytes >> 2;
   while (numDWords--)
   {
      u32 dword = *(u32 *)src;
      dword = ((dword<<24)|((dword<<8)&0x00FF0000)|((dword>>8)&0x0000FF00)|(dword>>24));
      *(u32 *)dest = dword;
      dest = (void *)((intptr_t)dest+4);
      src  = (void *)((intptr_t)src +4);
   }

   // copy trailing bytes
   trailingBytes = numBytes & 3;
   if (trailingBytes)
   {
      src = (void *)((intptr_t)src ^ 3);
      for (i = 0; i < trailingBytes; i++)
      {
         *(u8 *)(dest) = *(u8 *)(src);
         dest = (void *)((intptr_t)dest+1);
         src  = (void *)((intptr_t)src -1);
      }
   }
}

static INLINE void DWordInterleave( void *mem, u32 numDWords )
{
   int tmp;
   while( numDWords-- )
   {
      tmp = *(int *)((intptr_t)mem + 0);
      *(int *)((intptr_t)mem + 0) = *(int *)((intptr_t)mem + 4);
      *(int *)((intptr_t)mem + 4) = tmp;
      mem = (void *)((intptr_t)mem + 8);
   }
}

static INLINE void QWordInterleave( void *mem, u32 numDWords )
{
   numDWords >>= 1; // qwords
   while( numDWords-- )
   {
      int tmp0, tmp1;
      tmp0 = *(int *)((intptr_t)mem + 0);
      tmp1 = *(int *)((intptr_t)mem + 4);
      *(int *)((intptr_t)mem + 0) = *(int *)((intptr_t)mem + 8);
      *(int *)((intptr_t)mem + 8) = tmp0;
      *(int *)((intptr_t)mem + 4) = *(int *)((intptr_t)mem + 12);
      *(int *)((intptr_t)mem + 12) = tmp1;
      mem = (void *)((intptr_t)mem + 16);
   }
}

static INLINE u32 swapdword( u32 value )
{
   return ((value & 0xff000000) >> 24) |
      ((value & 0x00ff0000) >>  8) |
      ((value & 0x0000ff00) <<  8) |
      ((value & 0x000000ff) << 24);
}

static INLINE u16 swapword( u16 value )
{
   return (value << 8) | (value >> 8);
}

#define RGBA8888_RGBA4444(color) (((color & 0x000000f0) <<  8) | ((color & 0x0000f000) >>  4) | ((color & 0x00f00000) >> 16) | ((color & 0xf0000000) >> 28))

static INLINE u32 RGBA5551_RGBA8888( u16 color )
{
   u8 r, g, b, a;
   color = swapword( color );
   r = Five2Eight[color >> 11];
   g = Five2Eight[(color >> 6) & 0x001f];
   b = Five2Eight[(color >> 1) & 0x001f];
   a = One2Eight [(color     ) & 0x0001];
   return (a << 24) | (b << 16) | (g << 8) | r;
}

// Just swaps the word
#define RGBA5551_RGBA5551(color) (swapword( color ))

static INLINE u32 IA88_RGBA8888( u16 color )
{
    u8 a = color >> 8;
    u8 i = color & 0x00FF;
    return (a << 24) | (i << 16) | (i << 8) | i;
}

static INLINE u16 IA88_RGBA4444( u16 color )
{
   u8 i = color >> 12;
   u8 a = (color >> 4) & 0x000F;
   return (i << 12) | (i << 8) | (i << 4) | a;
}

#define IA44_RGBA4444(color) (((color & 0xf0) << 8) | ((color & 0xf0) << 4) | (color))

static INLINE u32 IA44_RGBA8888( u8 color )
{
   u8 i = Four2Eight[color >> 4];
   u8 a = Four2Eight[color & 0x0F];
   return (a << 24) | (i << 16) | (i << 8) | i;
}

static INLINE u16 IA44_IA88( u8 color )
{
   u8 i = Four2Eight[color >> 4];
   u8 a = Four2Eight[color & 0x0F];
   return (a << 8) | i;
}

static INLINE u16 IA31_RGBA4444( u8 color )
{
   u8 i = Three2Four[color >> 1];
   u8 a = One2Four[color & 0x01];
   return (i << 12) | (i << 8) | (i << 4) | a;
}

static INLINE u16 IA31_IA88( u8 color )
{
   u8 i = Three2Eight[color >> 1];
   u8 a = One2Eight[color & 0x01];
   return (a << 8) | i;
}

static INLINE u32 IA31_RGBA8888( u8 color )
{
   u8 i = Three2Eight[color >> 1];
   u8 a = One2Eight[color & 0x01];
   return (i << 24) | (i << 16) | (i << 8) | a;
}

static INLINE u16 I8_RGBA4444( u8 color )
{
   u8 c = color >> 4;
   return (c << 12) | (c << 8) | (c << 4) | c;
}

#define I8_RGBA8888(color) ((color << 24) | (color << 16) | (color << 8) | color)

static INLINE u16 I4_RGBA4444( u8 color )
{
   u16 ret = color & 0x0f;
   ret |= ret << 4; ret |= ret << 8; return ret;
}

#define I4_I8(color) (Four2Eight[color & 0x0f])

static INLINE u16 I4_IA88( u8 color )
{
   u32 c = Four2Eight[color & 0x0f];
   return (c << 8) | c;
}

#define I8_IA88(color) ((color << 8) | color)

static INLINE u16 IA88_IA88( u16 color )
{
   u8 a = (color&0xFF);
   u8 i = (color>>8);
   return  (i << 8) | a;
}


static INLINE u32 I4_RGBA8888( u8 color )
{
   u8 c = Four2Eight[color];
   c |= c << 4;
   return (c << 24) | (c << 16) | (c << 8) | c;
}

#endif // CONVERT_H

