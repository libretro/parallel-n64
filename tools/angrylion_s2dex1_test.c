/* angrylion_s2dex1_test -- the S2DEX 1.x object commands draw what the
 * 1.x microcode draws.
 *
 * S2DEX 1.x (GBI 1, Yoshi's Story) differs from the S2DEX2 build the
 * walker was written against:
 *
 *  - it keeps a tile history in DMEM (0xd2/0xd3): every drawn object
 *    flips its render tile between 0 and 2, a texture load right after
 *    another load fences with TILESYNC, and a load after a draw puts
 *    LOADSYNC in front of the load command;
 *  - G_OBJ_SPRITE draws two textured triangles (RDP 0x0a) rather than a
 *    texture rectangle, with the render tile in bits 16..18 of the first
 *    word;
 *  - G_OBJ_RECTANGLE_R is opcode 0xb2;
 *  - G_SELECT_DL is G_RDPHALF_0 (0xe4) followed by 0xb0 and calls the
 *    list only when its status word changes.
 *
 * The walker used to draw every object on tile 0 with no fences, left
 * the sprite triangles' tile field empty and did not decode G_SELECT_DL
 * as a call, so the list below came out with the wrong tiles, no
 * LOADSYNC and the rectangles in the wrong places. The expected stream
 * follows what cxd4 produces for the same commands in Yoshi's Story.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <string.h>

#include "rdp_emit_f3dex2.h"
#include "rdp_emit_s2dex.h"

/* The walkers reference the HLE activation glue for mid-list microcode
 * swaps; this list never issues one. */
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

#define RDRAM_SIZE (256u * 1024u)
#define DL         0x1000u
#define SUBDL      0x2000u
#define TXTR_A     0x3000u
#define TXTR_B     0x3020u
#define TXTR_C     0x3040u
#define OBJ        0x3100u
#define MTX        0x3200u

static unsigned char rdram[RDRAM_SIZE];
static unsigned char fifo_storage[0x4000];
static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static void put32(unsigned int addr, unsigned int v)
{
    rdram[(addr + 0) ^ 3u] = (unsigned char)(v >> 24);
    rdram[(addr + 1) ^ 3u] = (unsigned char)(v >> 16);
    rdram[(addr + 2) ^ 3u] = (unsigned char)(v >> 8);
    rdram[(addr + 3) ^ 3u] = (unsigned char)v;
}

static void put16(unsigned int addr, unsigned int v)
{
    rdram[(addr + 0) ^ 3u] = (unsigned char)(v >> 8);
    rdram[(addr + 1) ^ 3u] = (unsigned char)v;
}

/* a TXTRBLOCK load of 64 words into TMEM 0 with its own dedup flag */
static void put_txtr(unsigned int at, unsigned int flag)
{
    put32(at + 0x00u, 0x00001033u);
    put32(at + 0x04u, 0x00010000u);
    put16(at + 0x08u, 0);
    put16(at + 0x0au, 63);
    put16(at + 0x0cu, 0x0800);
    put16(at + 0x0eu, 0);
    put32(at + 0x10u, flag);
    put32(at + 0x14u, 0xffffffffu);
}

static unsigned int dl_at;
static void cmd(unsigned int w0, unsigned int w1)
{
    put32(dl_at, w0);
    put32(dl_at + 4u, w1);
    dl_at += 8u;
}

