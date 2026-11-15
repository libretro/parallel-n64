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


#ifndef RDP_INTAKE_H
#define RDP_INTAKE_H

/* The RDP's command list as the DPC registers point at it: read from
 * RDRAM, from the RSP's DMEM (XBUS), or from a host buffer the HLE
 * emitter wrote, whole commands handed on and one cut short kept for the
 * rest of it; and the last of each state command, to give a command
 * processor made anew the state the RDP had. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* the most 64-bit words a list kept for its end may take */
#define RDP_INTAKE_WORDS (0x40000u >> 3)

typedef struct rdp_intake_callbacks
{
	/* a command of @num_words 32-bit words, opcode 8 and up */
	void (*enqueue)(void *user, unsigned num_words, const uint32_t *words);
	/* after a sync full is handed on: waits for it where the RDP is
	 * synchronous, and raises the DP interrupt */
	void (*sync_full)(void *user);
	void *user;
} rdp_intake_callbacks_t;

typedef struct rdp_intake
{
	rdp_intake_callbacks_t cb;
	/* a list's 64-bit words kept, two 32-bit words each: [cur, ptr) still
	 * to run */
	uint32_t data[2 * RDP_INTAKE_WORDS];
	int cur;
	int ptr;

	/* the HLE emitter's list, at [hle_base, hle_base + hle_len) of the
	 * RDP's addresses: NULL when none */
	const uint8_t *hle_buf;
	uint32_t hle_base;
	uint32_t hle_len;

	/* the last of each state command, and set tile and set tile size by
	 * tile */
	uint32_t state_words[64][2];
	bool state_valid[64];
	uint32_t tile_state_words[2][8][2];
	bool tile_state_valid[2][8];
} rdp_intake_t;

/* Nothing kept */
void rdp_intake_init(rdp_intake_t *in, const rdp_intake_callbacks_t *cb);
/* The state commands seen are forgotten */
void rdp_intake_forget_state(rdp_intake_t *in);

/* NULL: commands come from RDRAM again */
void rdp_intake_set_hle_buffer(rdp_intake_t *in, const uint8_t *buf, uint32_t base, uint32_t len);

/* The list from @dpc_current to @dpc_end (@dpc_status says from where:
 * DP_STATUS_XBUS_DMA, DMEM of 4 KiB at @dmem; else @rdram, 16 MiB of
 * the RDP's addresses). False when it is not read: empty, too long to
 * keep, or out of range; true when it was, and DPC_CURRENT is to become
 * DPC_END. */
bool rdp_intake_process(rdp_intake_t *in, uint32_t dpc_current, uint32_t dpc_end, uint32_t dpc_status,
		const uint8_t *rdram, const uint8_t *dmem);

/* Hands the state commands seen to the callbacks again, in an order that
 * sets each before what depends on it */
void rdp_intake_replay_state(rdp_intake_t *in);

/* How many 64-bit words the command of @opcode is */
unsigned rdp_intake_command_words(unsigned opcode);

RETRO_END_DECLS

#endif
