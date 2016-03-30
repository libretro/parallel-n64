#include <math.h>

#include "../Glitch64/glide.h"
#include "Combine.h"
#include "rdp.h"
#include "../../Graphics/GBI.h"
#include "../../Graphics/RSP/gSP_funcs_C.h"
#include "../../Graphics/RSP/RSP_state.h"

typedef struct DRAWOBJECT_t
{
  float objX;
  float objY;
  float scaleW;
  float scaleH;
  int16_t imageW;
  int16_t imageH;

  uint16_t  imageStride;
  uint16_t  imageAdrs;
  uint8_t  imageFmt;
  uint8_t  imageSiz;
  uint8_t  imagePal;
  uint8_t  imageFlags;
} DRAWOBJECT;

/* forward decls */

static void uc6_draw_polygons (VERTEX v[4]);
static void uc6_read_object_data (DRAWOBJECT *d);
static void uc6_init_tile(const DRAWOBJECT *d);
extern uint32_t dma_offset_mtx;
extern int32_t cur_mtx;
extern uint32_t dma_offset_mtx;
extern uint32_t dma_offset_vtx;
extern int32_t billboarding;
extern int dzdx;
extern int deltaZ;
VERTEX **org_vtx;

void cull_trianglefaces(VERTEX **v, unsigned iterations, bool do_update, bool do_cull, int32_t wd);
void pre_update(void);
/* end forward decls */

