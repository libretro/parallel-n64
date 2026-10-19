/* angrylion_rs_fullsync_test -- a Rogue Squadron display list's own
 * SYNC_FULL reaches the RDP once.
 *
 * The walker notes the list's full sync and leaves the task's single
 * terminating SYNC_FULL to the dispatcher, which appends it when the list
 * completes; forwarding the list's own as well would hand the RDP two.
 * The list here is one page: a pipe sync, a full sync and the end
 * command.  The FIFO must hold the pipe sync only, with the full sync
 * recorded as seen.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <string.h>

#include "rdp_emit_f3dex2.h"
#include "rdp_emit_rs.h"

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

void rs_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr);
void rdp_fifo_fullsync_reset(void);
int rdp_fifo_fullsync_seen(void);

#define RDRAM_SIZE (64u * 1024u)
#define PAGE       0x1000u

static unsigned char rdram[RDRAM_SIZE];
static unsigned char fifo_storage[0x1000];

static void put32(unsigned int addr, unsigned int v)
{
    rdram[(addr + 0) ^ 3u] = (unsigned char)(v >> 24);
    rdram[(addr + 1) ^ 3u] = (unsigned char)(v >> 16);
    rdram[(addr + 2) ^ 3u] = (unsigned char)(v >> 8);
    rdram[(addr + 3) ^ 3u] = (unsigned char)v;
}

int main(void)
{
    static GSPState gsp;
    RdpFifo fifo;
    unsigned int i, syncs = 0, pipe = 0;
    int ok;

    put32(PAGE + 0x00u, 0);                 /* no next page */
    put32(PAGE + 0x08u, 0xe7000000u);       /* pipe sync */
    put32(PAGE + 0x0cu, 0);
    put32(PAGE + 0x10u, 0xe9000000u);       /* full sync */
    put32(PAGE + 0x14u, 0);
    put32(PAGE + 0x18u, 0xb8000000u);       /* end */
    put32(PAGE + 0x1cu, 0);

    rs_set_rdram(rdram);
    rs_set_rdram_size(RDRAM_SIZE);
    f3dex2_set_rdram(rdram);
    f3dex2_set_rdram_size(RDRAM_SIZE);
    rdp_fifo_init(&fifo, fifo_storage, 0x100000u, sizeof(fifo_storage));
    rdp_fifo_fullsync_reset();
    memset(&gsp, 0, sizeof(gsp));

    rs_run_dl(&gsp, &fifo, PAGE);

    for (i = 0; i + 8u <= fifo.used; i += 8u) {
        unsigned int w0;
        memcpy(&w0, fifo_storage + i, 4);
        if (((w0 >> 24) & 0x3fu) == 0x29u)
            syncs++;
        if (((w0 >> 24) & 0x3fu) == 0x27u)
            pipe++;
    }
    ok = syncs == 0 && pipe == 1 && rdp_fifo_fullsync_seen();
    if (!ok) {
        printf("FAIL: walker forwarded %u full syncs and %u pipe syncs, full sync %s\n",
               syncs, pipe, rdp_fifo_fullsync_seen() ? "seen" : "not seen");
        return 1;
    }
    printf("angrylion_rs_fullsync_test: all checks passed\n");
    return 0;
}
