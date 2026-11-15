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
#include "rdp_intake.h"

#define DP_STATUS_XBUS_DMA 0x01u
#define SYNC_FULL 0x29u

static const unsigned char command_words[64] = {
	1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1
};

unsigned rdp_intake_command_words(unsigned opcode)
{
	return command_words[opcode & 63];
}

void rdp_intake_init(rdp_intake_t *in, const rdp_intake_callbacks_t *cb)
{
	memset(in, 0, sizeof(*in));
	in->cb = *cb;
}

void rdp_intake_forget_state(rdp_intake_t *in)
{
	memset(in->state_valid, 0, sizeof(in->state_valid));
	memset(in->tile_state_valid, 0, sizeof(in->tile_state_valid));
}

void rdp_intake_set_hle_buffer(rdp_intake_t *in, const uint8_t *buf, uint32_t base, uint32_t len)
{
	in->hle_buf = buf;
	in->hle_base = base;
	in->hle_len = len;
}

/* Keeps the state a processor made anew is to be given */
static void keep_state(rdp_intake_t *in, unsigned op, const uint32_t *w)
{
	if (op == 0x35 || op == 0x32)
	{
		/* set tile, set tile size: by tile */
		const unsigned set = op == 0x35 ? 0 : 1;
		const unsigned tile = (w[1] >> 24) & 7;
		in->tile_state_words[set][tile][0] = w[0];
		in->tile_state_words[set][tile][1] = w[1];
		in->tile_state_valid[set][tile] = true;
	}
	/* (not the syncs, loads or rectangles) */
	else if (op >= 0x2a && op != 0x30 && op != 0x33 && op != 0x34 && op != 0x36)
	{
		in->state_words[op][0] = w[0];
		in->state_words[op][1] = w[1];
		in->state_valid[op] = true;
	}
}

/* Runs the whole commands at the start of @words, @count 64-bit words,
 * and returns how many words they took: a command cut short at the end
 * is left for when the rest of it comes */
static int run_commands(rdp_intake_t *in, const uint32_t *words, int count)
{
	int cur = 0;
	while (cur < count)
	{
		const uint32_t *w = words + 2 * cur;
		const unsigned op = (w[0] >> 24) & 63;
		const int length = (int)command_words[op];

		if (count - cur < length)
			break;
		if (op >= 8)
			in->cb.enqueue(in->cb.user, (unsigned)length * 2, w);
		keep_state(in, op, w);
		if (op == SYNC_FULL)
			in->cb.sync_full(in->cb.user);
		cur += length;
	}
	return cur;
}

static void read_word(rdp_intake_t *in, const uint8_t *p)
{
	memcpy(&in->data[2 * in->ptr], p, 8);
	in->ptr++;
}

bool rdp_intake_process(rdp_intake_t *in, uint32_t dpc_current, uint32_t dpc_end, uint32_t dpc_status,
		const uint8_t *rdram, const uint8_t *dmem)
{
	const uint32_t current = dpc_current & 0x00fffff8u;
	const uint32_t end = dpc_end & 0x00fffff8u;
	const bool xbus = (dpc_status & DP_STATUS_XBUS_DMA) != 0;
	uint32_t offset = current;
	bool from_hle;
	int length;

	if (end <= current)
		return false;
	length = (int)((end - current) >> 3);
	if ((in->ptr + length) & ~(int)(0x0003ffff >> 3))
		return false;

	/* the HLE emitter's list, where it lies at the RDP's addresses */
	from_hle = !xbus && in->hle_buf && current >= in->hle_base && current - in->hle_base < in->hle_len;

	/* A list in one piece of memory, with nothing left over from the last
	 * one, is run where it is; only a command it cuts short is kept */
	if (!xbus && in->ptr == 0)
	{
		const uint8_t *direct = NULL;
		if (from_hle)
		{
			if ((current - in->hle_base) + (uint32_t)length * 8u <= in->hle_len)
				direct = in->hle_buf + (current - in->hle_base);
		}
		else if (current + (uint32_t)length * 8u <= 0x1000000u)
			direct = rdram + current;

		if (direct)
		{
			const uint32_t *words = (const uint32_t *)(const void *)direct;
			const int done = run_commands(in, words, length);
			if (done < length)
				memcpy(in->data, words + 2 * done, (size_t)(length - done) * 8);
			in->ptr = length - done;
			in->cur = 0;
			return true;
		}
	}

	if (xbus)
	{
		/* DMEM wraps at 4 KiB */
		do
		{
			offset &= 0xff8;
			read_word(in, dmem + offset);
			offset += 8;
		} while (--length > 0);
	}
	else
	{
		do
		{
			if (from_hle)
			{
				const uint32_t rel = offset - in->hle_base;
				if (rel + 8 > in->hle_len)
					break;
				read_word(in, in->hle_buf + rel);
			}
			else
			{
				offset &= 0xfffff8;
				read_word(in, rdram + offset);
			}
			offset += 8;
		} while (--length > 0);
	}

	in->cur += run_commands(in, &in->data[2 * in->cur], in->ptr - in->cur);
	if (in->cur == in->ptr)
	{
		in->ptr = 0;
		in->cur = 0;
	}
	return true;
}

void rdp_intake_replay_state(rdp_intake_t *in)
{
	/* the images, other modes, combine, scissor, convert, keys, colors and
	 * the primitive depth, then the tiles */
	static const unsigned char order[] = {
		0x3f, 0x3e, 0x3d, 0x2f, 0x3c, 0x2d, 0x2c, 0x2a, 0x2b,
		0x37, 0x38, 0x39, 0x3a, 0x3b, 0x2e
	};
	unsigned i, set, tile;

	for (i = 0; i < sizeof(order); i++)
		if (in->state_valid[order[i]])
			in->cb.enqueue(in->cb.user, 2, in->state_words[order[i]]);
	for (set = 0; set < 2; set++)
		for (tile = 0; tile < 8; tile++)
			if (in->tile_state_valid[set][tile])
				in->cb.enqueue(in->cb.user, 2, in->tile_state_words[set][tile]);
}
