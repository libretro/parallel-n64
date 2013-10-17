#ifndef _GLIDE_FUNCS_H
#define _GLIDE_FUNCS_H

#include "glide.h"

typedef FxU32 GrCCUColor_t;
typedef FxU32 GrACUColor_t;
typedef FxU32 GrTCCUColor_t;
typedef FxU32 GrTACUColor_t;
typedef FxU32 GrCombineMode_t;

// ZIGGY framebuffer copy extension
// allow to copy the depth or color buffer from back/front to front/back
#define GR_FBCOPY_MODE_DEPTH 0
#define GR_FBCOPY_MODE_COLOR 1
#define GR_FBCOPY_BUFFER_BACK 0
#define GR_FBCOPY_BUFFER_FRONT 1
FX_ENTRY void FX_CALL grFramebufferCopyExt(int x, int y, int w, int h,
                                           int buffer_from, int buffer_to, int mode);

// COMBINE extension

#define GR_FUNC_MODE_ZERO                 0x00
#define GR_FUNC_MODE_X                    0x01
#define GR_FUNC_MODE_ONE_MINUS_X          0x02
#define GR_FUNC_MODE_NEGATIVE_X           0x03
#define GR_FUNC_MODE_X_MINUS_HALF         0x04

#define GR_CMBX_ZERO                      0x00
#define GR_CMBX_TEXTURE_ALPHA             0x01
#define GR_CMBX_ALOCAL                    0x02
#define GR_CMBX_AOTHER                    0x03
#define GR_CMBX_B                         0x04
#define GR_CMBX_CONSTANT_ALPHA            0x05
#define GR_CMBX_CONSTANT_COLOR            0x06
#define GR_CMBX_DETAIL_FACTOR             0x07
#define GR_CMBX_ITALPHA                   0x08
#define GR_CMBX_ITRGB                     0x09
#define GR_CMBX_LOCAL_TEXTURE_ALPHA       0x0a
#define GR_CMBX_LOCAL_TEXTURE_RGB         0x0b
#define GR_CMBX_LOD_FRAC                  0x0c
#define GR_CMBX_OTHER_TEXTURE_ALPHA       0x0d
#define GR_CMBX_OTHER_TEXTURE_RGB         0x0e
#define GR_CMBX_TEXTURE_RGB               0x0f
#define GR_CMBX_TMU_CALPHA                0x10
#define GR_CMBX_TMU_CCOLOR                0x11


FX_ENTRY void FX_CALL
grColorCombineExt(GrCCUColor_t a, GrCombineMode_t a_mode,
				  GrCCUColor_t b, GrCombineMode_t b_mode,
                  GrCCUColor_t c, FxBool c_invert,
				  GrCCUColor_t d, FxBool d_invert,
				  FxU32 shift, FxBool invert);

FX_ENTRY void FX_CALL
grAlphaCombineExt(GrACUColor_t a, GrCombineMode_t a_mode,
				  GrACUColor_t b, GrCombineMode_t b_mode,
				  GrACUColor_t c, FxBool c_invert,
				  GrACUColor_t d, FxBool d_invert,
				  FxU32 shift, FxBool invert);

FX_ENTRY void FX_CALL
grTexColorCombineExt(GrChipID_t       tmu,
                     GrTCCUColor_t a, GrCombineMode_t a_mode,
                     GrTCCUColor_t b, GrCombineMode_t b_mode,
                     GrTCCUColor_t c, FxBool c_invert,
                     GrTCCUColor_t d, FxBool d_invert,
                     FxU32 shift, FxBool invert);

FX_ENTRY void FX_CALL
grTexAlphaCombineExt(GrChipID_t       tmu,
                     GrTACUColor_t a, GrCombineMode_t a_mode,
                     GrTACUColor_t b, GrCombineMode_t b_mode,
                     GrTACUColor_t c, FxBool c_invert,
                     GrTACUColor_t d, FxBool d_invert,
                     FxU32 shift, FxBool invert);

FX_ENTRY void FX_CALL
grConstantColorValueExt(GrChipID_t    tmu,
                        GrColor_t     value);

FX_ENTRY void FX_CALL 
grColorCombineExt(GrCCUColor_t a, GrCombineMode_t a_mode,
                  GrCCUColor_t b, GrCombineMode_t b_mode,
                  GrCCUColor_t c, FxBool c_invert,
                  GrCCUColor_t d, FxBool d_invert,
                  FxU32 shift, FxBool invert);

FX_ENTRY void FX_CALL
grAlphaCombineExt(GrACUColor_t a, GrCombineMode_t a_mode,
                  GrACUColor_t b, GrCombineMode_t b_mode,
                  GrACUColor_t c, FxBool c_invert,
                  GrACUColor_t d, FxBool d_invert,
                  FxU32 shift, FxBool invert);

// config functions
//FX_ENTRY void FX_CALL grConfigWrapperExt(HINSTANCE instance, HWND hwnd);
FX_ENTRY void FX_CALL grConfigWrapperExt(FxI32, FxI32, FxBool, FxBool);
FX_ENTRY GrScreenResolution_t FX_CALL grWrapperFullScreenResolutionExt(FxU32*, FxU32*);
FX_ENTRY void FX_CALL grGetGammaTableExt(FxU32, FxU32*, FxU32*, FxU32*);

extern void grChromaRangeExt(GrColor_t color0, GrColor_t color1, FxU32 mode);
extern void grChromaRangeModeExt(GrChromakeyMode_t mode);
extern void grTexChromaRangeExt(GrChipID_t tmu, GrColor_t color0, GrColor_t color1, GrTexChromakeyMode_t mode);
extern void grTexChromaModeExt(GrChipID_t tmu, GrChromakeyMode_t mode);

FX_ENTRY void FX_CALL grTextureBufferExt( GrChipID_t  		tmu,
                                         FxU32 				startAddress,
                                         GrLOD_t 			lodmin,
                                         GrLOD_t 			lodmax,
                                         GrAspectRatio_t 	aspect,
                                         GrTextureFormat_t 	fmt,
                                         FxU32 				evenOdd);

FX_ENTRY void FX_CALL
grTextureAuxBufferExt( GrChipID_t tmu,
                      FxU32      startAddress,
                      GrLOD_t    thisLOD,
                      GrLOD_t    largeLOD,
                      GrAspectRatio_t aspectRatio,
                      GrTextureFormat_t format,
                      FxU32      odd_even_mask );

FX_ENTRY void FX_CALL grAuxBufferExt( GrBuffer_t buffer );

#endif
