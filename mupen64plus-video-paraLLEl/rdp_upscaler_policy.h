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


#ifndef RDP_UPSCALER_POLICY_H
#define RDP_UPSCALER_POLICY_H

/* What the frontend's temporal upscaler is given: whether frames are
 * drawn for it, at which factor, how many frames the jitter takes, and
 * what is said of it. C89. */

#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <libretro.h>

RETRO_BEGIN_DECLS

typedef struct rdp_upscaler_policy
{
	/* frames are drawn for the upscaler */
	bool on;
	/* the next frame does not follow the last one */
	bool reset_pending;
	/* the framebuffer last shown, in native pixels, and the size the
	 * factor was chosen for */
	unsigned native_width;
	unsigned native_height;
	unsigned picked_width;
	unsigned picked_height;
	/* what was said last */
	unsigned said;
} rdp_upscaler_policy_t;

/* What a frame is drawn as */
typedef struct rdp_upscaler_decision
{
	unsigned factor;
	/* the upscaler asked for frames */
	bool requested;
	/* the command processor is made anew for it */
	bool remake;
} rdp_upscaler_decision_t;

/* Off, 320x240 shown, a reset to come */
void rdp_upscaler_policy_init(rdp_upscaler_policy_t *p);

/* Once a frame, before it runs: @request (NULL: none) picks the factor
 * and turns the upscaler on, the one chosen standing while the frames
 * stay the size it was chosen for; else @option_factor. The command
 * processor, made for @processor_requested and rendering at
 * @processor_factor, is made anew for another factor, or where turning
 * the upscaler on or off turns the super-sampled read-back
 * (@super_sampled_option) off or on. */
rdp_upscaler_decision_t rdp_upscaler_policy_poll(rdp_upscaler_policy_t *p,
		const struct retro_hw_upscaler_request *request, unsigned option_factor,
		unsigned processor_requested, unsigned processor_factor, bool super_sampled_option);

/* Frames of the Halton (2, 3) jitter: what the ratio of the output to
 * the render size wants, at least 8, at most 64 */
unsigned rdp_upscaler_policy_jitter_phase(const rdp_upscaler_policy_t *p, unsigned output_width,
		unsigned processor_factor);

/* What to say of what the upscaler asked for (@request: the last one)
 * and what came of it, when that changes: false when nothing changed.
 * @level: a retro_log_level. */
bool rdp_upscaler_policy_message(rdp_upscaler_policy_t *p, bool requested, bool on, unsigned factor,
		const struct retro_hw_upscaler_request *request, char *msg, size_t len, enum retro_log_level *level);

RETRO_END_DECLS

#endif
