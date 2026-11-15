/* test_rdp_upscaler_policy -- what the frontend's temporal upscaler is
 * given (mupen64plus-video-paraLLEl/rdp_upscaler_policy.c).
 *
 * - without a request: the option's factor, off, the processor made anew
 *   only for another factor;
 * - with one: the factor it fits, on, a reset; the factor stands while
 *   the frames stay the size it was chosen for; a request nothing fits
 *   is as none;
 * - turning on or off makes the processor anew where it turns the
 *   super-sampled read-back off or on, and only then;
 * - the jitter's frames: 8 for frames at the output's size, more as the
 *   output outgrows them, 64 at most;
 * - each change said once, at its level.
 *
 * Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <string.h>

#include "rdp_upscaler_policy.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_upscaler_policy: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static struct retro_hw_upscaler_request request(unsigned render_width, unsigned render_height)
{
	struct retro_hw_upscaler_request r;
	memset(&r, 0, sizeof(r));
	r.output_width = 2 * render_width;
	r.output_height = 2 * render_height;
	r.render_width = render_width;
	r.render_height = render_height;
	r.min_render_width = render_width / 2;
	r.min_render_height = render_height / 2;
	r.max_render_width = render_width * 2;
	r.max_render_height = render_height * 2;
	return r;
}

static void test_poll(void)
{
	rdp_upscaler_policy_t p;
	struct retro_hw_upscaler_request r = request(640, 480), none;
	rdp_upscaler_decision_t d;

	rdp_upscaler_policy_init(&p);
	CHECK(!p.on && p.reset_pending && p.native_width == 320 && p.native_height == 240, "off, 320x240, a reset");
	p.reset_pending = false;

	d = rdp_upscaler_policy_poll(&p, NULL, 1, 1, 1, false);
	CHECK(d.factor == 1 && !d.requested && !d.remake && !p.on && !p.reset_pending,
			"without a request: the option's factor, nothing made anew");
	d = rdp_upscaler_policy_poll(&p, NULL, 4, 1, 1, false);
	CHECK(d.factor == 4 && d.remake && p.reset_pending, "another option is made anew");
	p.reset_pending = false;

	d = rdp_upscaler_policy_poll(&p, &r, 1, 4, 4, false);
	CHECK(d.requested && d.factor == 2 && p.on && d.remake && p.reset_pending,
			"a request: the factor it fits, on, made anew, a reset");
	p.reset_pending = false;
	d = rdp_upscaler_policy_poll(&p, &r, 1, 2, 2, false);
	CHECK(d.factor == 2 && p.on && !d.remake && !p.reset_pending, "and it stands, frame after frame");

	/* the frames grow to 640x480: 2x would still fit, and stands */
	p.native_width = 640;
	p.native_height = 480;
	d = rdp_upscaler_policy_poll(&p, &r, 1, 2, 2, false);
	CHECK(d.factor == 2 && !d.remake, "the factor stands while it fits");
	/* to 800x600: 2x no longer fits */
	p.native_width = 800;
	p.native_height = 600;
	d = rdp_upscaler_policy_poll(&p, &r, 1, 2, 2, false);
	CHECK(d.factor == 1 && d.remake && p.picked_width == 800, "for frames it does not fit another factor");

	/* a request nothing fits */
	p.native_width = 320;
	p.native_height = 240;
	none = request(64, 48);
	none.max_render_width = 100;
	none.max_render_height = 100;
	p.reset_pending = false;
	d = rdp_upscaler_policy_poll(&p, &none, 1, 1, 1, false);
	CHECK(d.requested && !p.on && d.factor == 1 && p.reset_pending, "a request nothing fits: off, as none");

	/* on and off at the same factor, with the super-sampled read-back */
	rdp_upscaler_policy_init(&p);
	d = rdp_upscaler_policy_poll(&p, &r, 2, 2, 2, false);
	CHECK(p.on && !d.remake, "on at the option's own factor: nothing made anew");
	d = rdp_upscaler_policy_poll(&p, NULL, 2, 2, 2, false);
	CHECK(!p.on && !d.remake && p.reset_pending, "nor off");
	d = rdp_upscaler_policy_poll(&p, &r, 2, 2, 2, true);
	CHECK(p.on && d.remake, "but with the super-sampled read-back, which it turns off, it is");
	d = rdp_upscaler_policy_poll(&p, &r, 2, 2, 2, true);
	CHECK(p.on && !d.remake, "once");
	d = rdp_upscaler_policy_poll(&p, NULL, 2, 2, 2, true);
	CHECK(!p.on && d.remake, "and off again");
	rdp_upscaler_policy_init(&p);
	d = rdp_upscaler_policy_poll(&p, NULL, 1, 1, 1, true);
	p.native_width = 640;
	p.native_height = 480;
	d = rdp_upscaler_policy_poll(&p, &r, 1, 1, 1, true);
	CHECK(p.on && d.factor == 1 && !d.remake, "at 1x there is no read-back to turn off");
}

static void test_jitter(void)
{
	rdp_upscaler_policy_t p;
	rdp_upscaler_policy_init(&p);
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 640, 2) == 8, "frames at the output's size: 8");
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 320, 2) == 8, "smaller: still 8");
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 1280, 2) == 32, "twice the size: 32");
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 3840, 2) == 64, "at most 64");
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 900, 2) == 16, "rounded up: 8 x 1.977 = 15.8, 16");
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 1280, 0) == 8, "without a factor, 8");
	p.native_width = 0;
	CHECK(rdp_upscaler_policy_jitter_phase(&p, 1280, 2) == 8, "without frames, 8");
}

static void test_messages(void)
{
	rdp_upscaler_policy_t p;
	struct retro_hw_upscaler_request r = request(640, 480);
	enum retro_log_level level;
	char msg[512];

	rdp_upscaler_policy_init(&p);
	CHECK(rdp_upscaler_policy_message(&p, false, false, 1, &r, msg, sizeof(msg), &level)
			&& level == RETRO_LOG_INFO && strstr(msg, "no request"), "no request, said");
	CHECK(!rdp_upscaler_policy_message(&p, false, false, 1, &r, msg, sizeof(msg), &level), "once");
	CHECK(rdp_upscaler_policy_message(&p, true, false, 1, &r, msg, sizeof(msg), &level)
			&& level == RETRO_LOG_WARN && strstr(msg, "asks for 640x480 (320x240 to 1280x960) for 1280x960")
			&& strstr(msg, "no factor of 320x240 fits"), "a request nothing fits, warned, with the sizes");
	CHECK(rdp_upscaler_policy_message(&p, true, true, 2, &r, msg, sizeof(msg), &level)
			&& level == RETRO_LOG_INFO && strstr(msg, "drawing at 2x, 640x480, for 1280x960"), "drawing for it");
	CHECK(!rdp_upscaler_policy_message(&p, true, true, 2, &r, msg, sizeof(msg), &level), "once");
	CHECK(rdp_upscaler_policy_message(&p, true, true, 4, &r, msg, sizeof(msg), &level)
			&& strstr(msg, "drawing at 4x, 1280x960"), "another factor said");
}

int main(void)
{
	test_poll();
	test_jitter();
	test_messages();
	if (failures)
	{
		fprintf(stderr, "test_rdp_upscaler_policy: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_upscaler_policy: ok\n");
	return 0;
}
