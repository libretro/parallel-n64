#ifndef GLI_COMBINER_STAGES_H
#define GLI_COMBINER_STAGES_H

/* What every backend does to a combiner's stages before writing them, and
 * the inputs they read (CombinerStages.c). C89. */

#include <boolean.h>
#include "Types.h"
#include "CombinerMux.h"
#include "gDP.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the first cycle reads texel 0 for texel 1 */
void gli_stage_correct_first(CombinerStage *_stage);
/* the second cycle's texels swapped */
void gli_stage_correct_second(CombinerStage *_stage);

/* N64 sign extension of the first cycle's result, by how the second reads it */
bool gli_combined_color_c(u32 _cycleType, const gDPCombine *_combine);
bool gli_combined_alpha_c(u32 _cycleType, const gDPCombine *_combine);
bool gli_combined_color_abd(u32 _cycleType, const gDPCombine *_combine);
bool gli_combined_alpha_abd(u32 _cycleType, const gDPCombine *_combine);

/* Four words a stage: op * 256 + input, -1 to end, INTER keeping its other
 * two inputs in the next two. */
void gli_stage_encode(const CombinerStage *_stage, int *_ops);

/* A combiner's inputs: a bit for each G_GCI_ input it reads */
u32 gli_inputs_add_stage(u32 _inputs, const CombinerStage *_stage);
bool gli_inputs_tile(u32 _inputs, u32 _tile);
bool gli_inputs_texture(u32 _inputs);
bool gli_inputs_lod(u32 _inputs);
bool gli_inputs_noise(u32 _inputs);
bool gli_inputs_shade(u32 _inputs);
bool gli_inputs_shade_color(u32 _inputs);
bool gli_inputs_hw_lighting(u32 _inputs);

#ifdef __cplusplus
}
#endif

#endif
