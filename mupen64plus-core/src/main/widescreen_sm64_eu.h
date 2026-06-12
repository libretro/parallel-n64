/* AUTO-GENERATED — do not edit by hand.
 * Super Mario 64 (Europe, PAL, sha1 4ac57216...) 16:9 widescreen table.
 * The PAL build computes the camera aspect as (w/h) * 1.1f; replacing the
 * 1.1f multiplier (0x3f8ccccd) with 1.33333f (0x3faaaaab) yields
 * (4/3) * 1.33333 = 16/9. Single hunk, boot-verified headless (reaches
 * gameplay, renders 16:9, HUD intact). Author: libretroadmin */

static const struct rompatch_word widescreen_sm64_eu_words[] = {
   { 0x0c4098u, 0x3f8ccccdu, 0x3faaaaabu, ROMPATCH_WF_NONE },
};
