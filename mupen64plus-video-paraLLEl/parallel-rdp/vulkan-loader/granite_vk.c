/* Copyright (c) 2020 Themaister
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
#include "granite_vk.h"

static PFN_vkGetInstanceProcAddr get_proc;

#define GRANITE_VK_DEFINE(name) PFN_##name name;
GRANITE_VK_INSTANCE_FUNCTIONS(GRANITE_VK_DEFINE)
#undef GRANITE_VK_DEFINE

void granite_vk_init_loader(PFN_vkGetInstanceProcAddr get_instance_proc_addr)
{
	get_proc = get_instance_proc_addr;
}

uint32_t granite_vk_instance_version(void)
{
	PFN_vkEnumerateInstanceVersion enumerate;
	uint32_t version = 0;

	if (!get_proc)
		return 0;
	enumerate = (PFN_vkEnumerateInstanceVersion)get_proc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
	if (!enumerate)
		return VK_API_VERSION_1_0;
	return enumerate(&version) == VK_SUCCESS ? version : 0;
}

void granite_vk_load_instance(VkInstance instance)
{
#define GRANITE_VK_LOAD(name) name = get_proc ? (PFN_##name)get_proc(instance, #name) : NULL;
	GRANITE_VK_INSTANCE_FUNCTIONS(GRANITE_VK_LOAD)
#undef GRANITE_VK_LOAD
}

void granite_vk_load_device_table(struct granite_vk_device_table *table, VkDevice device)
{
	memset(table, 0, sizeof(*table));
	if (!vkGetDeviceProcAddr)
		return;
#define GRANITE_VK_LOAD(name) table->name = (PFN_##name)vkGetDeviceProcAddr(device, #name);
	GRANITE_VK_DEVICE_FUNCTIONS(GRANITE_VK_LOAD)
#undef GRANITE_VK_LOAD
	table->loaded = 1;
}
