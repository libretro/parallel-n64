/* AUTO-GENERATED — do not edit by hand.
 * Super Mario 64 (Japan, NTSC-J, sha1 8a20a5c8...) 16:9 widescreen table.
 * The JP build computes the camera aspect as width/height directly (no
 * multiplier constant to retarget, unlike the PAL build).  geo_process_perspective
 * holds the aspect at 0x036748/0x03674c:
 *     0x036748:  div.s $f16, $f6, $f10   ; aspect = width / height
 *     0x03674c:  swc1  $f16, 0x34($sp)   ; store aspect (guPerspectiveF arg)
 * Overwrite those two slots in place to store a hardcoded 16:9 aspect float,
 * with no added code (no relocation):
 *     0x036748:  lui $at, 0x3fe3         ; $at = 0x3fe30000 = 1.7734 (~16/9)
 *     0x03674c:  sw  $at, 0x34($sp)      ; store the float bits to the aspect slot
 *
 * Hunk 3 (frustum culling): obj_is_in_view computes the horizontal cull edge
 * from the camera fov without accounting for aspect, so at 16:9 objects near the
 * left/right edges pop in and out.  The half-fov is built as
 * (fov/2 + 1) * 32768 / 180; changing the /180 divisor (0x4334) to /135 (0x4307)
 * at 0x037fb4 scales the cull edge by 180/135 = 1.3333 to match.  Errs slightly
 * toward over-rendering, which is fail-safe (objects appear just past the edge
 * rather than popping out early).
 *
 * Boot-verified headless on the retail JP ROM (16:9, HUD intact).
 * Author: libretroadmin */

static const struct rompatch_word widescreen_sm64_jp_words[] = {
   { 0x036748u, 0x460a3403u, 0x3c013fe3u, ROMPATCH_WF_NONE }, /* aspect: lui $at,0x3fe3 */
   { 0x03674cu, 0xe7b00034u, 0xafa10034u, ROMPATCH_WF_NONE }, /* aspect: sw $at,0x34($sp) */
   { 0x037fb4u, 0x3c014334u, 0x3c014307u, ROMPATCH_WF_NONE }, /* cull fov /180 -> /135 */
};
