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
 * Boot-verified headless: reaches gameplay, FOV widened (meandiff 18.98 vs stock,
 * a whole-frame geometry change), HUD intact.  Author: libretroadmin */

static const struct rompatch_word widescreen_sm64_jp_words[] = {
   { 0x036748u, 0x460a3403u, 0x3c013fe3u, ROMPATCH_WF_NONE },
   { 0x03674cu, 0xe7b00034u, 0xafa10034u, ROMPATCH_WF_NONE },
};
