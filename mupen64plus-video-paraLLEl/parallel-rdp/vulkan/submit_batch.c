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

#include <stdlib.h>
#include <string.h>
#include "submit_batch.h"
#include "logging.h"

/* The capacity to grow an array of @cap to */
static size_t grown(size_t cap)
{
	return cap ? cap * 2 : 16;
}

static granite_submit_batch_t *current(granite_submit_composer_t *composer)
{
	return &composer->batches[composer->num_batches - 1];
}

/* A semaphore of this kind may not join the current batch */
static bool must_split(granite_submit_composer_t *composer, bool timeline)
{
	const granite_submit_batch_t *batch = current(composer);

	return composer->split && (timeline ? batch->has_binary : batch->has_timeline);
}

void granite_submit_composer_init(granite_submit_composer_t *composer)
{
	memset(composer, 0, sizeof(*composer));
	granite_submit_composer_begin(composer, false);
}

void granite_submit_composer_free(granite_submit_composer_t *composer)
{
	free(composer->waits);
	free(composer->wait_stages);
	free(composer->wait_values);
	free(composer->cmds);
	free(composer->signals);
	free(composer->signal_values);
	granite_submit_composer_init(composer);
}

void granite_submit_composer_begin(granite_submit_composer_t *composer, bool split)
{
	composer->num_waits = 0;
	composer->num_cmds = 0;
	composer->num_signals = 0;
	composer->num_batches = 1;
	composer->split = split;
	composer->failed = false;
	memset(&composer->batches[0], 0, sizeof(composer->batches[0]));
}

void granite_submit_composer_begin_batch(granite_submit_composer_t *composer)
{
	const granite_submit_batch_t *batch = current(composer);
	granite_submit_batch_t *next;

	if (batch->first_wait == composer->num_waits && batch->first_cmd == composer->num_cmds &&
			batch->first_signal == composer->num_signals)
		return;
	if (composer->num_batches == GRANITE_SUBMIT_MAX_BATCHES)
		return;

	next = &composer->batches[composer->num_batches++];
	next->first_wait = composer->num_waits;
	next->first_cmd = composer->num_cmds;
	next->first_signal = composer->num_signals;
	next->has_timeline = false;
	next->has_binary = false;
}

void granite_submit_composer_add_wait(granite_submit_composer_t *composer, VkSemaphore semaphore,
		VkPipelineStageFlags stages, uint64_t value)
{
	const granite_submit_batch_t *batch = current(composer);

	/* waits come first in a batch */
	if (batch->first_cmd != composer->num_cmds || batch->first_signal != composer->num_signals ||
			must_split(composer, value != 0))
		granite_submit_composer_begin_batch(composer);

	if (composer->num_waits == composer->cap_waits)
	{
		/* (an array that grew stays grown if a later one cannot) */
		size_t cap = grown(composer->cap_waits);
		VkSemaphore *waits = (VkSemaphore *)realloc(composer->waits, cap * sizeof(*waits));
		VkPipelineStageFlags *wait_stages;
		uint64_t *values;

		if (waits)
			composer->waits = waits;
		wait_stages = (VkPipelineStageFlags *)realloc(composer->wait_stages, cap * sizeof(*wait_stages));
		if (wait_stages)
			composer->wait_stages = wait_stages;
		values = (uint64_t *)realloc(composer->wait_values, cap * sizeof(*values));
		if (values)
			composer->wait_values = values;
		if (!waits || !wait_stages || !values)
		{
			composer->failed = true;
			return;
		}
		composer->cap_waits = cap;
	}

	composer->waits[composer->num_waits] = semaphore;
	composer->wait_stages[composer->num_waits] = stages;
	composer->wait_values[composer->num_waits] = value;
	composer->num_waits++;
	if (value)
		current(composer)->has_timeline = true;
	else
		current(composer)->has_binary = true;
}

void granite_submit_composer_add_command_buffer(granite_submit_composer_t *composer, VkCommandBuffer cmd)
{
	/* command buffers come before the signals of a batch */
	if (current(composer)->first_signal != composer->num_signals)
		granite_submit_composer_begin_batch(composer);

	if (composer->num_cmds == composer->cap_cmds)
	{
		size_t cap = grown(composer->cap_cmds);
		VkCommandBuffer *cmds = (VkCommandBuffer *)realloc(composer->cmds, cap * sizeof(*cmds));

		if (!cmds)
		{
			composer->failed = true;
			return;
		}
		composer->cmds = cmds;
		composer->cap_cmds = cap;
	}

	composer->cmds[composer->num_cmds++] = cmd;
}

