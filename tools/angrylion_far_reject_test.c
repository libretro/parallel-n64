/* angrylion_far_reject_test -- far-plane rejection follows the microcode,
 * not its banner.
 *
 * Stock F3DEX2 builds are named "F3DEX ... fifo 2.0x" without the '2',
 * the same banner shape as Star Wars Episode I Racer's custom build that
 * computes no +z outcode. A stock resident whose triangle entry ANDs the
 * three outcodes with 0x7070 must keep far rejection; the same banner over
 * a text segment without that entry must drop it; a "F3DEX2" or "F3DZEX"
 * banner never drops it.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_frontend.h"
#include <stdio.h>
#include <string.h>

#define RDRAM_SIZE (64u * 1024u)
#define UDATA      0x4000u
#define UTEXT      0x8000u

static unsigned char rdram[RDRAM_SIZE];
static int failures;

/* entry points of the HLE activation glue the walker objects reference */
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

static void put8(unsigned int addr, unsigned int v)
{
    rdram[addr ^ 3u] = (unsigned char)(v & 0xffu);
}

static void put32(unsigned int addr, unsigned int v)
{
    put8(addr + 0u, v >> 24);
    put8(addr + 1u, v >> 16);
    put8(addr + 2u, v >> 8);
    put8(addr + 3u, v);
}

static void banner(const char *text)
{
    unsigned int i, n = (unsigned int)strlen(text);
    for (i = 0; i < n; i++)
        put8(UDATA + 0x100u + i, (unsigned char)text[i]);
}

static void tri_entry(unsigned int mask)
{
    unsigned int at = UTEXT + 0x400u;
    put32(at +  0u, 0x8c250024u);   /* lw   a1,0x24(at) */
    put32(at +  8u, 0x8c460024u);   /* lw   a2,0x24(v0) */
    put32(at + 16u, 0x8c670024u);   /* lw   a3,0x24(v1) */
    put32(at + 24u, 0x30ab0000u | mask); /* andi t3,a1,mask */
    put32(at + 32u, 0x00cb5824u);   /* and  t3,a2,t3 */
    put32(at + 40u, 0x00eb5824u);   /* and  t3,a3,t3 */
}

static void check(int got, int want, const char *what)
{
    if (got != want)
    {
        printf("FAIL: %s (got %d, want %d)\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    memset(rdram, 0, sizeof(rdram));
    banner("RSP Gfx ucode F3DEX       fifo 2.08  Yoshitaka Yasumoto 1999 Nintendo.");
    tri_entry(0x7070u);
    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, UDATA, UTEXT), 0,
          "stock F3DEX fifo build with a +z reject keeps the far plane");

    tri_entry(0x70b0u);
    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, UDATA, UTEXT), 0,
          "0x70b0 reject mask keeps the far plane");

    memset(rdram, 0, sizeof(rdram));
    banner("RSP Gfx ucode F3DEX.NoN   fifo 2.08  Yoshitaka Yasumoto 1999 Nintendo.");
    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, UDATA, UTEXT), 1,
          "F3DEX fifo banner without the stock reject drops the far plane");
    tri_entry(0x3030u);
    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, UDATA, UTEXT), 1,
          "a reject mask without +z drops the far plane");

    memset(rdram, 0, sizeof(rdram));
    banner("RSP Gfx ucode F3DZEX.NoN  fifo 2.06H Yoshitaka Yasumoto 1998 Nintendo.");
    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, UDATA, UTEXT), 0,
          "F3DZEX keeps the far plane");

    memset(rdram, 0, sizeof(rdram));
    banner("RSP Gfx ucode F3DEX2      fifo 2.08  Yoshitaka Yasumoto 1999 Nintendo.");
    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, UDATA, UTEXT), 0,
          "F3DEX2 banner keeps the far plane");

    check(gsp_ucode_drops_far(rdram, RDRAM_SIZE, RDRAM_SIZE + 0x100u, UTEXT), 0,
          "an out-of-range data segment is ignored");

    if (failures)
    {
        printf("angrylion_far_reject_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("angrylion_far_reject_test: all checks passed\n");
    return 0;
}
