#ifndef _GDP_FUNCS_PROT_H
#define _GDP_FUNCS_PROT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Glide64 prototypes */
void glide64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry );
void glide64gDPLoadBlock( uint32_t tile, uint32_t ul_s, uint32_t ul_t,
      uint32_t lr_s, uint32_t dxt );

#ifdef __cplusplus
}
#endif


/* GLN64 prototypes */
#ifndef GLIDEN64
#ifdef __cplusplus
extern "C" {
#endif
#endif

void gln64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry );
void gln64gDPLoadBlock( uint32_t tile, uint32_t ul_s, uint32_t ul_t,
      uint32_t lr_s, uint32_t dxt );

#ifndef GLIDEN64
#ifdef __cplusplus
}
#endif
#endif

#endif