void granite_submit_composer_add_signal(granite_submit_composer_t *composer, VkSemaphore semaphore,
		uint64_t value)
{
	if (must_split(composer, value != 0))
		granite_submit_composer_begin_batch(composer);

	if (composer->num_signals == composer->cap_signals)
	{
		size_t cap = grown(composer->cap_signals);
		VkSemaphore *signals = (VkSemaphore *)realloc(composer->signals, cap * sizeof(*signals));
		uint64_t *values;

		if (signals)
			composer->signals = signals;
		values = (uint64_t *)realloc(composer->signal_values, cap * sizeof(*values));
		if (values)
			composer->signal_values = values;
		if (!signals || !values)
		{
			composer->failed = true;
			return;
		}
		composer->cap_signals = cap;
	}

	composer->signals[composer->num_signals] = semaphore;
	composer->signal_values[composer->num_signals] = value;
	composer->num_signals++;
	if (value)
		current(composer)->has_timeline = true;
	else
		current(composer)->has_binary = true;
}

unsigned granite_submit_composer_bake(granite_submit_composer_t *composer, int profiling_iteration,
		const VkSubmitInfo **submits)
{
	unsigned count = 0;
	unsigned i;

	for (i = 0; i < composer->num_batches; i++)
	{
		const granite_submit_batch_t *batch = &composer->batches[i];
		bool last = i + 1 == composer->num_batches;
		size_t num_waits = (last ? composer->num_waits : batch[1].first_wait) - batch->first_wait;
		size_t num_cmds = (last ? composer->num_cmds : batch[1].first_cmd) - batch->first_cmd;
		size_t num_signals = (last ? composer->num_signals : batch[1].first_signal) - batch->first_signal;
		VkSubmitInfo *submit;

		/* an empty batch is no submission */
		if (!num_waits && !num_cmds && !num_signals)
			continue;

		submit = &composer->submits[count];
		memset(submit, 0, sizeof(*submit));
		submit->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		if (batch->has_timeline)
		{
			VkTimelineSemaphoreSubmitInfoKHR *timeline = &composer->timeline_infos[count];

			if (composer->split && batch->has_binary)
				LOGE("Using timeline semaphore info, but have binary semaphores as well.\n");

			memset(timeline, 0, sizeof(*timeline));
			timeline->sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
			timeline->waitSemaphoreValueCount = (uint32_t)num_waits;
			timeline->pWaitSemaphoreValues = composer->wait_values + batch->first_wait;
			timeline->signalSemaphoreValueCount = (uint32_t)num_signals;
			timeline->pSignalSemaphoreValues = composer->signal_values + batch->first_signal;
			submit->pNext = timeline;
		}

		if (profiling_iteration >= 0)
		{
			VkPerformanceQuerySubmitInfoKHR *profiling = &composer->profiling_infos[count];

			memset(profiling, 0, sizeof(*profiling));
			profiling->sType = VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR;
			profiling->counterPassIndex = (uint32_t)profiling_iteration;
			profiling->pNext = submit->pNext;
			submit->pNext = profiling;
		}

		submit->commandBufferCount = (uint32_t)num_cmds;
		submit->pCommandBuffers = composer->cmds + batch->first_cmd;
		submit->waitSemaphoreCount = (uint32_t)num_waits;
		submit->pWaitSemaphores = composer->waits + batch->first_wait;
		submit->pWaitDstStageMask = composer->wait_stages + batch->first_wait;
		submit->signalSemaphoreCount = (uint32_t)num_signals;
		submit->pSignalSemaphores = composer->signals + batch->first_signal;
		count++;
	}

	*submits = composer->submits;
	return count;
}

VkResult granite_submit_composer_submit(granite_submit_composer_t *composer,
		const struct VolkDeviceTable *table, VkQueue queue, VkFence fence, int profiling_iteration,
		bool one_by_one)
{
	const VkSubmitInfo *submits;
	VkResult result = VK_SUCCESS;
	unsigned count, i;

	if (composer->failed)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	count = granite_submit_composer_bake(composer, profiling_iteration, &submits);
	if (!one_by_one)
		return table->vkQueueSubmit(queue, count, submits, fence);

	for (i = 0; i < count && result == VK_SUCCESS; i++)
		result = table->vkQueueSubmit(queue, 1, &submits[i], i + 1 == count ? fence : VK_NULL_HANDLE);
	return result;
}
