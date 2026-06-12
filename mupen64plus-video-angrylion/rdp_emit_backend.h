/* rdp_emit_backend.h -- RDP backend abstraction for the HLE command emitter.
 *
 * The HLE emitter (rdp_emit_hle.c and the frontend/bridge/encoder it drives)
 * is RDP-backend-agnostic: it walks an F3DEX2/S2DEX display list and produces
 * a standard RDP command FIFO.  Only three things differ between rasterizer
 * backends -- how to reach guest RDRAM/DMEM, and how to hand a finished
 * command FIFO to the RDP -- so they are abstracted here.
 *
 * angrylion registers its plugin_get_ and n64video_ implementations; parallel
 * registers its gfx_info accessors plus a submit that feeds parallel-rdp
 * CommandProcessor.  The display-list walk, microcode detection and command
 * encoding are shared verbatim.
 *
 * Language: ISO C89 / MSVC-compatible.  Usable from both C and C++.
 */
#ifndef RDP_EMIT_BACKEND_H
#define RDP_EMIT_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

/* One rasterizer backend's hooks.  All pointers must be non-NULL when the
 * backend is installed.  Memory accessors return the same host pointers the
 * backend's own command path uses; the emitter only reads through them. */
typedef struct RdpEmitBackend
{
    /* Base of guest RDRAM (host pointer), its size in bytes, and base of
     * guest DMEM (host pointer).  Read-only from the emitter's view. */
    unsigned char *(*get_rdram)(void);
    unsigned int   (*get_rdram_size)(void);
    unsigned char *(*get_dmem)(void);

    /* Rasterize a finished RDP command FIFO.  `storage` is the host buffer
     * holding `len_bytes` of RDP command words; `base_byte_addr` is the
     * virtual RDRAM byte address the FIFO is reported to occupy (the DPC
     * registers are pointed at [base, base+len)).  The backend must fetch
     * the command words from `storage` -- not from guest RDRAM at `base`,
     * which is never written -- run its RDP over the list, and on return
     * leave its command-fetch reading guest RDRAM again.  Called once per
     * drained batch (mid-frame overflow flush and the frame-final batch). */
    void           (*submit)(const unsigned char *storage,
                             unsigned int base_byte_addr,
                             unsigned int len_bytes);
} RdpEmitBackend;

/* Install the active backend.  The pointer must outlive all subsequent
 * rdp_emit_hle_process_dlist() calls (a file-scope static is fine).  Passing
 * NULL is a no-op.  Not thread-safe: set it once at plugin connect time, or
 * before each ProcessDList on a single graphics thread. */
void rdp_emit_set_backend(const RdpEmitBackend *be);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_BACKEND_H */
