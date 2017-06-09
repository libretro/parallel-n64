#include "../../../../main/main.h"
#include "../../../../main/device.h"
#include "../../../../memory/memory.h"
#include "../../../../rsp/rsp_core.h"
#include "../../../cached_interp.h"
#include "../../../cp0_private.h"
#include "../../../cp1_private.h"
#include "../../../interupt.h"
#include "../../../ops.h"
#include "../../../r4300.h"
#include "../../../recomp.h"
#include "../../../recomph.h" //include for function prototypes
#include "../../../tlb.h"
#include "../../new_dynarec.h"
#include "n64.h"

#ifdef __cplusplus
extern "C" {
#endif

extern u_int memory_map[1048576];

#ifdef __cplusplus
}
#endif
