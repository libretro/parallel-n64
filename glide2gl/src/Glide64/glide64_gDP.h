#include <stdint.h>

#include <retro_inline.h>

#include "GBI.h"
#include "Util.h"
#include "TexLoad.h"

//forward decls
extern void RestoreScale(void);

extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int CI_SET;
extern uint32_t swapped_addr;

#ifdef __cplusplus
extern "C" {
#endif

void glide64gDPLoadBlock( uint32_t tile, uint32_t ul_s, uint32_t ul_t,
      uint32_t lr_s, uint32_t dxt );

#ifdef __cplusplus
}
#endif
