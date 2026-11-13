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
#include "queue_submit.h"

bool granite_queue_waits_add(granite_queue_waits_t *waits, granite_semaphore_t *semaphore,
		VkPipelineStageFlags stages)
{
	if (waits->count == GRANITE_QUEUE_MAX_WAITS)
		return false;
	granite_semaphore_ref(semaphore);
	waits->semaphores[waits->count] = semaphore;
	waits->stages[waits->count] = stages;
	waits->count++;
	return true;
}

void granite_queue_waits_collect(granite_queue_waits_t *waits, granite_submit_composer_t *composer,
		granite_frame_garbage_t *garbage)
{
	unsigned pass, i;

	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < waits->count; i++)
			if (granite_semaphore_is_timeline(waits->semaphores[i]) == (pass == 1))
				granite_semaphore_wait(waits->semaphores[i], waits->stages[i], composer, garbage);

	for (i = 0; i < waits->count; i++)
		granite_semaphore_unref(waits->semaphores[i]);
	waits->count = 0;
}

void granite_queue_waits_clear(granite_queue_waits_t *waits, VkDevice device, const struct VolkDeviceTable *table)
{
	unsigned i;

	for (i = 0; i < waits->count; i++)
	{
		granite_semaphore_t *semaphore = waits->semaphores[i];

		/* (a timeline value has nothing to free) */
		if (semaphore->owned && semaphore->semaphore != VK_NULL_HANDLE)
			table->vkDestroySemaphore(device, semaphore->semaphore, NULL);
		semaphore->semaphore = VK_NULL_HANDLE;
		semaphore->owned = false;
		semaphore->signalled = false;
		granite_semaphore_unref(semaphore);
	}
	waits->count = 0;
}

void granite_staging_plan(VkQueue src, VkQueue graphics, VkQueue compute, VkPipelineStageFlags stages,
		VkAccessFlags access, granite_staging_plan_t *plan)
{
	/* what a compute queue can do with it */
	VkPipelineStageFlags compute_stages = stages & (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			| VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
	VkAccessFlags compute_access = access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
			| VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT
			| VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

	memset(plan, 0, sizeof(*plan));

	if (src == graphics && src == compute)
	{
		/* one queue for it all: a barrier is enough */
		plan->barrier_stages = stages;
		plan->barrier_access = access;
	}
	else if (src == graphics)
	{
		plan->barrier_stages = stages;
		plan->barrier_access = access;
		plan->wait_compute = compute_stages != 0;
	}
	else if (src == compute)
	{
		plan->barrier_stages = compute_stages;
		plan->barrier_access = compute_access;
		plan->wait_graphics = stages != 0;
	}
	else
	{
		/* a queue of its own: both wait */
		plan->wait_graphics = stages != 0;
		plan->wait_compute = compute_stages != 0;
	}

	plan->graphics_stages = stages;
	plan->compute_stages = compute_stages;
}
