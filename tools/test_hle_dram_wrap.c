/* test_hle_dram_wrap -- HLE RSP accesses stay inside RDRAM and DMEM.
 *
 * RDRAM is an 8 MiB block followed directly by the cartridge ROM in the
 * core's memory map. The rsp-hle DRAM accessors used to mask addresses
 * with 0xffffff, so a pointer between 8 and 16 MiB (or a bulk transfer
 * running past the end) read or wrote whatever lay after RDRAM. They now
 * wrap inside the 8 MiB, element by element for bulk transfers that cross
 * the end; DMEM bulk transfers wrap inside its 4 KiB the same way.
 *
 * The buffers here are exactly 8 MiB and 4 KiB, so an access past them is
 * also caught by the sanitizer build.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define DRAM_BYTES 0x800000u

static struct hle_t s_hle;

static void test_element_accessors(void)
{
    *dram_u32(&s_hle, 0x10) = 0x11223344u;
    CHECK(*dram_u32(&s_hle, 0x800010) == 0x11223344u, "dram_u32 above 8 MiB wraps");
    *dram_u16(&s_hle, 0xfffffe) = 0xbeefu;
    CHECK(*dram_u16(&s_hle, 0x7ffffe) == 0xbeefu, "dram_u16 at 16 MiB - 2 lands at 8 MiB - 2");
    *dram_u8(&s_hle, 0x800003) = 0x5a;
    CHECK(*dram_u8(&s_hle, 3) == 0x5a, "dram_u8 above 8 MiB wraps");
}

static void test_bulk_dram(void)
{
    uint32_t w[4] = { 0xa0a0a0a0u, 0xb1b1b1b1u, 0xc2c2c2c2u, 0xd3d3d3d3u };
    uint32_t r[4];
    uint16_t h[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };
    uint16_t hr[4];
    uint8_t b[4] = { 1, 2, 3, 4 };
    uint8_t br[4];

    /* crossing the end of RDRAM */
    dram_store_u32(&s_hle, w, 0x7ffff8, 4);
    CHECK(*dram_u32(&s_hle, 0x7ffff8) == w[0] && *dram_u32(&s_hle, 0x7ffffc) == w[1],
          "dram_store_u32 writes the words before the end");
    CHECK(*dram_u32(&s_hle, 0) == w[2] && *dram_u32(&s_hle, 4) == w[3],
          "dram_store_u32 wraps the rest to the start of RDRAM");
    memset(r, 0, sizeof(r));
    dram_load_u32(&s_hle, r, 0x7ffff8, 4);
    CHECK(memcmp(r, w, sizeof(w)) == 0, "dram_load_u32 wraps across the end");

    /* starting past 8 MiB */
    memset(r, 0, sizeof(r));
    dram_load_u32(&s_hle, r, 0xfffff8, 4);
    CHECK(memcmp(r, w, sizeof(w)) == 0, "dram_load_u32 from 16 MiB - 8 reads RDRAM");

    dram_store_u16(&s_hle, h, 0x7ffffc, 4);
    memset(hr, 0, sizeof(hr));
    dram_load_u16(&s_hle, hr, 0x7ffffc, 4);
    CHECK(memcmp(hr, h, sizeof(h)) == 0, "dram u16 bulk wraps across the end");
    CHECK(*dram_u16(&s_hle, 2) == 0x4444, "dram_store_u16 last element at RDRAM + 2");

    dram_store_u8(&s_hle, b, 0x7ffffe, 4);
    memset(br, 0, sizeof(br));
    dram_load_u8(&s_hle, br, 0x7ffffe, 4);
    CHECK(memcmp(br, b, sizeof(b)) == 0, "dram u8 bulk wraps across the end");
    CHECK(*dram_u8(&s_hle, 1) == 4, "dram_store_u8 last element at RDRAM + 1");

    /* an in-range transfer is unchanged */
    dram_store_u32(&s_hle, w, 0x1000, 4);
    memset(r, 0, sizeof(r));
    dram_load_u32(&s_hle, r, 0x1000, 4);
    CHECK(memcmp(r, w, sizeof(w)) == 0, "in-range dram bulk transfer");
}

static void test_bulk_dmem(void)
{
    uint32_t w[2] = { 0x01020304u, 0x05060708u };
    uint32_t r[2];
    uint16_t h[3] = { 7, 8, 9 };
    uint16_t hr[3];

    dmem_store_u32(&s_hle, w, 0xffc, 2);
    CHECK(*dmem_u32(&s_hle, 0xffc) == w[0] && *dmem_u32(&s_hle, 0) == w[1],
          "dmem_store_u32 wraps past the end of DMEM");
    memset(r, 0, sizeof(r));
    dmem_load_u32(&s_hle, r, 0xffc, 2);
    CHECK(memcmp(r, w, sizeof(w)) == 0, "dmem_load_u32 wraps past the end of DMEM");

    dmem_store_u16(&s_hle, h, 0xffe, 3);
    memset(hr, 0, sizeof(hr));
    dmem_load_u16(&s_hle, hr, 0xffe, 3);
    CHECK(memcmp(hr, h, sizeof(h)) == 0, "dmem u16 bulk wraps past the end of DMEM");
}

int main(void)
{
    s_hle.dram = (unsigned char*)calloc(1, DRAM_BYTES);
    s_hle.dmem = (unsigned char*)calloc(1, 0x1000);
    if (s_hle.dram == NULL || s_hle.dmem == NULL)
        return 2;

    test_element_accessors();
    test_bulk_dram();
    test_bulk_dmem();

    free(s_hle.dram);
    free(s_hle.dmem);

    if (s_failures != 0) {
        printf("%d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_hle_dram_wrap: all checks passed\n");
    return 0;
}
