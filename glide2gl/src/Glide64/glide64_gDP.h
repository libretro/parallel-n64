#include <stdint.h>

#include <retro_inline.h>

#include "../../../Graphics/GBI.h"
#include "Util.h"
#include "TexLoad.h"

extern uint32_t ucode5_texshiftaddr;
extern uint32_t ucode5_texshiftcount;
extern uint16_t ucode5_texshift;
extern int CI_SET;
extern uint32_t swapped_addr;

#ifdef __cplusplus
extern "C" {
#endif

void glide64gDPSetTile(
      uint32_t fmt,
      uint32_t siz,
      uint32_t line,
      uint32_t tmem,
      uint32_t tile,
      uint32_t palette,
      uint32_t cmt,
      uint32_t maskt,
      uint32_t shiftt,
      uint32_t cms,
      uint32_t masks,
      uint32_t shifts );

void glide64gDPLoadTile(uint32_t tile, uint32_t ul_s, uint32_t ul_t,
      uint32_t lr_s, uint32_t lr_t);

void glide64gDPSetTileSize(uint32_t tile, uint32_t uls, uint32_t ult,
      uint32_t lrs, uint32_t lrt);

#ifdef __cplusplus
}
#endif
