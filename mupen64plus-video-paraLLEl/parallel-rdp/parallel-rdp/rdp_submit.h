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


#ifndef RDP_SUBMIT_H
#define RDP_SUBMIT_H

/* When the renderer's recorded work goes to the GPU, and what the
 * submission has to make visible. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "command_buffer.h"

RETRO_BEGIN_DECLS

/* A flush every this many passes, when they come in a row */
#define RDP_SUBMIT_MAX_PENDING_PASSES 8u
/* or this many primitives */
#define RDP_SUBMIT_MAX_PENDING_PRIMITIVES 256u
/* or when nothing was submitted for this long */
#define RDP_SUBMIT_MAX_NS 1000000u
/* With the command thread idle: at least this many primitives or passes */
#define RDP_SUBMIT_IDLE_PRIMITIVES 32u
#define RDP_SUBMIT_IDLE_PASSES 2u

/* What was recorded since the last submission */
typedef struct rdp_submit_counts
{
	unsigned passes;
	unsigned passes_upscaled;
	unsigned primitives;
	unsigned primitives_upscaled;
} rdp_submit_counts_t;

/* Whether to submit now: enough passes or primitives to keep the GPU
 * fed without submitting too often, the GPU idle (@gpu_idle), or
 * nothing submitted for a while (@ns_since_submit). The upscaled passes
 * count too where only they are drawn (@super_sampled). */
bool rdp_submit_due(const rdp_submit_counts_t *c, bool super_sampled, bool gpu_idle, uint64_t ns_since_submit);

/* With the command thread idle: whether there is enough to be worth a
 * flush and a submission */
bool rdp_submit_idle_due(const rdp_submit_counts_t *c);

/* What a submission carries */
typedef struct rdp_submit_taken
{
	/* passes whose results the CPU reads: the native ones, or the
	 * upscaled ones where only they are drawn */
	bool host_visible;
	/* upscaled passes, which flushed their memory already */
	bool upscaled;
} rdp_submit_taken_t;

/* What the counts come to, and the counts back to none */
rdp_submit_taken_t rdp_submit_take(rdp_submit_counts_t *c, bool super_sampled);

/* The barrier at the end of a submission: the passes' writes made
 * available to the host where it reads RDRAM itself (@host_reads), or to
 * the copy that reads them back */
void rdp_submit_end_barrier(granite_command_buffer_t *cb, const rdp_submit_taken_t *t, bool host_reads);

RETRO_END_DECLS

#endif
