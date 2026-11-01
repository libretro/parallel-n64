/* test_rdp_x_range -- parallel-RDP's X overflow test for tile binning.
 *
 * compute_conservative_max_num_tiles sizes a primitive's tile work from
 * its X span, and assumes the whole scissor when the span overflows the
 * rasterizer's range. Interpolation clamps to int32, so a span can start
 * at INT_MIN, where std::abs is undefined and, as it wraps, negative:
 * that overflow went undetected and the estimate came out too small.
 */
#include <climits>
#include <cstdio>

#include "rdp_data_structures.hpp"

struct Case
{
	int32_t start_x, end_x, limit;
	bool exceeds;
};

int main()
{
	static const Case cases[] = {
		{ 0, 2047, 2047, false },
		{ -2047, 2047, 2047, false },
		{ -2048, 0, 2047, true },
		{ 0, 2048, 2047, true },
		{ INT_MIN, 10, 2047, true },
		{ 10, INT_MIN, 2047, true },
		{ INT_MIN, INT_MAX, 2047 * 8, true },
		{ -16376, 16376, 2047 * 8, false },
		{ -16377, 0, 2047 * 8, true },
	};
	int fails = 0;
	for (const Case &c : cases)
	{
		bool got = RDP::x_range_exceeds(c.start_x, c.end_x, c.limit);
		if (got != c.exceeds)
		{
			fprintf(stderr, "FAIL: span %d..%d against %d: %d, expected %d\n",
			        c.start_x, c.end_x, c.limit, int(got), int(c.exceeds));
			fails++;
		}
	}
	printf("test_rdp_x_range: %u cases, %d wrong\n", unsigned(sizeof(cases) / sizeof(cases[0])), fails);
	return fails ? 1 : 0;
}
