/* AUTO-GENERATED — do not edit by hand.
 * Super Mario 64 (Europe, PAL, sha1 4ac57216...) 16:9 widescreen table.
 *
 * Hunk 1 (aspect): the PAL build computes the camera aspect as (w/h) * 1.1f;
 * replacing the 1.1f multiplier (0x3f8ccccd) with 1.33333f (0x3faaaaab) yields
 * (4/3) * 1.33333 = 16/9.
 *
 * Hunk 2 (frustum culling): obj_is_in_view computes the horizontal cull edge
 * from the camera fov without accounting for aspect, so at 16:9 objects near
 * the left/right edges are culled as if the screen were still 4:3 and pop in
 * and out.  The half-fov is built as (fov/2 + 1) * 32768 / 180; widening that
 * angle by changing the /180 divisor (0x4334) to /135 (0x4307) scales the cull
 * edge by 180/135 = 1.3333, matching the wider view.  This errs slightly toward
 * over-rendering (a few % wider than exact), which is fail-safe: objects appear
 * just past the visible edge rather than popping out early.
 *
 * Boot-verified headless (16:9, HUD intact). Author: libretroadmin */

static const struct rompatch_word widescreen_sm64_eu_words[] = {
   { 0x0c4098u, 0x3f8ccccdu, 0x3faaaaabu, ROMPATCH_WF_NONE }, /* aspect 1.1f -> 1.3333f */
   { 0x02cc60u, 0x3c014334u, 0x3c014307u, ROMPATCH_WF_NONE }, /* cull fov /180 -> /135 */
};
