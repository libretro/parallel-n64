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

#ifndef GRANITE_COMMAND_POOL_H
#define GRANITE_COMMAND_POOL_H

#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* A frame context's command buffers for one queue family and thread:
 * handed out one after another through the frame, all taken back at once
 * when the frame context comes round again. A command buffer is allocated
 * the first time a frame needs one more than any before it did, and kept.
 * Belongs to the thread that records with it: no locking. */
typedef struct granite_command_pool
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkCommandPool pool;
	VkCommandBuffer *buffers;
	unsigned count;
	unsigned capacity;
	/* handed out since begin */
	unsigned index;
} granite_command_pool_t;

/* queue_family_index: VK_QUEUE_FAMILY_IGNORED for a queue the device does
 * not have; the pool then stays empty and is never asked. */
void granite_command_pool_init(granite_command_pool_t *pool, VkDevice device,
		const struct VolkDeviceTable *table, uint32_t queue_family_index);
/* Frees its command buffers and the pool */
void granite_command_pool_free(granite_command_pool_t *pool);
/* The frame's GPU work is done: every command buffer is free again */
void granite_command_pool_begin(granite_command_pool_t *pool);
/* Gives the command buffers and their memory back to the driver */
void granite_command_pool_trim(granite_command_pool_t *pool);
/* VK_NULL_HANDLE if one cannot be made */
VkCommandBuffer granite_command_pool_request(granite_command_pool_t *pool);

RETRO_END_DECLS

#endif
