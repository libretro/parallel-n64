#include "vu.h"
#include "divrom.h"

static void VRSQ(int vd, int de, int vt, int e)
{
   message("VRSQ\nUntested.", 1);

   DivIn = (int)VR[vt][e & 07];
   do_div(DivIn, SP_DIV_SQRT_YES, SP_DIV_PRECISION_SINGLE);
   SHUFFLE_VECTOR(VACC_L, VR[vt], e);
   VR[vd][de &= 07] = (short)DivOut;
   DPH = SP_DIV_PRECISION_SINGLE;
   return;
}
