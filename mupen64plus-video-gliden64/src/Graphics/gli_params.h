#ifndef GLI_PARAMS_H
#define GLI_PARAMS_H

/* GLideN64's graphics parameters, the numbers GL has for them, which the
 * Vulkan backend takes too. C89; opengl_ContextImpl.c holds each to its GL
 * header's.
 * GLES2 knows only GLI_BT_FRAMEBUFFER: C binds frame buffers to
 * gli_ctx.drawFramebuffer and readFramebuffer, which are that there. */

/* colorFormat */
#define GLI_CF_RED_GREEN_BLUE              0x1907
#define GLI_CF_RGBA                        0x1908
#define GLI_CF_RG                          0x8227
#define GLI_CF_RED                         0x1903
#define GLI_CF_DEPTH                       0x1902
#define GLI_CF_LUMINANCE                   0x1909
#define GLI_CF_RED_INTEGER                 0x8D94

/* internalcolorFormat */
#define GLI_ICF_NOCOLOR                    0
#define GLI_ICF_RGB8                       0x8051
#define GLI_ICF_RGBA8                      0x8058
#define GLI_ICF_RGBA4                      0x8056
#define GLI_ICF_RGB5_A1                    0x8057
#define GLI_ICF_RG                         0x822B
#define GLI_ICF_R16F                       0x822D
#define GLI_ICF_DEPTH                      0x81A6
#define GLI_ICF_RG32F                      0x8230
#define GLI_ICF_LUMINANCE                  0x1909
#define GLI_ICF_COLOR_INDEX8               0x80E5
/* the internal formats only the frame buffer textures use */
#define GLI_ICF_RGB                        0x1907
#define GLI_ICF_RGBA                       0x1908
#define GLI_ICF_R8                         0x8229
#define GLI_ICF_R16UI                      0x8234
#define GLI_ICF_R32F                       0x822E
#define GLI_ICF_RGBA32F                    0x8814
#define GLI_ICF_DEPTH_COMPONENT            0x1902
#define GLI_ICF_DEPTH16                    0x81A5

/* datatype */
#define GLI_DT_UNSIGNED_BYTE               0x1401
#define GLI_DT_UNSIGNED_SHORT              0x1403
#define GLI_DT_UNSIGNED_INT                0x1405
#define GLI_DT_FLOAT                       0x1406
#define GLI_DT_HALF_FLOAT                  0x140B
#define GLI_DT_UNSIGNED_SHORT_5_6_5        0x8363
#define GLI_DT_UNSIGNED_SHORT_5_5_5_1      0x8034
#define GLI_DT_UNSIGNED_SHORT_4_4_4_4      0x8033

/* textureTarget */
#define GLI_TT_TEXTURE_2D                  0x0DE1
#define GLI_TT_TEXTURE_2D_MULTISAMPLE      0x9100
#define GLI_TT_TEXTURE_EXTERNAL            0x8D65
#define GLI_TT_RENDERBUFFER                0x8D41

/* bufferTarget */
#define GLI_BT_FRAMEBUFFER                 0x8D40
#define GLI_BT_DRAW_FRAMEBUFFER            0x8CA9
#define GLI_BT_READ_FRAMEBUFFER            0x8CA8

/* bufferAttachment */
#define GLI_BA_COLOR_ATTACHMENT0           0x8CE0
#define GLI_BA_COLOR_ATTACHMENT1           0x8CE1
#define GLI_BA_COLOR_ATTACHMENT2           0x8CE2
#define GLI_BA_DEPTH_ATTACHMENT            0x8D00

/* enable */
#define GLI_EN_BLEND                       0x0BE2
#define GLI_EN_CULL_FACE                   0x0B44
#define GLI_EN_DEPTH_TEST                  0x0B71
#define GLI_EN_DEPTH_CLAMP                 0x864F
#define GLI_EN_CLIP_DISTANCE0              0x3000
#define GLI_EN_DITHER                      0x0BD0
#define GLI_EN_POLYGON_OFFSET_FILL         0x8037
#define GLI_EN_SCISSOR_TEST                0x0C11

/* textureIndices */
#define GLI_TU_TEX0                        0
#define GLI_TU_TEX1                        1
#define GLI_TU_NoiseTex                    2
#define GLI_TU_DepthTex                    3
#define GLI_TU_ZLUTTex                     4
#define GLI_TU_PaletteTex                  5
#define GLI_TU_MSTEX0                      6
#define GLI_TU_MSTEX1                      7

/* textureImageUnits */
#define GLI_IU_DepthZ                      2
#define GLI_IU_DepthDeltaZ                 3

/* textureImageAccessMode */
#define GLI_IA_READ_ONLY                   0x88B8
#define GLI_IA_WRITE_ONLY                  0x88B9
#define GLI_IA_READ_WRITE                  0x88BA

/* textureParameters */
#define GLI_TP_FILTER_NEAREST              0x2600
#define GLI_TP_FILTER_LINEAR               0x2601
#define GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST 0x2700
#define GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST 0x2701
#define GLI_TP_WRAP_CLAMP_TO_EDGE          0x812F
#define GLI_TP_WRAP_REPEAT                 0x2901
#define GLI_TP_WRAP_MIRRORED_REPEAT        0x8370

/* cullMode */
#define GLI_CULLMODE_FRONT                     0x0405
#define GLI_CULLMODE_BACK                      0x0404
#define GLI_CULLMODE_FRONT_AND_BACK            0x0408

/* compare */
#define GLI_COMPARE_LEQUAL                     0x0203
#define GLI_COMPARE_LESS                       0x0201
#define GLI_COMPARE_ALWAYS                     0x0207

/* blend */
#define GLI_BLEND_ZERO                     0x0000
#define GLI_BLEND_ONE                      0x0001
#define GLI_BLEND_SRC_ALPHA                0x0302
#define GLI_BLEND_DST_ALPHA                0x0304
#define GLI_BLEND_ONE_MINUS_SRC_ALPHA      0x0303
#define GLI_BLEND_CONSTANT_ALPHA           0x8003
#define GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GLI_BLEND_SRC1_COLOR               0x88F9
#define GLI_BLEND_ONE_MINUS_SRC1_COLOR     0x88FA
#define GLI_BLEND_SRC1_ALPHA               0x8589
#define GLI_BLEND_ONE_MINUS_SRC1_ALPHA     0x88FB

/* drawmode */
#define GLI_DM_TRIANGLES                   0x0004
#define GLI_DM_TRIANGLE_STRIP              0x0005
#define GLI_DM_LINES                       0x0001

/* blitMask */
#define GLI_BM_COLOR_BUFFER                0x4000
#define GLI_BM_DEPTH_BUFFER                0x0100
#define GLI_BM_STENCIL_BUFFER              0x0400

#endif
