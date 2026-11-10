#ifndef GLI_BLEND_H
#define GLI_BLEND_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	GLI_BF_ZERO,
	GLI_BF_ONE,
	GLI_BF_SRC_ALPHA,
	GLI_BF_ONE_MINUS_SRC_ALPHA,
	GLI_BF_DST_ALPHA,
	GLI_BF_CONSTANT_ALPHA,
	GLI_BF_ONE_MINUS_CONSTANT_ALPHA
};

/* The fixed-function blend for the current RDP blender setup */
typedef struct gli_blend_state
{
	u8 enable;
	u8 src, dst;
	u8 fogColor; /* the fog color is the blend constant */
} gli_blend_state;

/* @_legacy: the legacy table whatever the config, for combiners without
 * shader blending. @_blastCorpsTexture: the Blast Corps hack applies and
 * the combiner samples a texture */
void gli_blend_mode(int _legacy, int _blastCorpsTexture, gli_blend_state *_out);

#ifdef __cplusplus
}
#endif

#endif
