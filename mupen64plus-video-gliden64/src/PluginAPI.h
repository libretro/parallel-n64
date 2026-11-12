#ifndef COMMONPLUGINAPI_H
#define COMMONPLUGINAPI_H

#include <stddef.h>
#include "m64p_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/* where GLideN64 keeps its files: UTF-8, forward slashes, no trailing slash */
void gli_api_user_data_path(char * _strPath, size_t _size);
void gli_api_user_cache_path(char * _strPath, size_t _size);

#ifdef __cplusplus
}
#endif

#endif /* COMMONPLUGINAPI_H */
