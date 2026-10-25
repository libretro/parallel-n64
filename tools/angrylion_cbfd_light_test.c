/* angrylion_cbfd_light_test -- Conker's Bad Fur Day vertex lighting
 * follows the F3DEXBG point-light model.
 *
 * The microcode starts from the ambient (the top light slot) in the
 * colour lanes as byte << 7, adds each point light below the slot under
 * it, min(rgb, rgb * ca / |p - light|^2), and multiplies the vertex's own
 * colour by the sum with VMULF before the byte store. The HLE used to
 * approximate it with the stock directional dot and an integer
 * colour * light / 255, which lit the intro's N logo and Conker himself
 * visibly darker than the LLE RSP.
 *
 *  - ambient only: a full ambient keeps 127 of a 128 colour byte (the
 *    VMULF rounding), where colour * 255 / 255 kept 128;
 *  - a point light at the vertex adds its whole colour;
 *  - a point light far away adds nothing.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_frontend.h"
#include "rdp_emit_rsp.h"
#include <stdio.h>
#include <string.h>

int s2dex1_ucode_match(const unsigned char *r, unsigned int size,
                       unsigned int ud, unsigned int ut)
{
    (void)r; (void)size; (void)ud; (void)ut;
    return 0;
}
void gsp_detect_ucode_params(GSPState *st, const unsigned char *r,
                             unsigned int size, unsigned int ud,
                             unsigned int ut)
{
    (void)st; (void)r; (void)size; (void)ud; (void)ut;
}

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

int main(void)
{
    int32_t clip[4] = { 0x00100000, 0x00200000, 0x00300000, 0x00400000 };
    int32_t coord[16];
    int32_t positions[3][4];
    int32_t rgb[3][3];
    int32_t ca[3] = { 0x10, 0, 0 };
    int32_t source[3] = { 128, 255, 255 };
    int32_t out[3];
    int k;

    /* a zero coordinate scale puts the vertex at the origin */
    memset(coord, 0, sizeof(coord));
    memset(positions, 0, sizeof(positions));

    /* one light: the ambient in slot 1 */
    for (k = 0; k < 3; k++)
    {
        rgb[0][k] = 0;
        rgb[1][k] = 0xff;
    }
    rsp_cbfd_light(clip, coord, (const int32_t (*)[4])positions,
                   (const int32_t (*)[3])rgb, ca, 1, source, out);
    CHECK(out[0] == 127 && out[1] == 254, "a full ambient keeps colour * 0x7f80 >> 15");

    /* ambient 0x40 in slot 2, a 0x40 point light in slot 0 at the vertex */
    for (k = 0; k < 3; k++)
    {
        rgb[0][k] = 0x40;
        rgb[1][k] = 0xff;      /* the slot below the ambient is not a point light */
        rgb[2][k] = 0x40;
    }
    rsp_cbfd_light(clip, coord, (const int32_t (*)[4])positions,
                   (const int32_t (*)[3])rgb, ca, 2, source, out);
    CHECK(out[1] == 127, "a point light at the vertex adds its whole colour");

    positions[0][0] = 0x4000;
    positions[0][1] = 0x4000;
    rsp_cbfd_light(clip, coord, (const int32_t (*)[4])positions,
                   (const int32_t (*)[3])rgb, ca, 2, source, out);
    CHECK(out[1] == 63, "a point light far away adds nothing");

    if (s_failures)
    {
        printf("angrylion_cbfd_light_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("angrylion_cbfd_light_test: all checks passed\n");
    return 0;
}
