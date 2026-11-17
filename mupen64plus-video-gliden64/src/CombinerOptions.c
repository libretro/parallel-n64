/* The options GLideN64's generated combiner shaders depend on, one bit
 * each in this order: stored with the shaders, and compared to tell when
 * they must be built again.
 * WARNING: the shader storage format version must be increased after any
 * change in this function. */
#include "Config.h"

u32 gli_combiner_options_bits(void)
{
	return (config.video.multisampling > 0 ? 1U : 0U)
		| (config.texture.bilinearMode << 1)
		| (config.texture.enableHalosRemoval << 2)
		| (config.generalEmulation.enableHWLighting << 3)
		| (config.generalEmulation.enableNoise << 4)
		| (config.generalEmulation.enableLOD << 5)
		| (config.frameBufferEmulation.N64DepthCompare << 6)
		| (config.generalEmulation.enableLegacyBlending << 7)
		| (config.generalEmulation.enableFragmentDepthWrite << 8);
}
