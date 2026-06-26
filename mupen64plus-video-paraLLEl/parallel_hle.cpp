/* parallel_hle.cpp -- HLE-emitter graphics path for parallel-rdp.
 *
 * When the RSP plugin is HLE, the core routes display lists to the graphics
 * plugin's ProcessDList.  parallel-rdp is a low-level RDP and has no geometry
 * front-end of its own, so historically parallelProcessDList() did nothing and
 * RSP-HLE + parallel rendered a blank screen.
 *
 * This wires parallel-rdp to the same backend-agnostic HLE command emitter the
 * angrylion plugin uses (rdp_emit_*.c): it walks the F3DEX2/S2DEX display list
 * and synthesizes a standard RDP command FIFO in a host buffer.  We register a
 * parallel-rdp backend whose submit hands that FIFO to RDP::process_commands()
 * via the host-buffer redirect (RDP::set_hle_cmd_buffer), so the synthesized
 * commands take the exact same path real-game RDP commands do -- no guest RDRAM
 * is written, so Expansion Pak titles (Majora's Mask) are unaffected.
 *
 * The emitter sources are shared C (ISO C89); this glue is the only
 * parallel-specific piece, mirroring angrylion's al_hle_backend in interface.c.
 */

#include "rdp.hpp"
#include "Gfx #1.3.h"
#include "z64.h"

extern "C" {
#include "../mupen64plus-video-angrylion/rdp_emit_hle.h"
#include "../mupen64plus-video-angrylion/rdp_emit_backend.h"
}

/* parallel reports the same fixed 8 MiB RDRAM size the rest of the core and the
 * angrylion backend assume; the HLE FIFO is parked in the top 256 KiB of that
 * virtual range (never dereferenced into guest memory). */
#define PARALLEL_RDRAM_SIZE 0x800000u

static unsigned char *par_get_rdram(void)
{
    return (unsigned char *)GET_GFX_INFO(RDRAM);
}

static unsigned int par_get_rdram_size(void)
{
    return PARALLEL_RDRAM_SIZE;
}

static unsigned char *par_get_dmem(void)
{
    return (unsigned char *)GET_GFX_INFO(DMEM);
}

/* Rasterize a finished RDP command FIFO through parallel-rdp.  Point the DPC
 * registers at the FIFO's reported virtual range, activate the host-buffer
 * redirect so process_commands() fetches the command words from `storage`
 * (guest RDRAM at `base` is never written), run the list, then clear the
 * redirect so subsequent (LLE) command processing reads guest RDRAM again. */
static void par_hle_submit(const unsigned char *storage,
                           unsigned int base_byte_addr, unsigned int len_bytes)
{
    *GET_GFX_INFO(DPC_START_REG)   = base_byte_addr;
    *GET_GFX_INFO(DPC_CURRENT_REG) = base_byte_addr;
    *GET_GFX_INFO(DPC_END_REG)     = base_byte_addr + len_bytes;

    RDP::set_hle_cmd_buffer(storage, base_byte_addr, len_bytes);
    RDP::process_commands();
    RDP::set_hle_cmd_buffer(nullptr, 0, 0);
}

static const RdpEmitBackend par_hle_backend =
{
    par_get_rdram,
    par_get_rdram_size,
    par_get_dmem,
    par_hle_submit,
    nullptr     /* set_s2dex_texsync: parallel-rdp orders its own work */
};

/* Called from parallelProcessDList() (parallel.cpp). */
void parallel_hle_process_dlist(void)
{
    rdp_emit_set_backend(&par_hle_backend);
    rdp_emit_hle_process_dlist();
}
