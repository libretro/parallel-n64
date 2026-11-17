#ifndef TEXRECTDRAWER_H
#define TEXRECTDRAWER_H

#include <boolean.h>
#include "Types.h"
#include "gSP.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FrameBuffer;

void gli_texrect_init(void);
void gli_texrect_destroy(void);
bool gli_texrect_add_rect(void);
void gli_texrect_add_background_rect(void);
bool gli_texrect_draw(void);
bool gli_texrect_is_empty(void);
bool gli_texrect_can_continue(void);


#ifdef __cplusplus
}
#endif

#endif /* TEXRECTDRAWER_H */