int main(void)
{
    static GSPState gsp;
    RdpFifo fifo;
    unsigned int i;
    char seq[64];
    unsigned int n = 0;
    unsigned int rect_tiles[4], tri_tiles[4];
    unsigned int nrect = 0, ntri = 0;
    const char *want = "BTBRRLBRtt";

    /* a 32x32 RGBA16 object at (16,16), unit scale */
    put16(OBJ + 0x00u, 16u << 2);       /* objX s10.2 */
    put16(OBJ + 0x02u, 1u << 10);       /* scaleW u5.10 */
    put16(OBJ + 0x04u, 32u << 5);       /* imageW u10.5 */
    put16(OBJ + 0x06u, 0);
    put16(OBJ + 0x08u, 16u << 2);       /* objY */
    put16(OBJ + 0x0au, 1u << 10);
    put16(OBJ + 0x0cu, 32u << 5);
    put16(OBJ + 0x0eu, 0);
    put16(OBJ + 0x10u, 32u * 2u / 8u);  /* imageStride, 64-bit words */
    put16(OBJ + 0x12u, 0);              /* imageAdrs */
    rdram[(OBJ + 0x14u) ^ 3u] = 0;      /* RGBA */
    rdram[(OBJ + 0x15u) ^ 3u] = 2;      /* 16b */
    rdram[(OBJ + 0x16u) ^ 3u] = 0;
    rdram[(OBJ + 0x17u) ^ 3u] = 0;

    /* identity object matrix, unit base scale */
    put32(MTX + 0x00u, 0x00010000u);
    put32(MTX + 0x04u, 0);
    put32(MTX + 0x08u, 0);
    put32(MTX + 0x0cu, 0x00010000u);
    put16(MTX + 0x10u, 0);
    put16(MTX + 0x12u, 0);
    put16(MTX + 0x14u, 0x0400u);
    put16(MTX + 0x16u, 0x0400u);

    put_txtr(TXTR_A, 1);
    put_txtr(TXTR_B, 2);
    put_txtr(TXTR_C, 3);

    /* the selected list draws one rectangle */
    dl_at = SUBDL;
    cmd(0x03000000u, OBJ);              /* G_OBJ_RECTANGLE */
    cmd(0xb8000000u, 0);

    dl_at = DL;
    cmd(0x05000000u | (23u << 16), MTX);    /* G_OBJ_MOVEMEM, full matrix */
    cmd(0xc1000000u, TXTR_A);           /* load: first after boot, no fence */
    cmd(0xc1000000u, TXTR_B);           /* load after load: TILESYNC */
    cmd(0x03000000u, OBJ);              /* rect: tile 0 */
    /* G_SELECT_DL sid 0, flag 1: status 0 -> 1, calls SUBDL (tile 2) */
    cmd(0xe4000000u | (0u << 16) | (SUBDL & 0xffffu), 1);
    cmd(0xb0000000u | (SUBDL >> 16), 0xffffffffu);
    /* the same select again: status already 1, not called */
    cmd(0xe4000000u | (0u << 16) | (SUBDL & 0xffffu), 1);
    cmd(0xb0000000u | (SUBDL >> 16), 0xffffffffu);
    cmd(0xc1000000u, TXTR_C);           /* load after a draw: LOADSYNC */
    cmd(0xb2000000u, OBJ);              /* G_OBJ_RECTANGLE_R: tile 0 */
    cmd(0x04000000u, OBJ);              /* G_OBJ_SPRITE: tile 2, triangles */
    cmd(0xb8000000u, 0);

    f3dex2_set_rdram(rdram);
    f3dex2_set_rdram_size(RDRAM_SIZE);
    f3dex2_seg_reset();
    f3dex2_force_class_s2dex1();
    s2dex_set_version2(0);
    s2dex_reset();
    rdp_fifo_init(&fifo, fifo_storage, 0x100000u, sizeof(fifo_storage));
    rdp_fifo_fullsync_reset();
    memset(&gsp, 0, sizeof(gsp));

    f3dex2_run_dl(&gsp, &fifo, DL, 1, 0);

    /* Reduce the stream to the commands under test:
     *   T tilesync, L loadsync, B load block, R texture rectangle,
     *   t textured triangle. */
    for (i = 0; i + 8u <= fifo.used && n + 1u < sizeof(seq); )
    {
        unsigned int w0, w1, op;
        memcpy(&w0, fifo_storage + i, 4);
        memcpy(&w1, fifo_storage + i + 4, 4);
        op = (w0 >> 24) & 0x3fu;
        if (op == 0x28u)
            seq[n++] = 'T';
        else if (op == 0x26u)
            seq[n++] = 'L';
        else if (op == 0x33u)
            seq[n++] = 'B';
        else if (op == 0x24u || op == 0x25u)
        {
            seq[n++] = 'R';
            if (nrect < 4u)
                rect_tiles[nrect++] = (w1 >> 24) & 7u;
        }
        else if (op >= 0x0au && op <= 0x0fu && (op & 2u))
        {
            seq[n++] = 't';
            if (ntri < 4u)
                tri_tiles[ntri++] = (w0 >> 16) & 7u;
        }
        if (op == 0x24u || op == 0x25u)
            i += 16u;
        else if (op >= 0x08u && op <= 0x0fu)
            i += 8u * (4u + ((op & 4u) ? 8u : 0u) + ((op & 2u) ? 8u : 0u)
                      + ((op & 1u) ? 2u : 0u));
        else
            i += 8u;
    }
    seq[n] = 0;

    if (strcmp(seq, want) != 0)
    {
        printf("FAIL: command sequence %s, expected %s\n", seq, want);
        s_failures++;
    }
    CHECK(nrect >= 3u && rect_tiles[0] == 0 && rect_tiles[1] == 2
          && rect_tiles[2] == 0,
          "rectangles do not alternate render tiles 0, 2, 0");
    CHECK(ntri == 2u && tri_tiles[0] == 2 && tri_tiles[1] == 2,
          "sprite triangles do not carry render tile 2 in bits 16..18");

    if (s_failures)
        return 1;
    printf("angrylion_s2dex1_test: all checks passed\n");
    return 0;
}
