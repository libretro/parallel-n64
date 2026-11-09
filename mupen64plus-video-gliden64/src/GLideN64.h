#ifndef GLIDEN64_H
#define GLIDEN64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char	pluginName[];
extern wchar_t	pluginNameW[];
extern void (*CheckInterrupts)( void );

#ifdef __cplusplus
}
#endif

#endif /* GLIDEN64_H */
