#include "F3D.h"
#include "F3DEX.h"
#include "F3DEX095.h"
#include "gSP.h"
#include "GBI.h"

// F3DEX 0.95 is an early F3DEX variant used by Mario Kart 64. It is
// identical to F3DEX except for G_CULLDL: the 0.95 microcode uses the
// older F3D-style cull semantics rather than F3DEX_CullDL.
void F3DEX095_Init()
{
	F3DEX_Init();
	GBI_SetGBI(G_CULLDL, F3D_CULLDL, F3D_CullDL);
}
