#ifndef COMMONPLUGINAPI_H
#define COMMONPLUGINAPI_H

#include "m64p_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/* where GLideN64 keeps its files */
void gli_api_find_plugin_path(wchar_t * _strPath);
void gli_api_user_data_path(wchar_t * _strPath);
void gli_api_user_cache_path(wchar_t * _strPath);

#ifdef __cplusplus
}
#endif

#endif /* COMMONPLUGINAPI_H */
