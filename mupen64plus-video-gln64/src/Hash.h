#ifndef _GLES2N64_HASH_H
#define _GLES2N64_HASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t Hash_Calculate(uint32_t hash, const void *buffer, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* _GLES2N64_HASH_H */
