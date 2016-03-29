#ifndef _GDP_FUNCS_PROT_H
#define _GDP_FUNCS_PROT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Glide64 prototypes */
void glide64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry );

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

#ifndef GLIDEN64
#ifdef __cplusplus
}
#endif
#endif

#endif
