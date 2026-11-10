#ifndef TEXTUREFILTERHANDLER_H
#define TEXTUREFILTERHANDLER_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void gli_TFH_init(void);
void gli_TFH_shutdown(void);
void gli_TFH_dumpcache(void);
bool gli_TFH_is_inited(void);
bool gli_TFH_options_changed(void);

#ifdef __cplusplus
}
#endif

#endif /* TEXTUREFILTERHANDLER_H */
