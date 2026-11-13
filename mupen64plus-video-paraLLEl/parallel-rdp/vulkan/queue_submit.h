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

#ifndef GRANITE_QUEUE_SUBMIT_H
#define GRANITE_QUEUE_SUBMIT_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "semaphore.h"

RETRO_BEGIN_DECLS

#define GRANITE_QUEUE_MAX_WAITS 32

/* The semaphores a queue's next submission waits for. Fixed in size:
 * nothing is allocated when one is added. Owner thread only. */
typedef struct granite_queue_waits
{
	granite_semaphore_t *semaphores[GRANITE_QUEUE_MAX_WAITS];
	VkPipelineStageFlags stages[GRANITE_QUEUE_MAX_WAITS];
	unsigned count;
} granite_queue_waits_t;

/* The queue's next submission waits for @semaphore at @stages; the list
 * takes a reference. False when the list is full: submit the queue,
 * which empties it, and add again. */
bool granite_queue_waits_add(granite_queue_waits_t *waits, granite_semaphore_t *semaphore,
		VkPipelineStageFlags stages);
/* All of them into the submission composed in @composer, the binary
 * ones first and then the timeline values, and the list is empty. */
void granite_queue_waits_collect(granite_queue_waits_t *waits, granite_submit_composer_t *composer,
		granite_frame_garbage_t *garbage);
/* The device is idle and nothing will wait for them: a binary semaphore
 * of its object's own is destroyed at once, and the list is empty. */
void granite_queue_waits_clear(granite_queue_waits_t *waits, VkDevice device, const struct VolkDeviceTable *table);

/* How a buffer filled on one queue gets to the others */
typedef struct granite_staging_plan
{
	/* the filling commands end with a barrier from the transfer write
	 * to these; none when @barrier_stages is 0 */
	VkPipelineStageFlags barrier_stages;
	VkAccessFlags barrier_access;
	/* the submission signals a semaphore the graphics queue waits for
	 * at @graphics_stages, and one the compute queue waits for at
	 * @compute_stages: in that order when both */
	bool wait_graphics;
	bool wait_compute;
	VkPipelineStageFlags graphics_stages;
	VkPipelineStageFlags compute_stages;
} granite_staging_plan_t;

/* @src is the queue the buffer is filled on; @stages and @access are
 * all it can be used with afterwards. */
void granite_staging_plan(VkQueue src, VkQueue graphics, VkQueue compute, VkPipelineStageFlags stages,
		VkAccessFlags access, granite_staging_plan_t *plan);

RETRO_END_DECLS

#endif
