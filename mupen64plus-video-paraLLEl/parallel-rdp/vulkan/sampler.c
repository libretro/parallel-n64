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

#include <string.h>
#include "sampler.h"
#include "cookie.h"
#include "logging.h"

void granite_sampler_linear_clamp_info(VkSamplerCreateInfo *info)
{
	memset(info, 0, sizeof(*info));
	info->sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info->magFilter = VK_FILTER_LINEAR;
	info->minFilter = VK_FILTER_LINEAR;
	info->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	info->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info->maxAnisotropy = 1.0f;
	info->maxLod = VK_LOD_CLAMP_NONE;
}

bool granite_sampler_init_linear_clamp(granite_sampler_t *sampler, VkDevice device,
		const struct VolkDeviceTable *table)
{
	VkSamplerCreateInfo info;

	sampler->sampler = VK_NULL_HANDLE;
	sampler->cookie = 0;
	granite_sampler_linear_clamp_info(&info);
	if (table->vkCreateSampler(device, &info, NULL, &sampler->sampler) != VK_SUCCESS)
	{
		LOGE("Failed to create sampler.\n");
		sampler->sampler = VK_NULL_HANDLE;
		return false;
	}
	sampler->cookie = granite_cookie_next();
	return true;
}

void granite_sampler_free(granite_sampler_t *sampler, VkDevice device, const struct VolkDeviceTable *table)
{
	if (sampler->sampler != VK_NULL_HANDLE)
		table->vkDestroySampler(device, sampler->sampler, NULL);
	sampler->sampler = VK_NULL_HANDLE;
	sampler->cookie = 0;
}
