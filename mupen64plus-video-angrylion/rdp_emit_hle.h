/* rdp_emit_hle.h -- activation glue for the angrylion HLE-graphics path.
 *
 * Ties the F3DEX2 dispatcher + RDP FIFO to the angrylion rasterizer. This is
 * called from angrylionProcessDList(), which the core only invokes when the
 * RSP plugin is HLE (cxd4/parallel are low-level and feed the RDP directly
 * via ProcessRDPList instead). So no extra gating is required: if this runs,
 * the RSP is HLE and a display list is waiting. ISO C89 / MSVC-compatible.
 */
#ifndef RDP_EMIT_HLE_H
#define RDP_EMIT_HLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read the OSTask display-list pointer from DMEM, walk it through the HLE
 * geometry path, and rasterize the emitted RDP command FIFO. */
void rdp_emit_hle_process_dlist(void);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_HLE_H */
