/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.11.26                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/

static INLINE void do_and(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = VS[i] & VT[i];
   vector_copy(VD, VACC_L);
}

static INLINE void do_nand(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = ~(VS[i] & VT[i]);
   vector_copy(VD, VACC_L);
}

static INLINE void do_nor(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = ~(VS[i] | VT[i]);
   vector_copy(VD, VACC_L);
}

static INLINE void do_nxor(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = ~(VS[i] ^ VT[i]);
   vector_copy(VD, VACC_L);
}

static INLINE void VNXOR(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_nxor(VR[vd], VR[vs], ST);
}

static INLINE void VNOR(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_nor(VR[vd], VR[vs], ST);
}

static INLINE void do_or(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] | VT[i];
    vector_copy(VD, VACC_L);
}

static INLINE void do_xor(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] ^ VT[i];
    vector_copy(VD, VACC_L);
}

static INLINE void VXOR(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_xor(VR[vd], VR[vs], ST);
}

static INLINE void VOR(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_or(VR[vd], VR[vs], ST);
}

static INLINE void VAND(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_and(VR[vd], VR[vs], ST);
}

static INLINE void VNAND(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_nand(VR[vd], VR[vs], ST);
}
