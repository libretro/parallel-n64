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
#include "vu.h"

INLINE static void do_ne(short* VD, short* VS, short* VT)
{
    register int i;

#ifdef INTENSE_DEBUG
    for (i = 0; i < 8; i++)
       fprintf(stderr, "VS[%d] = %d\n", i, VS[i]);
    for (i = 0; i < 8; i++)
       fprintf(stderr, "VT[%d] = %d\n", i, VT[i]);
#endif

    for (i = 0; i < N; i++)
        clip[i] = 0;
    for (i = 0; i < N; i++)
        comp[i] = (VS[i] != VT[i]);
    for (i = 0; i < N; i++)
        comp[i] = comp[i] | ne[i];
#if (0)
    merge(VACC_L, comp, VS, VT); /* correct but redundant */
#else
    vector_copy(VACC_L, VS);
#endif
    vector_copy(VD, VACC_L);

    for (i = 0; i < N; i++)
        ne[i] = 0;
    for (i = 0; i < N; i++)
        co[i] = 0;

#ifdef INTENSE_DEBUG
    for (i = 0; i < 8; i++)
       fprintf(stderr, "VD[%d] = %d\n", i, VD[i]);
#endif
    return;
}

static void VNE(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_ne(VR[vd], VR[vs], ST);
    return;
}
