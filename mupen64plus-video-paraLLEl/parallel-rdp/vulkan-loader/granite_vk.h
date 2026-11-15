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


#ifndef GRANITE_VK_H
#define GRANITE_VK_H

/* The Vulkan functions Granite calls, loaded from the frontend's
 * vkGetInstanceProcAddr: the instance's as globals named after them, the
 * device's in a table per device. Nothing is linked against a Vulkan
 * library. C89. */

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
/* (the core alone: no window system's header, windows.h above all, is
 * pulled in; the frontend makes the surfaces) */
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The instance's, called on the instance and its devices */
#define GRANITE_VK_INSTANCE_FUNCTIONS(X) \
	X(vkCreateDevice) \
	X(vkEnumerateDeviceExtensionProperties) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetDeviceProcAddr) \
	X(vkGetPhysicalDeviceFeatures2) \
	X(vkGetPhysicalDeviceFormatProperties2) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceMemoryProperties2) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceToolPropertiesEXT)

/* The device's */
#define GRANITE_VK_DEVICE_FUNCTIONS(X) \
	X(vkAllocateCommandBuffers) \
	X(vkAllocateDescriptorSets) \
	X(vkAllocateMemory) \
	X(vkBeginCommandBuffer) \
	X(vkBindBufferMemory) \
	X(vkBindImageMemory) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdBindDescriptorSets) \
	X(vkCmdBindPipeline) \
	X(vkCmdBindVertexBuffers) \
	X(vkCmdBlitImage) \
	X(vkCmdClearColorImage) \
	X(vkCmdClearDepthStencilImage) \
	X(vkCmdCopyBuffer) \
	X(vkCmdCopyBufferToImage) \
	X(vkCmdDispatch) \
	X(vkCmdDispatchIndirect) \
	X(vkCmdDraw) \
	X(vkCmdEndRenderPass) \
	X(vkCmdFillBuffer) \
	X(vkCmdPipelineBarrier) \
	X(vkCmdPushConstants) \
	X(vkCmdPushDescriptorSetWithTemplateKHR) \
	X(vkCmdSetScissor) \
	X(vkCmdSetViewport) \
	X(vkCreateBuffer) \
	X(vkCreateBufferView) \
	X(vkCreateCommandPool) \
	X(vkCreateComputePipelines) \
	X(vkCreateDescriptorPool) \
	X(vkCreateDescriptorSetLayout) \
	X(vkCreateDescriptorUpdateTemplate) \
	X(vkCreateFence) \
	X(vkCreateFramebuffer) \
	X(vkCreateGraphicsPipelines) \
	X(vkCreateImage) \
	X(vkCreateImageView) \
	X(vkCreatePipelineCache) \
	X(vkCreatePipelineLayout) \
	X(vkCreateRenderPass) \
	X(vkCreateSampler) \
	X(vkCreateSemaphore) \
	X(vkCreateShaderModule) \
	X(vkDestroyBuffer) \
	X(vkDestroyBufferView) \
	X(vkDestroyCommandPool) \
	X(vkDestroyDescriptorPool) \
	X(vkDestroyDescriptorSetLayout) \
	X(vkDestroyDescriptorUpdateTemplate) \
	X(vkDestroyDevice) \
	X(vkDestroyFence) \
	X(vkDestroyFramebuffer) \
	X(vkDestroyImage) \
	X(vkDestroyImageView) \
	X(vkDestroyPipeline) \
	X(vkDestroyPipelineCache) \
	X(vkDestroyPipelineLayout) \
	X(vkDestroyRenderPass) \
	X(vkDestroySampler) \
	X(vkDestroySemaphore) \
	X(vkDestroyShaderModule) \
	X(vkDeviceWaitIdle) \
	X(vkEndCommandBuffer) \
	X(vkFlushMappedMemoryRanges) \
	X(vkFreeCommandBuffers) \
	X(vkFreeMemory) \
	X(vkGetBufferMemoryRequirements2) \
	X(vkGetDeviceQueue) \
	X(vkGetImageMemoryRequirements) \
	X(vkGetImageMemoryRequirements2) \
	X(vkGetMemoryHostPointerPropertiesEXT) \
	X(vkGetPipelineCacheData) \
	X(vkInvalidateMappedMemoryRanges) \
	X(vkMapMemory) \
	X(vkQueueSubmit) \
	X(vkResetCommandPool) \
	X(vkResetDescriptorPool) \
	X(vkResetFences) \
	X(vkTrimCommandPool) \
	X(vkUnmapMemory) \
	X(vkUpdateDescriptorSetWithTemplate) \
	X(vkUpdateDescriptorSetWithTemplateKHR) \
	X(vkUpdateDescriptorSets) \
	X(vkWaitForFences) \
	X(vkWaitSemaphoresKHR)

#define GRANITE_VK_DECLARE(name) extern PFN_##name name;
GRANITE_VK_INSTANCE_FUNCTIONS(GRANITE_VK_DECLARE)
#undef GRANITE_VK_DECLARE

struct granite_vk_device_table
{
#define GRANITE_VK_FIELD(name) PFN_##name name;
	GRANITE_VK_DEVICE_FUNCTIONS(GRANITE_VK_FIELD)
#undef GRANITE_VK_FIELD
	/* (so that the table is never empty for a compiler) */
	int loaded;
};

/* Where the functions come from */
void granite_vk_init_loader(PFN_vkGetInstanceProcAddr get_instance_proc_addr);

/* The loader's Vulkan version: 1.0 without vkEnumerateInstanceVersion,
 * 0 without a loader */
uint32_t granite_vk_instance_version(void);

/* The instance's functions, for @instance */
void granite_vk_load_instance(VkInstance instance);

/* @device's functions: NULL those it does not have */
void granite_vk_load_device_table(struct granite_vk_device_table *table, VkDevice device);

#ifdef __cplusplus
}
#endif

#endif
