/* parallel-RDP's masked readback copy (parallel-rdp/masked_copy.c).
 *
 * What the GPU wrote is copied back to RDRAM byte by byte under a mask:
 * a byte goes over where the top bit of its mask byte is set and the
 * destination is left alone elsewhere. Built twice, with the SSE2 path
 * (where the host has it) and with the portable loop
 * (-DRDP_MASKED_COPY_SCALAR): both must do exactly that, for masks that
 * are all set, all clear, and anything between, at every size and
 * alignment, and write nothing outside the range. */
#include <stdio.h>
#include <string.h>

#include "masked_copy.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_masked_copy: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define MAX_SIZE 512
#define GUARD 32

static uint64_t rng = ((uint64_t)0x12345678u << 32) | 0x9abcdef1u;
static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32_t)(rng >> 16);
}

static unsigned run(unsigned size, unsigned align, unsigned kind)
{
	static uint8_t dst[GUARD + MAX_SIZE + GUARD + 16], want[GUARD + MAX_SIZE + GUARD + 16];
	static uint8_t data[MAX_SIZE + 16], mask[MAX_SIZE + 16];
	unsigned i, written = 0;

	for (i = 0; i < sizeof(dst); i++)
		dst[i] = (uint8_t)rnd();
	memcpy(want, dst, sizeof(dst));
	for (i = 0; i < size; i++)
	{
		data[align + i] = (uint8_t)rnd();
		switch (kind)
		{
			case 0: /* all written */
				mask[align + i] = 0xff;
				break;
			case 1: /* none */
				mask[align + i] = 0;
				break;
			case 2: /* bytes here and there, as the GPU leaves them */
				mask[align + i] = (rnd() & 3) ? 0 : 0xff;
				break;
			case 3: /* whole chunks */
				mask[align + i] = ((i / 16) % 3) ? 0xff : 0;
				break;
			case 4: /* the first half of each chunk */
				mask[align + i] = (i % 16) < 8 ? 0xff : 0;
				break;
			case 5: /* the second */
				mask[align + i] = (i % 16) < 8 ? 0 : 0xff;
				break;
			case 6: /* all but one byte of each word */
				mask[align + i] = (i % 4) == (i / 4) % 4 ? 0 : 0xff;
				break;
			default: /* any byte: only its top bit counts */
				mask[align + i] = (uint8_t)rnd();
				break;
		}
		if (mask[align + i] & 0x80)
		{
			want[GUARD + align + i] = data[align + i];
			written++;
		}
	}

	rdp_masked_copy(dst + GUARD + align, data + align, mask + align, size);
	rdp_masked_copy_end();
	if (memcmp(dst, want, sizeof(dst)))
		return ~0u;
	return written;
}

int main(void)
{
	unsigned size, align, kind, bad = 0, written = 0, kept = 0;

	for (kind = 0; kind < 8; kind++)
		for (size = 0; size <= MAX_SIZE; size += 16)
			for (align = 0; align < 16; align++)
			{
				unsigned n = run(size, align, kind);
				if (n == ~0u)
					bad++;
				else
				{
					written += n;
					kept += size - n;
				}
				if (kind == 0 && n != size)
					bad++;
				if (kind == 1 && n != 0)
					bad++;
			}
	CHECK(bad == 0, "a byte is copied where its mask's top bit is set, and nothing else is touched");
	CHECK(written > 100000 && kept > 100000, "(bytes were copied and bytes were kept)");

	if (failures)
	{
		fprintf(stderr, "test_masked_copy: %u failure(s)\n", failures);
		return 1;
	}
#ifdef RDP_MASKED_COPY_SCALAR
	printf("test_masked_copy_scalar: ok\n");
#else
	printf("test_masked_copy: ok\n");
#endif
	return 0;
}
