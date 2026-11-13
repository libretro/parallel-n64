/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GRANITE_SAMPLER_H
#define GRANITE_SAMPLER_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* The samplers a device keeps */
enum granite_stock_sampler
{
	GRANITE_STOCK_SAMPLER_LINEAR_CLAMP = 0,
	GRANITE_STOCK_SAMPLER_COUNT
};

/* A sampler the device keeps for as long as it lives */
typedef struct granite_sampler
{
	VkSampler sampler;
	/* what tells it from every other object, for caches */
	uint64_t cookie;
} granite_sampler_t;

/* What the one sampler in use is created with: linear within a level,
 * the nearest level, clamped to the edge, every level, no comparison
 * and no anisotropy */
void granite_sampler_linear_clamp_info(VkSamplerCreateInfo *info);

/* Creates that sampler. False, and no sampler, when the driver will not. */
bool granite_sampler_init_linear_clamp(granite_sampler_t *sampler, VkDevice device,
		const struct VolkDeviceTable *table);
/* Destroys it at once: nothing may be using it */
void granite_sampler_free(granite_sampler_t *sampler, VkDevice device, const struct VolkDeviceTable *table);

RETRO_END_DECLS

#endif
