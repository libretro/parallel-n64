/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

//****************************************************************
//
// Glide64 - Glide Plugin for Nintendo 64 emulators
// Project started on December 29th, 2001
//
// Authors:
// Dave2001, original author, founded the project in 2001, left it in 2002
// Gugaman, joined the project in 2002, left it in 2002
// Sergey 'Gonetz' Lipski, joined the project in 2002, main author since fall of 2002
// Hiroshi 'KoolSmoky' Morii, joined the project in 2007
//
//****************************************************************
//
// To modify Glide64:
// * Write your name and (optional)email, commented by your work, so I know who did it, and so that you can find which parts you modified when it comes time to send it to me.
// * Do NOT send me the whole project or file that you modified.  Take out your modified code sections, and tell me where to put them.  If people sent the whole thing, I would have many different versions, but no idea how to combine them all.
//
//****************************************************************
//
// SuperFastHash calculation functions 
// https://bitbucket.org/fabriceT/lhash/raw/b7b6836c1273f7120459f8314e201cac5495ce3e/SuperFastHash.c
//
// http://www.azillionmonkeys.com/qed/hash.html
// code under GPLv2.0 licence.
// Created by Gonetz, 2004
//
//****************************************************************
//*
#include <stdio.h>
#include <stdint.h>

void CRC_BuildTable(void)
{
}

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
  +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif

#if 0
/* SuperFastHash */
uint32_t CRC32(uint32_t hash, const char * data, int len)
{
  uint32_t tmp;
  int rem;

  if (len <= 0 || data == NULL)
     return 0;

  rem = len & 3;
  len >>= 2;

  /* Main loop */
  for (;len > 0; len--) {
    hash  += get16bits (data);
    tmp    = (get16bits (data+2) << 11) ^ hash;
    hash   = (hash << 16) ^ tmp;
    data  += 2*sizeof (uint16_t);
    hash  += hash >> 11;
  }

  /* Handle end cases */
  switch (rem) {
        case 3: hash += get16bits (data);
          hash ^= hash << 16;
          hash ^= data[sizeof (uint16_t)] << 18;
          hash += hash >> 11;
          break;
        case 2: hash += get16bits (data);
          hash ^= hash << 11;
          hash += hash >> 17;
          break;
        case 1: hash += *data;
          hash ^= hash << 10;
          hash += hash >> 1;
  }

  /* Force "avalanching" of final 127 bits */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;
}
#else

#define MAGIC 0x5bd1e995
#define MAGIC2 24
/* MurmurHash2 */
uint32_t CRC32(uint32_t seed, const char *key, int len)
{
   // 'm' and 'r' are mixing constants generated offline.
   // They're not really 'magic', they just happen to work well.

   // Initialize the hash to a 'random' value

   uint32_t h = seed ^ len;

   // Mix 4 bytes at a time into the hash

   const uint8_t* data = (const uint8_t*)key;

   while(len >= 4)
   {
      uint32_t k = *(uint32_t*)data;

      k *= MAGIC;
      k ^= k >> MAGIC2;
      k *= MAGIC;

      h *= MAGIC;
      h ^= k;

      data += 4;
      len -= 4;
   }

   // Handle the last few bytes of the input array

   switch(len)
   {
      case 3: h ^= data[2] << 16;
      case 2: h ^= data[1] << 8;
      case 1: h ^= data[0];
              h *= MAGIC;
   };

   // Do a few final mixes of the hash to ensure the last few
   // bytes are well-incorporated.

   h ^= h >> 13;
   h *= MAGIC;
   h ^= h >> 15;

   return h;
}
#endif
