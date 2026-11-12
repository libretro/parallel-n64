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

#ifndef GRANITE_SUBMIT_BATCH_H
#define GRANITE_SUBMIT_BATCH_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

#define GRANITE_SUBMIT_MAX_BATCHES 8

typedef struct granite_submit_batch
{
	/* where its waits, command buffers and signals begin; they end where
	 * the next batch's begin */
	size_t first_wait;
	size_t first_cmd;
	size_t first_signal;
	/* a semaphore with a timeline value, and one without, among its
	 * waits and signals */
	bool has_timeline;
	bool has_binary;
} granite_submit_batch_t;

/* Puts together what goes to a queue in one submission: the semaphores
 * to wait for, the command buffers, the semaphores to signal.
 *
 * Vulkan orders a batch as waits, then command buffers, then signals, so
 * whatever is added out of that order begins another batch. With @split
 * (a driver that cannot take binary and timeline semaphores in one
 * batch) a semaphore of the other kind begins another batch as well.
 *
 * It is reused from one submission to the next and keeps its storage:
 * once it has grown to what a frame submits, composing allocates
 * nothing. One thread's. */
typedef struct granite_submit_composer
{
	VkSemaphore *waits;
	VkPipelineStageFlags *wait_stages;
	/* the timeline value to wait for; 0 for a binary semaphore */
	uint64_t *wait_values;
	size_t num_waits;
	size_t cap_waits;

	VkCommandBuffer *cmds;
	size_t num_cmds;
	size_t cap_cmds;

	VkSemaphore *signals;
	/* the timeline value to signal; 0 for a binary semaphore */
	uint64_t *signal_values;
	size_t num_signals;
	size_t cap_signals;

	granite_submit_batch_t batches[GRANITE_SUBMIT_MAX_BATCHES];
	unsigned num_batches;
	bool split;
	/* something could not be added for lack of memory: the submission
	 * is refused */
	bool failed;

	VkSubmitInfo submits[GRANITE_SUBMIT_MAX_BATCHES];
	VkTimelineSemaphoreSubmitInfoKHR timeline_infos[GRANITE_SUBMIT_MAX_BATCHES];
	VkPerformanceQuerySubmitInfoKHR profiling_infos[GRANITE_SUBMIT_MAX_BATCHES];
} granite_submit_composer_t;

void granite_submit_composer_init(granite_submit_composer_t *composer);
void granite_submit_composer_free(granite_submit_composer_t *composer);
/* Starts a new submission: one empty batch */
void granite_submit_composer_begin(granite_submit_composer_t *composer, bool split);

/* What follows goes into a batch of its own, unless the current one is
 * empty. Past GRANITE_SUBMIT_MAX_BATCHES the last batch is carried on. */
void granite_submit_composer_begin_batch(granite_submit_composer_t *composer);
/* @value: the timeline value to wait for, 0 for a binary semaphore */
void granite_submit_composer_add_wait(granite_submit_composer_t *composer, VkSemaphore semaphore,
		VkPipelineStageFlags stages, uint64_t value);
void granite_submit_composer_add_command_buffer(granite_submit_composer_t *composer, VkCommandBuffer cmd);
/* @value: the timeline value to signal, 0 for a binary semaphore */
void granite_submit_composer_add_signal(granite_submit_composer_t *composer, VkSemaphore semaphore,
		uint64_t value);

/* The VkSubmitInfos of the batches that have something in them, in
 * @submits; returns how many. @profiling_iteration: the counter pass of
 * a performance query, or negative. Valid until the next begin. */
unsigned granite_submit_composer_bake(granite_submit_composer_t *composer, int profiling_iteration,
		const VkSubmitInfo **submits);
/* Bakes and submits to @queue, with @fence on the last batch. With
 * @one_by_one each batch is its own vkQueueSubmit, for the drivers that
 * @split is for. VK_ERROR_OUT_OF_HOST_MEMORY, and nothing submitted,
 * when something could not be added. */
VkResult granite_submit_composer_submit(granite_submit_composer_t *composer,
		const struct VolkDeviceTable *table, VkQueue queue, VkFence fence, int profiling_iteration,
		bool one_by_one);

RETRO_END_DECLS

#endif
