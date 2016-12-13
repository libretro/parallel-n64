#ifndef COVERAGE_H_
#define COVERAGE_H_

// RDP multisampling, the sampling pattern within a pixel is 8 samples within a 4x4 grid.
// o - o -
// - o - o
// o - o -
// - o - o
// where 'o' denotes the sampling points.
// The rasterizer quantizes the X coordinates to 1/8th of a pixel, rounds up to nearest qpel
// and creates a coverage mask from that.
uint compute_coverage(ivec4 lx, ivec4 rx, int x)
{
   int eightpel = x * 8;
   // We need 16 comparisons to form the sample mask for a pixel. 8 samples for left and right bounds.
   // Fortunately, we can vectorize this neatly.

   // The distinction between LEQ and LE ensures no overlapping coverage.
   bvec4 left0 = lessThanEqual(lx, eightpel + ivec4(0, 2, 0, 2));
   bvec4 left1 = lessThanEqual(lx, eightpel + ivec4(4, 6, 4, 6));
   bvec4 right0 = lessThan(eightpel + ivec4(0, 2, 0, 2), rx);
   bvec4 right1 = lessThan(eightpel + ivec4(4, 6, 4, 6), rx);

   // The coverage is packed as follows, doesn't really matter, but makes debugging against Angrylion easier.
   // 0x80   -   0x20   -
   //    - 0x40   -   0x10
   // 0x08   -   0x02   -
   //    - 0x04   -   0x01
   return uint(horizontalOr(bitwiseAnd(left0, right0) * ivec4(0x80, 0x40, 0x08, 0x04) +
                            bitwiseAnd(left1, right1) * ivec4(0x20, 0x10, 0x02, 0x01)));
}



#endif
