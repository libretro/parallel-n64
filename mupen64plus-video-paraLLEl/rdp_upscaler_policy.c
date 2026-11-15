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


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <compat/msvc.h>
#include "rdp_upscaler_policy.h"
#include "temporal_upscaler.h"

void rdp_upscaler_policy_init(rdp_upscaler_policy_t *p)
{
	memset(p, 0, sizeof(*p));
	p->reset_pending = true;
	p->native_width = 320;
	p->native_height = 240;
	p->said = ~0u;
}

/* The factor for the frames shown (rdp_upscaler_factor): the one chosen
 * stands while the frames stay the size it was chosen for */
static unsigned pick_factor(rdp_upscaler_policy_t *p, const struct retro_hw_upscaler_request *req,
		unsigned processor_factor)
{
	rdp_upscaler_range_t range;
	unsigned pick;
	range.want_width = req->render_width;
	range.min_width = req->min_render_width;
	range.min_height = req->min_render_height;
	range.max_width = req->max_render_width;
	range.max_height = req->max_render_height;
	pick = rdp_upscaler_factor(&range, p->native_width, p->native_height, p->on ? processor_factor : 0,
			p->picked_width, p->picked_height);
	if (pick)
	{
		p->picked_width = p->native_width;
		p->picked_height = p->native_height;
	}
	return pick;
}

rdp_upscaler_decision_t rdp_upscaler_policy_poll(rdp_upscaler_policy_t *p,
		const struct retro_hw_upscaler_request *request, unsigned option_factor,
		unsigned processor_requested, unsigned processor_factor, bool super_sampled_option)
{
	rdp_upscaler_decision_t d;
	bool on = false;

	d.factor = option_factor;
	d.requested = request != NULL;
	if (request)
	{
		unsigned pick = pick_factor(p, request, processor_factor);
		if (pick)
		{
			d.factor = pick;
			on = true;
		}
	}

	/* (super-sampled read-back is off while the upscaler is on) */
	d.remake = d.factor != processor_requested || (on != p->on && super_sampled_option && d.factor > 1);
	if (on != p->on || d.remake)
		p->reset_pending = true;
	p->on = on;
	return d;
}

unsigned rdp_upscaler_policy_jitter_phase(const rdp_upscaler_policy_t *p, unsigned output_width,
		unsigned processor_factor)
{
	float ratio, frames;
	unsigned phase;
	if (!p->native_width || !processor_factor)
		return 8;
	ratio = (float)output_width / (float)(p->native_width * processor_factor);
	/* (in float, then up: as ceilf) */
	frames = 8.0f * ratio * ratio;
	phase = (unsigned)ceil((double)frames);
	return phase < 8 ? 8 : phase > 64 ? 64 : phase;
}

bool rdp_upscaler_policy_message(rdp_upscaler_policy_t *p, bool requested, bool on, unsigned factor,
		const struct retro_hw_upscaler_request *r, char *msg, size_t len, enum retro_log_level *level)
{
	const unsigned now = (unsigned)requested | ((unsigned)on << 1) | (factor << 2);

	if (now == p->said)
		return false;
	p->said = now;
	if (!requested)
	{
		*level = RETRO_LOG_INFO;
		snprintf(msg, len, "Temporal upscaler: no request, rendering as usual.\n");
	}
	else if (!on)
	{
		*level = RETRO_LOG_WARN;
		snprintf(msg, len, "Temporal upscaler: asks for %ux%u (%ux%u to %ux%u) for %ux%u; "
				"no factor of %ux%u fits, rendering as usual.\n",
				r->render_width, r->render_height, r->min_render_width, r->min_render_height,
				r->max_render_width, r->max_render_height, r->output_width, r->output_height,
				p->native_width, p->native_height);
	}
	else
	{
		*level = RETRO_LOG_INFO;
		snprintf(msg, len, "Temporal upscaler: drawing at %ux, %ux%u, for %ux%u "
				"(asks for %ux%u, %ux%u to %ux%u).\n",
				factor, p->native_width * factor, p->native_height * factor,
				r->output_width, r->output_height, r->render_width, r->render_height,
				r->min_render_width, r->min_render_height, r->max_render_width, r->max_render_height);
	}
	return true;
}
