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
#include "rdp_submit.h"

bool rdp_submit_due(const rdp_submit_counts_t *c, bool super_sampled, bool gpu_idle, uint64_t ns_since_submit)
{
	/* Not too long without a submission, nor so many submissions that the
	 * GPU sits in bubbles: a run of small passes goes some at a time, and
	 * two full passes' primitives mean the RDP is busy. */
	return c->passes >= RDP_SUBMIT_MAX_PENDING_PASSES ||
			(super_sampled && c->passes_upscaled >= RDP_SUBMIT_MAX_PENDING_PASSES) ||
			c->primitives >= RDP_SUBMIT_MAX_PENDING_PRIMITIVES ||
			c->primitives_upscaled >= RDP_SUBMIT_MAX_PENDING_PRIMITIVES ||
			gpu_idle ||
			ns_since_submit > RDP_SUBMIT_MAX_NS;
}

bool rdp_submit_idle_due(const rdp_submit_counts_t *c)
{
	return c->primitives >= RDP_SUBMIT_IDLE_PRIMITIVES || c->passes >= RDP_SUBMIT_IDLE_PASSES;
}

rdp_submit_taken_t rdp_submit_take(rdp_submit_counts_t *c, bool super_sampled)
{
	rdp_submit_taken_t t;
	t.host_visible = (super_sampled ? c->passes_upscaled : c->passes) != 0;
	t.upscaled = c->passes_upscaled != 0;
	memset(c, 0, sizeof(*c));
	return t;
}

void rdp_submit_end_barrier(granite_command_buffer_t *cb, const rdp_submit_taken_t *t, bool host_reads)
{
	VkMemoryBarrier global;
	memset(&global, 0, sizeof(global));
	global.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	/* (an upscaled pass after the native one flushed memory already) */
	global.srcAccessMask = t->host_visible && !t->upscaled ? VK_ACCESS_MEMORY_WRITE_BIT : 0;
	global.dstAccessMask = host_reads ? VK_ACCESS_HOST_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT;
	granite_cb_barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			host_reads ? VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT, 1, &global, 0, NULL, 0, NULL);
}
