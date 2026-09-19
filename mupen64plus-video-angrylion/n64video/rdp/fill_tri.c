/* FILL-mode triangle write model.
 *
 * Hardware does not fill a triangle span as one contiguous run: in FILL
 * mode the span is written in 64-bit words with byte enables, and for
 * triangles the fill unit splits rows into bursts, trims end words and
 * blanks rows according to state carried from scanline to scanline.
 *
 * The state machines below (fill_burst_*, family A: vertical minor edges
 * on the right, moving edge on the left; fill_famb_*, family B: fixed
 * left edge, moving right edge) are taken from cen64
 * (https://gitlab.com/jgemu/cen64, src/rdp/rdp_core.c), where they were
 * reverse-engineered from the snapper64 "RDP Fill Mode Tri" hardware
 * reference captures. They are reproduced under cen64's licence:
 *
 * Copyright (c) 2015, Tyler J. Stachecki
 * Copyright (c) 2025-2026, Rupert Carmichael
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Only the container the plans are written into differs: cen64 stores a
 * plan in its per-span aux record, here it is struct fill_tri_plan in the
 * span itself.
 */

#include <limits.h>

static STRICTINLINE int32_t rdp_min32(int32_t a, int32_t b) { return a < b ? a : b; }
static STRICTINLINE int32_t rdp_max32(int32_t a, int32_t b) { return a > b ? a : b; }

typedef struct fill_burst_state
{
    int      active;
    int      armed;
    int64_t  R;              /* subpixel resume pointer, px units, 16.16 */
    int64_t  climb_pending;  /* previous row's clipped-edge advance */
    int      T;              /* staircase phase: 2, -28, -12 */
    int64_t  xl_prev, xl_prev2, xl_prev3;   /* clipped left edge history */
    int64_t  xlr_prev, xlr_prev2;           /* raw (wrapped) left edge history */
    int      terminal;       /* -1 = not terminal; else rows since entry */
    int      rearm_pending;
    int64_t  park;
    int64_t  tfrag_ref;
    int      tfrag_gate;
    int      tfrag_first;
    int32_t  prev_px0;       /* previous span's start pixel */
    int32_t  prev_px0_2;     /* span start two rows back */
    int64_t  prev_S;         /* previous burst row's resume word */
    int32_t  prev_W1;
    int64_t  term_pdR;       /* previous terminal row's pre-drop dR */
    int      term_pRodd;     /* previous terminal row's pre-drop parity */
    int      postterm;       /* row after a terminal re-arm */
    int      rel2;           /* T==2 clip-release signature latched */
    int      rel2lo;         /* ... with the low-edge variant */
    int      just_armed;     /* the arm happened on the previous row */
    int      t2cross;        /* T==2-transition entry latch (blank gate) */
} fill_burst_state;

#define FILL_BURST_NONE64  INT64_MIN
#define FILL_BURST_NONE32  INT32_MIN

/* k=0 close suppression thresholds (raw sub3-sub0 spread, 16.16):
 * KZ_REARM 2.500 px, bracket [160782, 164694];
 * KZ_TERM ~4.92 px, bracket [321732, 323532], last row, term <= 2. */
#define FILL_BURST_KZ_REARM  163840
#define FILL_BURST_KZ_TERM   322560

static uint8_t fill_burst_open_mask(int32_t px0)
{
    return (px0 & 1) ? 0xf8u : 0xffu;
}

static uint8_t fill_burst_close_mask(int32_t px1)
{
    return (px1 & 1) ? 0x01u : 0x1fu;
}

/* The empty plan, and the initializer every other plan builds on: the
 * aux pool is never cleared, so every field the executor can reach is
 * written here rather than left as residue from an earlier span. Both
 * runs are empty (lo > hi), so the trim bytes are unreachable on a
 * blank plan and their values are arbitrary; writing them keeps the
 * record self-describing. */
static void fill_burst_plan_blank(struct fill_tri_plan *ud)
{
    ud->m_fill_plan = 1;
    ud->m_fill_b1lo = 1;
    ud->m_fill_b1hi = 0;
    ud->m_fill_open  = 0xffu;
    ud->m_fill_close = 0xffu;
    ud->m_fill_pw   = -1;
    ud->m_fill_pm   = 0;
    ud->m_fill_t2lo = 1;
    ud->m_fill_t2hi = 0;
    ud->m_fill_t2close = 0xffu;
}

static void fill_burst_plan_run1(struct fill_tri_plan *ud, int32_t lo, int32_t hi,
                                 uint8_t open_be, uint8_t close_be)
{
    fill_burst_plan_blank(ud);
    ud->m_fill_b1lo  = (int16_t)lo;
    ud->m_fill_b1hi  = (int16_t)hi;
    ud->m_fill_open  = open_be;
    ud->m_fill_close = close_be;
}

/* The pre-arm span pattern: a single burst [W0 .. W1] with parity trims at
 * both ends (a one-word span intersects them). */
static void fill_burst_plan_normal(struct fill_tri_plan *ud, int32_t W0, int32_t W1,
                                   int32_t px0, int32_t px1)
{
    if (W1 < W0)
    {
        fill_burst_plan_blank(ud);
        return;
    }
    fill_burst_plan_run1(ud, W0, W1, fill_burst_open_mask(px0),
                         fill_burst_close_mask(px1));
}

/* FILL-mode triangle burst model, family B: fixed left edge, moving right
 * (h/m) edge (dxldy == 0, dxhdy/dxmdy != 0, lft == 0), adjudicated against
 * the snapper64 Fill Mode Tri Sweep hardware captures (96960/96960 rows
 * exact over 1024 traces). The mechanism engages when the raw right edge
 * runs off the scissor:
 *  - From the first row where any valid subline's raw right edge reaches
 *    the scissor column, the rendered span end is min(raw, clipx2)
 *    (inclusive of the scissor column itself, one past the normal clamp).
 *  - The machine arms when the last subline's integer X passes the
 *    scissor column (r3i >= clipx2 + 1). A resume pointer R rides a
 *    reference line, line = 2*clipx2 - 32 - even(r3i of the previous
 *    row), starting D = 48 px above it and dropping 16 px per row
 *    (D = max(D - 16, 0); R = line + D).
 *  - Burst rows: first burst [W0 .. E] with the opening trim suppressed,
 *    close trim from the scissor column parity at E = W1c - 8 *
 *    ceil((W1c - (S - 2)) / 8) for S = R >> 1; a parity partial at S and
 *    a full tail to the clamped raw end. Writes clamp at the row's last
 *    framebuffer word; nothing wraps.
 *  - Adjacency: once D <= 16 and r3i >= clipx2 + 8, rows render the
 *    first burst immediately followed by the tail (no gap, no partial),
 *    with R parked 16 px above the line and E anchored at S - 1.
 *  - The machine ends at r3i >= clipx2 + 24 (r3i >= clipx2 + 40 when
 *    already past at arming; single-sided bracket [40, 44] from the
 *    captures), on the final row of the window, and on rows whose last
 *    subline is invalid: those rows render the full row.
 *  - If the arming row is the final row, the machine's first burst row
 *    flushes into the same row's write stream (visible as the tail
 *    partial OR'd over the close trim when S lands on the last word). */
typedef struct fill_famb_state
{
    int      active;
    int      phase;          /* 0 pre-arm, 1 machine, 2 park, 3 done */
    int32_t  D;              /* px above the reference line */
    int32_t  prev_r3i;       /* previous row's last-subline integer X */
    int32_t  clip;           /* scissor right column (m_xl_raw >> 2) */
    int32_t  w1c;            /* last plan word: min(clip >> 1, row words - 1) */
    int32_t  lastj;          /* final row of the render window */
    int32_t  w1row;          /* last word of the framebuffer row */
} fill_famb_state;

static void fill_famb_plan(struct fill_tri_plan *ud, int32_t b1lo, int32_t b1hi,
                           uint8_t open_be, uint8_t close_be,
                           int32_t pw, uint8_t pm, int32_t t2lo, int32_t t2hi)
{
    ud->m_fill_plan  = 1;
    ud->m_fill_b1lo  = (int16_t)b1lo;
    ud->m_fill_b1hi  = (int16_t)b1hi;
    ud->m_fill_open  = open_be;
    ud->m_fill_close = close_be;
    ud->m_fill_pw    = (int16_t)pw;
    ud->m_fill_pm    = pm;
    ud->m_fill_t2lo  = (int16_t)t2lo;
    ud->m_fill_t2hi  = (int16_t)t2hi;
    /* The aux pool is never cleared and the executor consults the
     * family-A tail close byte on every plan: default it off. */
    ud->m_fill_t2close = 0xffu;
}

/* Anchored single-burst span [px0 .. px1]: nwr = ceil(npx / 2) words ending
 * at the word of the last pixel byte (the committed fill write law). */
static void fill_famb_plan_normal(struct fill_tri_plan *ud, int32_t px0, int32_t px1,
                                  int32_t w1row, int noopen)
{
    const int32_t npx = px1 - px0 + 1;
    if (npx <= 0)
    {
        fill_burst_plan_blank(ud);
        return;
    }
    int32_t wlast  = px1 >> 1;
    int32_t wfirst = wlast - ((npx + 1) / 2 - 1);
    uint8_t open_be  = noopen ? 0xffu : fill_burst_open_mask(px0);
    uint8_t close_be = fill_burst_close_mask(px1);
    /* Writes clamp at the row bounds; a trim word that falls outside the
     * row is dropped, not migrated to the boundary word. */
    if (wlast > w1row)
    {
        wlast = w1row;
        close_be = 0xffu;
    }
    if (wfirst < 0)
    {
        wfirst = 0;
        open_be = 0xffu;
    }
    fill_famb_plan(ud, wfirst, wlast, open_be, close_be, -1, 0u, 1, 0);
}

/* Block snap, shared by both fill-triangle families: the last word at
 * or below w1c that sits a whole number of 8-word blocks below the
 * resume word S, taken from S - anchor. The burst's first run ends
 * here; anchor selects how far under the resume word the snap starts
 * (2 for a gapped burst, 1 for the adjacent-tail regime). */
static int64_t fill_burst_e_snap(int64_t S, int64_t anchor, int64_t w1c)
{
    if (S - anchor >= w1c)
        return w1c;
    const int64_t dd = w1c - (S - anchor);
    return w1c - 8 * ((dd + 7) / 8);
}

static void fill_famb_row(fill_famb_state *st, int anyvalid,
                          int32_t px0, int32_t xr_raw_max, int32_t xr_raw3,
                          int r3_valid, int32_t j, struct fill_tri_plan *ud)
{
    if (!anyvalid)
    {
        /* Invalid scanline: nothing rendered, machine state frozen. */
        fill_burst_plan_blank(ud);
        return;
    }

    const int32_t clip = st->clip;
    const int32_t w1c = st->w1c;
    const int32_t W0 = px0 >> 1;
    const int32_t b1lo0 = W0 < 0 ? 0 : W0;
    const uint8_t cmask = fill_burst_close_mask(clip);
    const int32_t r3 = r3_valid ? xr_raw3 : xr_raw_max;
    const int32_t r3i = r3 >> 16;

    if (st->phase == 0)
    {
        const int32_t rmi = xr_raw_max >> 16;
        if (r3i >= clip + 1)
        {
            st->phase = 1;
            st->D = 48;
            if (r3i >= clip + 40)
            {
                /* Already far past the scissor at arming: straight to the
                 * terminal full-row regime. */
                st->phase = 3;
                fill_famb_plan(ud, b1lo0, w1c, 0xffu, 0xffu, -1, 0u, 1, 0);
            }
            else if (j == st->lastj)
            {
                /* Final-row arming: the machine's first burst row flushes
                 * into this row's write stream. Its close trim and gap all
                 * land inside the full run; only a tail partial on the
                 * last word stays visible. */
                const int32_t line0 = 2 * clip - 32 - (r3i & ~1);
                const int32_t R0 = line0 + 32;
                const int32_t S0 = R0 >> 1;
                const int32_t px1 = rmi < clip ? rmi : clip;
                const int32_t npx = px1 - px0 + 1;
                if (npx <= 0)
                {
                    fill_burst_plan_blank(ud);
                }
                else
                {
                    int32_t wlast = px1 >> 1;
                    int32_t wfirst = wlast - ((npx + 1) / 2 - 1);
                    if (wfirst > b1lo0) wfirst = b1lo0;
                    if (wfirst < 0) wfirst = 0;
                    uint8_t close_be = fill_burst_close_mask(px1);
                    if (wlast > st->w1row)
                    {
                        wlast = st->w1row;
                        close_be = 0xffu;
                    }
                    if (S0 < w1c)
                        close_be = 0xffu;
                    else if (S0 == w1c)
                        close_be |= (R0 & 1) ? 0x80u : 0xf8u;
                    fill_famb_plan(ud, wfirst, wlast, 0xffu, close_be,
                                   -1, 0u, 1, 0);
                }
            }
            else
            {
                fill_famb_plan_normal(ud, px0, rmi < clip ? rmi : clip,
                                      st->w1row, 1);
            }
        }
        else if (rmi >= clip)
        {
            /* Scissor-touch quirk: the rendered end is the scissor column
             * itself, one past the normal exclusive clamp. */
            fill_famb_plan_normal(ud, px0, clip, st->w1row, 0);
        }
        /* else: default single-burst path (plan 0), identical output. */
        st->prev_r3i = r3i;
        return;
    }

    if (!r3_valid || j == st->lastj)
    {
        /* Partial or final row: the full row renders; the machine state
         * itself does not advance to the terminal regime. */
        fill_famb_plan(ud, b1lo0, w1c, 0xffu, 0xffu, -1, 0u, 1, 0);
        st->prev_r3i = r3i;
        return;
    }

    if (st->phase == 3 || r3i >= clip + 24)
    {
        st->phase = 3;
        fill_famb_plan(ud, b1lo0, w1c, 0xffu, 0xffu, -1, 0u, 1, 0);
        st->prev_r3i = r3i;
        return;
    }

    const int32_t line = 2 * clip - 32 - (st->prev_r3i & ~1);

    if (st->phase == 2)
    {
        const int32_t R = line + 16;
        const int32_t S = R >> 1;
        const int32_t E = (int32_t)fill_burst_e_snap(S, 1, w1c);
        fill_famb_plan(ud, b1lo0, E < b1lo0 ? b1lo0 - 1 : E, 0xffu, cmask,
                       -1, 0u,
                       (E + 1) > b1lo0 ? (E + 1) : b1lo0, w1c);
        st->prev_r3i = r3i;
        return;
    }

    /* Phase 1: staircase onto the line. */
    st->D = st->D > 16 ? st->D - 16 : 0;
    const int32_t R = line + st->D;
    const int32_t S = R >> 1;
    const int32_t E = (int32_t)fill_burst_e_snap(S, 2, w1c);

    if (st->D <= 16 && r3i >= clip + 8)
    {
        st->phase = 2;
        fill_famb_plan(ud, b1lo0, E < b1lo0 ? b1lo0 - 1 : E, 0xffu, cmask,
                       -1, 0u,
                       (E + 1) > b1lo0 ? (E + 1) : b1lo0, w1c);
    }
    else if (S > w1c)
    {
        fill_famb_plan_normal(ud, px0, clip, st->w1row, 1);
    }
    else
    {
        const int32_t rmw = xr_raw_max >> 17;
        const int32_t w1m = rmw < w1c ? rmw : w1c;
        int32_t pw = -1;
        uint8_t pm = 0u;
        int32_t t2lo = S;
        if (S <= w1m && S >= W0 && S >= 0)
        {
            pw = S;
            pm = (R & 1) ? 0x80u : 0xf8u;
            t2lo = S + 1;
        }
        if (t2lo < b1lo0) t2lo = b1lo0;
        fill_famb_plan(ud, b1lo0, E < b1lo0 ? b1lo0 - 1 : E, 0xffu, cmask,
                       pw, pm, t2lo, S <= w1m ? w1m : t2lo - 1);
    }
    st->prev_r3i = r3i;
}

/* Python-semantics modulo (non-negative result) for the last-row block
 * drop lattice test. */
static int64_t fill_burst_mod16(int64_t x)
{
    int64_t m = x % 16;
    return (m < 0) ? m + 16 : m;
}

/* Drop the resume pointer by 16 px while it sits at or above limit_px,
 * at most maxdrops times (maxdrops < 0 = unlimited). Every drop loop in
 * the machine is this comparator with a different (limit, count) pair. */
static void fill_burst_drop_while(fill_burst_state *st, int64_t limit_px,
                                  int64_t maxdrops)
{
    while (maxdrops != 0 && (st->R >> 16) >= limit_px)
    {
        st->R -= (int64_t)16 << 16;
        maxdrops--;
    }
}

/* Seed (or re-seed) the burst pointer and staircase at an arm point:
 * R = 2*W1 + 19 px above the row base (+ the clipped edge's fraction on
 * a first arm; a terminal re-arm seeds at the integer), lagged climb
 * cleared, edge history rotated, T = 2. */
static void fill_burst_seed(fill_burst_state *st, int32_t W1,
                            int64_t xl_now, int64_t xlr_now, int with_frac)
{
    st->R = ((int64_t)(2 * W1 + 19) << 16) +
            (with_frac ? (xl_now & 0xffff) : 0);
    st->climb_pending = 0;
    st->xl_prev2 = st->xl_prev;
    st->xl_prev = xl_now;
    st->xlr_prev2 = st->xlr_prev;
    st->xlr_prev = xlr_now;
    st->T = 2;
}

/* Terminal FIFO flush comparator: a partial queued while the live R sat
 * in the [2*W1-16, 2*W1-15] launch band flushes into the current row's
 * stream when R vaults to >= 2*W1-13, landing at W1 with the LAUNCH
 * row's Rpx parity (pdR=-15 -> 0x80, pdR=-16 -> 0xf8). 121/121 with
 * 0/204 at pdR=-14 and 0/29 at pdR<=-17 in refs. */
static int fill_burst_fifo_flush(const fill_burst_state *st, int64_t dR_now)
{
    return st->term_pdR != FILL_BURST_NONE64 &&
           (st->term_pdR == -16 || st->term_pdR == -15) &&
           dR_now >= -13;
}

/* Dead-zone routed fragment emission (terminal fragment rows and the
 * postterm descent share it verbatim): the trimmed fragment W0 .. W1-7
 * where it fits, the full close run at width <= 5 words, blank in the
 * 6-7 word gap. */
static void fill_burst_plan_deadzone(struct fill_tri_plan *ud, int32_t W0,
                                     int32_t W1, uint8_t open_be,
                                     int32_t px1)
{
    if (W1 - 7 >= W0)
        fill_burst_plan_run1(ud, W0, W1 - 7, open_be, 0xffu);
    else if (W1 - W0 <= 4)
        fill_burst_plan_run1(ud, W0, W1, 0xffu,
                             fill_burst_close_mask(px1));
    else
        fill_burst_plan_blank(ud);
}

static void fill_burst_row(fill_burst_state *st, int anyvalid,
                           int32_t px0, int32_t px1,
                           int64_t xl_now, int64_t xlr_now,
                           int32_t spread, int is_last, struct fill_tri_plan *ud)
{
    if (!anyvalid)
    {
        /* Invalid scanline: nothing rendered, machine state frozen. */
        fill_burst_plan_blank(ud);
        return;
    }

    const int32_t W0 = px0 >> 1;
    const int32_t W1 = px1 >> 1;
    const int32_t pp = st->prev_px0;     /* previous span's start pixel */
    const int32_t pp2 = st->prev_px0_2;  /* two rows back */
    st->prev_px0_2 = pp;
    st->prev_px0 = px0;

    /* One-pixel spans render only when the span start moved by 2+ px. */
    if (px0 == px1 && (pp == FILL_BURST_NONE32 || px0 - pp < 2))
    {
        fill_burst_plan_blank(ud);
        return;
    }

    if (!st->armed)
    {
        fill_burst_plan_normal(ud, W0, W1, px0, px1);
        if ((px0 & 15) == 0)
        {
            /* Arm: the arming row itself renders the normal single-burst
             * pattern. */
            st->armed = 1;
            st->just_armed = 1;
            fill_burst_seed(st, W1, xl_now, xlr_now, 1);
        }
        else
        {
            st->xl_prev = xl_now;
            st->xlr_prev = xlr_now;
        }
        return;
    }

    const int64_t R_entry_px = st->R >> 16;
    const int T_entry = st->T;

    /* Clip-release catch-up climb: on the row after a scissor-
     * clip release (clamped edge stationary two rows back: xl_prev2 ==
     * xl_prev3, and moving again: xl_prev > xl_prev2) at T == -28, the
     * FIFO climb catches up to the LIVE edge instead of the lagged one
     * iff the previous edge's subpixel sits in the low band
     * ((xl_prev >> 12) & 7 <= 2). 5/5 catch-up vs 6/6 lagged in refs. */
    const int relsig = (st->T == -28 && st->xl_prev2 != FILL_BURST_NONE64 &&
                        st->xl_prev3 != FILL_BURST_NONE64 &&
                        st->xl_prev2 == st->xl_prev3 &&
                        st->xl_prev > st->xl_prev2 &&
                        st->xlr_prev != FILL_BURST_NONE64 &&
                        st->xlr_prev2 != FILL_BURST_NONE64 &&
                        st->xlr_prev - st->xlr_prev2 >= ((int64_t)16 << 16));
    if (relsig && ((st->xl_prev >> 12) & 7) <= 2)
        st->climb_pending = xl_now - st->xl_prev2;

    /* T==2 clip-release multi-drop signature: edge was scissor-
     * clamped two rows back (clamped != raw), released last row (clamped
     * == raw), fast raw slope. Computed pre-rotation. */
    const int relsig2 = (st->T == 2 && st->xl_prev2 != FILL_BURST_NONE64 &&
                         st->xlr_prev2 != FILL_BURST_NONE64 &&
                         st->xl_prev2 != st->xlr_prev2 &&
                         st->xl_prev == st->xlr_prev &&
                         st->xlr_prev - st->xlr_prev2 >= ((int64_t)16 << 16));
    const int rel2lo_now = st->rel2lo;

    /* Fast first-armed-row: when the arm row was NOT clipped
     * (xl_prev == xlr_prev) and the live edge delta is fast (>= 12 px;
     * corpus bracket (8, 14.78], clean band [10, 14]), the first armed
     * row climbs by the live delta instead of the arm zero, and the drop
     * is the per-compare multi-drop at Tpx. 2/2 in refs; clipped arms
     * excluded (their release machinery owns those rows). */
    const int ja = st->just_armed;
    st->just_armed = 0;
    int fastarm = 0;
    if (ja && st->xl_prev != FILL_BURST_NONE64 &&
        st->xlr_prev != FILL_BURST_NONE64 &&
        st->xl_prev == st->xlr_prev &&
        (xl_now - st->xl_prev) >= ((int64_t)12 << 16))
    {
        st->climb_pending = xl_now - st->xl_prev;
        fastarm = 1;
    }
    if (relsig2)
    {
        st->rel2 = 1;
        if (st->xl_prev < ((int64_t)16 << 16))
            st->rel2lo = 1;
    }
    if (rel2lo_now && st->T == 2)
    {
        st->rel2lo = 0;
        st->climb_pending = xl_now - st->xl_prev2;
    }

    /* Apply the lagged climb; the crossing features below are computed
     * from the PRE-rotation edge history (a twice-hit lag trap). */
    st->R += st->climb_pending;
    const int64_t Rpx_preT = st->R >> 16;

    int64_t nblocks = 0;
    if (st->xl_prev2 != FILL_BURST_NONE64)
    {
        nblocks = (st->xl_prev >> 20) - (st->xl_prev2 >> 20);
        if (nblocks < 0)
            nblocks = 0;
    }
    const int rawcrossed_lag = (st->xlr_prev2 != FILL_BURST_NONE64) &&
        ((st->xlr_prev >> 20) != (st->xlr_prev2 >> 20));
    int64_t rawnblocks = 0;
    if (st->xlr_prev2 != FILL_BURST_NONE64)
    {
        rawnblocks = (st->xlr_prev >> 20) - (st->xlr_prev2 >> 20);
        if (rawnblocks < 0)
            rawnblocks = 0;
    }
    st->climb_pending = xl_now - st->xl_prev;
    st->xl_prev3 = st->xl_prev2;
    st->xl_prev2 = st->xl_prev;
    st->xl_prev = xl_now;
    st->xlr_prev2 = st->xlr_prev;
    st->xlr_prev = xlr_now;

    if (st->T == -12)
    {
        /* T=-12 steady-phase drops, three parts pinned by hardware refs:
         *  (a) the drop gate is strictly Rpx > 2*W1 - 20: rows parked at
         *      exactly 2*W1 - 20 with a crossing take no drop (5/5),
         *  (b) multi-crossing rows take one 16 px drop per crossing, each
         *      drop individually gated on the CURRENT Rpx (6/6 incl. a
         *      2-crossing row that takes both),
         *  (c) if the pointer ENTERED the row below the window floor
         *      2*W1 - 28, one drop is forced even without a crossing
         *      (3/3; the same gate excludes slow steady rows since a
         *      small climb from below the floor cannot exceed
         *      2*W1 - 20). */
        fill_burst_drop_while(st, 2 * W1 - 19,
                              rawcrossed_lag ? rawnblocks
                              : ((R_entry_px < 2 * W1 - 28) ? 1 : 0));
    }
    else
    {
        const int64_t Tpx = 2 * W1 + st->T + 1;
        int tpx_consumed = 0;
        if (rel2lo_now && st->T == 2)
        {
            fill_burst_drop_while(st, 2 * W1 - 7, -1);
            if ((st->R >> 16) < 2 * W1 - 12)
                st->T = -12;
            tpx_consumed = 1;
        }
        if (fastarm)
        {
            fill_burst_drop_while(st, Tpx, -1);
            tpx_consumed = 1;
        }
        if (!tpx_consumed)
        {
            /* On the clip-release row at T == -28 the burst
             * pointer multi-drops into the [2*W1-28, 2*W1-12) window (one
             * 16 px drop per compare, repeated), instead of the steady
             * single drop. Reproduces all 11 release-row landings exactly
             * (k = 1..6). */
            if ((relsig || (st->rel2 && st->T == -28)) &&
                (st->R >> 16) >= 2 * W1 - 12)
            {
                fill_burst_drop_while(st, 2 * W1 - 12, -1);
                st->T = -12;
            }
            else if ((st->R >> 16) >= Tpx)
            {
                st->R -= (int64_t)16 << 16;
                if (st->T == -28)
                    st->T = -12;
                /* On the T==2 clip-release row the drop repeats, each
                 * compare individually gated on Rpx >= Tpx (the steady
                 * multi-drop shape at the T=2 threshold). 2/2 exact;
                 * non-clipped fast traces keep the single drop. */
                else if (st->rel2)
                    fill_burst_drop_while(st, Tpx, -1);
            }
        }
        if (st->T == 2 && (st->R >> 16) < 2 * W1 - 12)
            st->T = -28;
    }
    /* Staircase-phase concurrent crossing: a clipped-edge block crossing
     * while still in the T=2 phase, gated above 2*W1 + 13 px, fires the
     * per-block drop and hands the machine straight to steady. */
    if (st->T == 2 && nblocks > 0 && (st->R >> 16) > 2 * W1 + 13)
    {
        st->R -= ((int64_t)16 << 16) * nblocks;
        st->T = -12;
    }

    int64_t Rpx = st->R >> 16;
    int64_t S = Rpx >> 1;

    /* The -29 lattice signature (entry exactly one px below the -28
     * window floor) keys two behaviors: the last-row tail-trim cancel
     * below, and the non-last anti-drop terminal entry. */
    const int m29 = (R_entry_px - 2 * W1 == -29);

    /* Armed-stay graze: a would-be terminal entry whose PREVIOUS
     * row's pointer grazed the window floor (prev_S - (W1-15) in [0, 4])
     * and whose own landing barely reaches the span (S - W0 >= -1) does
     * NOT enter terminal; the row emits the armed tail instead. 4/4 with
     * 0 counterexamples in the 1029-entry terminal census at d <= 5.
     * One-row flag only; the left_bottom boundary relax below is scoped
     * to graze rows (a global relax regresses 19404 rows against the
     * captures). */
    const int graze = (st->terminal < 0 && S <= W0 &&
                       st->prev_S != FILL_BURST_NONE64 &&
                       st->prev_S - ((int64_t)W1 - 15) >= 0 &&
                       st->prev_S - ((int64_t)W1 - 15) <= 4 &&
                       S - W0 >= -1);
    const int entering = (st->terminal < 0 && S <= W0 && !graze);

    /* Non-last -29-entry anti-drop shape: an armed T==-12 row
     * entering at the -29 lattice that takes a terminal entry does NOT
     * emit the plain age-0 run. The write stream instead reflects the
     * pre-drop pointer vaulted one window UP (R_anti = Rpx_preT + 16):
     * trimmed head fragment W0 .. W1-7 full, the pointer partial at
     * S_anti by parity, and the W1 word full (no close). The PARKED
     * state is unchanged (the following row matches the ordinary
     * terminal model). 1/1 with 21493/21493 non-entering -29 controls
     * and the sole last-row -29 entry both unaffected. */
    const int anti29 = (entering && T_entry == -12 && !is_last && m29);

    /* Terminal transition: the resume pointer reached the span start. */
    if (entering)
    {
        st->terminal = 0;
        st->park = st->R;
        /* Fragment gate: R sits exactly 1 px under a lagged edge that
         * freshly landed on a 16 px block. */
        st->tfrag_gate = (st->xl_prev2 != FILL_BURST_NONE64 &&
                          st->xl_prev3 != FILL_BURST_NONE64 &&
                          (st->R - st->xl_prev2) == -((int64_t)1 << 16) &&
                          ((st->xl_prev2 >> 16) & 15) == 0 &&
                          ((st->xl_prev3 >> 16) & 15) != 0);
        st->tfrag_first = 1;
        st->tfrag_ref = st->park;
        st->term_pdR = FILL_BURST_NONE64;
        /* Blank-gate latch: terminal entered on the T==2 transition row with
         * the park exactly 1 px below a block-aligned clamped edge (same
         * d==-1/a2==0 signature as the tfrag gate, a3 unconstrained). */
        st->t2cross = (T_entry == 2 && st->xl_prev2 != FILL_BURST_NONE64 &&
                       (st->R - st->xl_prev2) == -((int64_t)1 << 16) &&
                       ((st->xl_prev2 >> 16) & 15) == 0);
    }

    if (st->terminal >= 0)
    {
        const int64_t dR_now = Rpx_preT - 2 * W1;

        /* Mid-terminal event: a lagged clipped-edge block crossing with
         * the edge freshly one-past the boundary blanks the row (with the
         * queued FIFO partial flushing through); only the 6-word
         * dead-zone width manifests. */
        if (st->terminal >= 1 &&
            st->xl_prev2 != FILL_BURST_NONE64 &&
            st->xl_prev3 != FILL_BURST_NONE64 &&
            (st->xl_prev2 >> 20) != (st->xl_prev3 >> 20) &&
            ((st->xl_prev2 >> 16) & 15) == 1 &&
            (W1 - W0 + 1) == 6)
        {
            fill_burst_plan_blank(ud);
            if (fill_burst_fifo_flush(st, dR_now))
            {
                ud->m_fill_pw = (int16_t)W1;
                ud->m_fill_pm = st->term_pRodd ? 0x80u : 0xf8u;
            }
            st->term_pdR = dR_now;
            st->term_pRodd = (int)(Rpx_preT & 1);
            st->terminal++;
            if ((px0 & 15) == 0)
                st->rearm_pending = 1;
            st->prev_S = S;
            st->prev_W1 = W1;
            return;
        }

        /* Open-trim comparator: stale px0 takes the trim, EXCEPT the
         * first stale row of a 6-word span (110/110 no-trim in refs;
         * second-and-later stale rows and all other widths trim).
         * Fresh-entry sub-span landing trim: when the entry
         * row's PRE-DROP pointer lands exactly one pixel below the span
         * start (Rpx_preT == px0 - 1; only reachable with odd px0,
         * pointer at the even word head 2*W0), the full-row open byte
         * takes the trim as well. 1/1 vs 3/3 dpre==0 controls landing
         * at/above px0 opening full; landing census: no other corpus
         * terminal entry reaches below px0 within word W0. (The
         * open_mask and partial-parity readings are observationally
         * degenerate here -- both force 0xf8.) */
        const int stale_trim = (pp != FILL_BURST_NONE32 && pp == px0 &&
                                !((W1 - W0 + 1) == 6 &&
                                  (pp2 == FILL_BURST_NONE32 || pp2 != px0)));
        const uint8_t ob_stale = stale_trim ? fill_burst_open_mask(px0)
                                            : 0xffu;
        const uint8_t ob_full =
            (stale_trim || (st->terminal == 0 && Rpx_preT == px0 - 1))
                ? fill_burst_open_mask(px0) : 0xffu;
        const int will_rearm = ((px0 & 15) != 0 && st->rearm_pending);
        /* Lag-2 block-landing fragment: at terminal index 1 with
         * px0&15 == 1 and no rearm pending, the row fragments iff the
         * edge two rows back was block-aligned (29/29 vs 0/22). */
        const int frag_lag2 = (st->terminal == 1 && (px0 & 15) == 1 &&
                               !st->rearm_pending &&
                               st->xl_prev3 != FILL_BURST_NONE64 &&
                               ((st->xl_prev3 >> 16) & 15) == 0);
        /* Block-landing fragment also fires at terminal index 1 when a
         * re-arm is already latched (19/19, zero counterexamples). */
        const int blockfrag = ((px0 & 15) == 0 &&
                               (st->terminal >= 2 ||
                                (st->terminal >= 1 && st->rearm_pending)));

        /* Second terminal row (age 1) after a T==2-transition
         * narrow entry: the row is BLANK iff the parked pointer crosses an
         * integer pixel boundary this row (frac(park)+climb >= 1). 6/6
         * blanks vs 7/7 emitters exact; T==-28 entries excluded (they
         * close-emit even when crossing). */
        if (st->terminal == 1 && st->t2cross && !st->tfrag_gate &&
            (W1 - W0) >= 5 && (W1 - W0) <= 6 &&
            Rpx_preT > (st->park >> 16))
        {
            fill_burst_plan_blank(ud);
        }
        else if ((will_rearm || blockfrag || frag_lag2) && W1 - 7 >= W0)
        {
            /* frag_lag2 and the tfrag machinery detect the same hardware
             * event; consume tfrag_first so the next row doesn't
             * re-frag. */
            if (frag_lag2)
                st->tfrag_first = 0;
            fill_burst_plan_run1(ud, W0, W1 - 7, ob_stale, 0xffu);
        }
        else if (anti29 && W1 - 7 >= W0)
        {
            /* Anti-drop emission override (see the entry-flag comment above). */
            const int64_t Ranti = Rpx_preT + 16;
            const int64_t Santi = Ranti >> 1;
            fill_burst_plan_run1(ud, W0, W1 - 7, 0xffu, 0xffu);
            if (Santi >= W0 && Santi <= W1)
            {
                ud->m_fill_pw = (int16_t)Santi;
                ud->m_fill_pm = (Ranti & 1) ? 0x80u : 0xf8u;
            }
            ud->m_fill_t2lo = (int16_t)W1;
            ud->m_fill_t2hi = (int16_t)W1;
        }
        else if (st->terminal >= 1 && st->tfrag_gate && !is_last &&
                 !(st->terminal == 1 && st->t2cross &&
                   Rpx_preT <= (st->park >> 16)) &&
                 (st->tfrag_first ||
                  xl_now < st->tfrag_ref + ((int64_t)2 << 16)))
        {
            /* Fragment rows: fire at least once on the gate, continue
             * while the edge is inside park + 2 px; the dead-zone routing
             * covers narrow spans. On the LAST row the
             * fragment/blank behaviors give way to the plain full run
             * with close -- the is_last exclusion here. The terminal >= 1
             * guard is explicit rather than implied by branch order: the
             * age-0 entry row always emits the full run. */
            st->tfrag_first = 0;
            fill_burst_plan_deadzone(ud, W0, W1, ob_stale, px1);
        }
        else
        {
            /* Terminal full rows (entry row included): the stale-span-
             * start trim comparator, plus the fresh-entry landing trim
             * above. */
            fill_burst_plan_run1(ud, W0, W1, ob_full,
                                 fill_burst_close_mask(px1));
        }

        /* k=0 close suppression (deferred close lost at the event) --
         * UNLESS a FIFO partial launches or flushes on this row: the
         * queued-partial write carries the close through. Brackets:
         * kz&flush 1/1 CLOSE|PART; kz&band&last 3/3 CLOSE|PART vs
         * band&last no-kz 12/12 plain CLOSE; kz alone 47/50 FF.
         * On a FRESH terminal entry (age 0) on the last row,
         * the close suppression is decided by the entry T, not the
         * spread: T==-12 suppresses (19/19), T==2/-28 closes (19/19).
         * Ages 1-2 keep the banked spread-based rule.
         * Shallow width-4 exception: a 4-word span whose
         * parked R entered at dR >= -22 keeps the close (the five
         * suppressed width-4 rows all entered at dR <= -23; width 2-3
         * rows are unaffected).
         * Aged terminal close suppression: at ages 3-4 with
         * T==-12, when the pointer crosses the window ceiling into the
         * [2*W1-12, 2*W1-11] band this row, the close is lost (6/6 vs
         * 10/10 closing above the band; 27 age>=5 band rows close, hence
         * the age bracket). */
        const int flush = fill_burst_fifo_flush(st, dR_now);
        const int kz =
            ((will_rearm && spread >= FILL_BURST_KZ_REARM) ||
             (is_last && st->terminal == 0 && T_entry == -12) ||
             (is_last && st->terminal >= 1 && st->terminal <= 2 &&
              spread >= FILL_BURST_KZ_TERM &&
              !(W1 - W0 == 3 && R_entry_px - 2 * W1 >= -22)) ||
             (st->terminal >= 3 && st->terminal <= 4 && st->T == -12 &&
              R_entry_px < 2 * W1 - 12 &&
              Rpx_preT >= 2 * W1 - 12 && Rpx_preT <= 2 * W1 - 11));
        const int launch_dump = kz && is_last &&
                                (dR_now == -16 || dR_now == -15);
        if (kz && !(flush || launch_dump) &&
            ud->m_fill_b1hi == (int16_t)W1)
        {
            /* The suppression maps the W1 word of the emitted run to a
             * full write (open trim included when the run is W1 alone);
             * runs not reaching W1 -- fragments, blanks -- are read
             * straight off the plan and left untouched. */
            ud->m_fill_close = 0xffu;
            if (ud->m_fill_b1lo == (int16_t)W1)
                ud->m_fill_open = 0xffu;
        }
        if (flush)
        {
            ud->m_fill_pw = (int16_t)W1;
            ud->m_fill_pm = st->term_pRodd ? 0x80u : 0xf8u;
        }
        if (launch_dump)
        {
            ud->m_fill_pw = (int16_t)W1;
            ud->m_fill_pm = (dR_now & 1) ? 0x80u : 0xf8u;
        }
        st->term_pdR = dR_now;
        st->term_pRodd = (int)(Rpx_preT & 1);
        st->terminal++;

        /* Re-arm one row after a fresh 16 px block landing. */
        if ((px0 & 15) == 0)
        {
            st->rearm_pending = 1;
        }
        else if (st->rearm_pending)
        {
            st->terminal = -1;
            st->rearm_pending = 0;
            st->postterm = 1;
            fill_burst_seed(st, W1, xl_now, xlr_now, 0);
        }
        return;
    }

    /* Postterm descent rows with the pointer above the close word take
     * the trim-fragment/blank shapes; the LAST row instead takes the
     * plain full run with close (7/7 in refs, width<=4 close path
     * 18/18). */
    if (st->postterm && S > W1)
    {
        const uint8_t ob = (pp != FILL_BURST_NONE32 && pp == px0)
            ? fill_burst_open_mask(px0) : 0xffu;
        st->prev_S = S;
        st->prev_W1 = W1;
        if (is_last)
            fill_burst_plan_run1(ud, W0, W1, ob,
                                 fill_burst_close_mask(px1));
        else
            fill_burst_plan_deadzone(ud, W0, W1, ob, px1);
        return;
    }

    /* Burst row: first burst [W0 .. E] with E snapped 8 words below the
     * resume word, then the tail [S .. W1]. */
    int64_t E = fill_burst_e_snap(S, 2, W1);
    /* Narrow-span dead zone / postterm landing: spans of at most 5 words
     * (and postterm descent landings) revert to a full first burst where
     * the burst geometry cannot fit. */
    int post_fallback = 0;
    int narrow_fall = 0;
    if (E < W0 && ((W1 - W0) <= 4 || st->postterm))
    {
        post_fallback = st->postterm;
        narrow_fall = !st->postterm;
        E = W1;
    }
    st->postterm = 0;

    /* Narrow-fallback overshoot blank: when the narrow (width
     * <= 5 word) fallback raises E to W1 but the burst pointer sits
     * STRICTLY ABOVE the close word (S > W1), the row emits nothing.
     * 1/1 vs 28/28 narrow-fallback controls at S <= W1 rendering; the
     * postterm fallback family is untouched (owned by the branch
     * above). */
    if (narrow_fall && S > W1)
    {
        st->prev_S = S;
        st->prev_W1 = W1;
        fill_burst_plan_blank(ud);
        return;
    }

    fill_burst_plan_blank(ud);
    if (E >= W0)
    {
        /* Postterm landing rows (E raised to W1 via the descent fallback)
         * take the stale-span-start open trim like terminal rows do:
         * 64/64 stale rows trim, 8/8 fresh rows don't, in refs.
         * Last-row narrow-fallback with the burst pointer
         * strictly INSIDE the span (W0 < S < W1): the open byte takes
         * the 0xf8 trim and the close is suppressed. 1/1; this is the
         * only last-row inside occurrence in the corpus. */
        const int inside_trim = (narrow_fall && is_last && S > W0 && S < W1);
        const uint8_t ob = (post_fallback && pp != FILL_BURST_NONE32 &&
                            pp == px0) ? fill_burst_open_mask(px0) : 0xffu;
        fill_burst_plan_run1(ud, W0, (int32_t)E,
                             inside_trim ? 0xf8u : ob,
                             inside_trim ? 0xffu
                                         : fill_burst_close_mask(px1));
        /* Pointer-at-close composite: on the LAST row, when the
         * burst pointer parks exactly at the close word (S == W1), the
         * queued tail partial co-writes with the close byte
         * (close | 0x80/0xf8 by Rpx parity). 1/1 with 0 counterexamples:
         * no other corpus row has S == W1 on a last-row armed close
         * emission. */
        if (narrow_fall && is_last && S == W1 && E == W1)
            ud->m_fill_close |= (Rpx & 1) ? 0x80u : 0xf8u;
    }

    /* Generalized tail trim: fires whenever the pointer ENTERED the row
     * from below the -28 window floor (prev post-drop S at or below
     * W1 - 15) and now sits above it -- the ==W1-15 form was the
     * steady-state special case (fast climbs can jump from S 44..46
     * too). Grazing entries relax the boundary to >=. Last-row -29
     * entry (the m29 lattice signature): the trim is cancelled (tail
     * runs to W1, no close). 14/14 at dR_entry==-29 vs 0/222 across
     * -28..-17; trim cancel 11/11. */
    const int left_bottom = (st->prev_S != FILL_BURST_NONE64 &&
                             st->prev_S <= (int64_t)st->prev_W1 - 15 &&
                             (graze ? (S >= (int64_t)W1 - 15)
                                    : (S > (int64_t)W1 - 15)) &&
                             !(is_last && m29));
    /* Last-row block drop to the -28 lattice: when the span start sits
     * within 3 px of a block boundary (px0&15 <= 2), the burst pointer
     * takes one extra 16 px drop, landing one below the nearest
     * -28-lattice boundary strictly under Rpx, plus px0's pixel parity:
     *   R' = base - 1 + (px0 & 1),  base = max(2*W1-28-16k) < Rpx.
     * Parked (no drop) when Rpx sits at the lattice or lattice+1 (3/3
     * park rows at mod==1 vs 0 fired; the 30/30 fired evidence is
     * mod >= 13). Even px0 lands odd (0x80 partial), odd px0 lands even
     * (0xf8); W0 clip takes the open mask. */
    if (is_last && st->T == -12 && (px0 & 15) <= 2 &&
        fill_burst_mod16(Rpx - (2 * W1 - 28)) >= 2)
    {
        int64_t base = 2 * W1 - 28;
        while (base >= Rpx)
            base -= 16;
        Rpx = base - 1 + (px0 & 1);
        S = Rpx >> 1;
    }
    const int64_t tail_end = left_bottom ? (int64_t)W1 - 7 : (int64_t)W1;
    /* Close-on-stalled-tail: when the burst pointer stalls (S == prev
     * row's S) one drop above landing (S - W0 == 7) with an exactly
     * two-block tail (W1 - S == 15), the tail's end word takes the close
     * mask. 131/131 vs 0/248 (unstalled) and 0/589 (dW1=14) in refs.
     * Deep-entry head-landing close survival: on an armed row
     * entering STRICTLY below the -28 window floor (R_entry - 2*W1 <=
     * -29) whose pointer lands in the span head, the tail's close byte
     * survives too. Head test: S - W0 <= 2 unconditionally, or
     * S - W0 <= 7 with the pointer stalled (S == prev_S). 3/3; the sole
     * unstalled depth-7 deep entry in the corpus keeps 0xff; every other
     * head landing sits at dR >= -28. */
    const int stalled = (st->prev_S != FILL_BURST_NONE64 &&
                         S == st->prev_S);
    const int tail_close =
        ((stalled && S - W0 == 7 && (int64_t)W1 - S == 15) ||
         (R_entry_px - 2 * W1 <= -29 &&
          (S - W0 <= 2 || (S - W0 <= 7 && stalled))));
    if (S <= tail_end && E < W1)
    {
        if (S >= W0)
        {
            ud->m_fill_pw = (int16_t)S;
            ud->m_fill_pm = (Rpx & 1) ? 0x80u : 0xf8u;
        }
        const int64_t t2lo = (S + 1 > W0) ? S + 1 : W0;
        if (t2lo <= tail_end)
        {
            ud->m_fill_t2lo = (int16_t)t2lo;
            ud->m_fill_t2hi = (int16_t)tail_end;
            if (tail_close && tail_end == W1)
                ud->m_fill_t2close = fill_burst_close_mask(px1);
        }
    }
    st->prev_S = S;
    st->prev_W1 = W1;
}

/* ------------------------------------------------------------------------
 * Adapter: everything below is this renderer's side of the model.
 * ---------------------------------------------------------------------- */

/* One 64-bit word of a FILL write: the enabled bytes of the fill value
 * (bit 7 of the mask is the word's first byte). The fill colour repeats
 * every four bytes of the colour image, whatever its pixel size. */
static void fill_tri_write_word(uint32_t wid, uint32_t rowb, int32_t w, uint8_t be)
{
    const uint32_t fba = state[wid].fb_address;
    const uint32_t fval = state[wid].fill_color;
    const uint8_t h0 = (fval & 0x10000) ? 3 : 0;
    const uint8_t h1 = (fval & 0x1) ? 3 : 0;
    uint32_t b;

    for (b = 0; b < 8u; b++)
    {
        uint32_t addr, bp, in;
        if (!(be & (0x80u >> b)))
            continue;
        addr = rowb + ((uint32_t)w << 3) + b;
        if (addr < fba)
            continue;
        bp = (addr - fba) & 3u;
        in = addr & px_mask8;
        if (px_valid_idx8(in))
        {
            px8[in ^ BYTE_ADDR_XOR] = (uint8_t)(fval >> ((3u - bp) << 3));
            px_hidden[in >> 1] = (bp < 2u) ? h0 : h1;
        }
    }
}

/* A row with a plan attached: execute it verbatim. */
static void fill_tri_run_plan(uint32_t wid, int line, const struct fill_tri_plan *pl)
{
    const uint32_t rowb = state[wid].fb_address + ((uint32_t)(state[wid].fb_width * line) << 2);
    int32_t w;

    for (w = pl->m_fill_b1lo; w <= pl->m_fill_b1hi; w++)
    {
        uint8_t be = 0xffu;
        if (w == pl->m_fill_b1lo) be &= pl->m_fill_open;
        if (w == pl->m_fill_b1hi) be &= pl->m_fill_close;
        if (be != 0u)
            fill_tri_write_word(wid, rowb, w, be);
    }
    if (pl->m_fill_pw >= 0 && pl->m_fill_pm != 0u)
        fill_tri_write_word(wid, rowb, pl->m_fill_pw, pl->m_fill_pm);
    for (w = pl->m_fill_t2lo; w <= pl->m_fill_t2hi; w++)
    {
        uint8_t be = 0xffu;
        if (w == pl->m_fill_t2hi) be &= pl->m_fill_t2close;
        if (be != 0u)
            fill_tri_write_word(wid, rowb, w, be);
    }
}

/* The write law of a FILL triangle span that walks right to left (major
 * edge on the right) and has no plan, from cen64's fill_write_span. With
 * a0/a1 the span's first and last byte, p0/p1 their positions in their
 * 64-bit words, q the end pixel's index in its word and B = ceil(p0/4),
 * the row is the single run [W0 + (q < B) .. W1]: the last word carries
 * only the bytes at or after p1, the first only those at or before p0
 * unless q - B >= 2 - the trims are complemented with respect to the
 * span, an end-of-span comparator resolving the wrong way. Spans that
 * walk left to right are written as a plain run, as rectangles are. */
static void fill_tri_write_span(uint32_t wid, int line, int32_t lo, int32_t hi, uint32_t bpp)
{
    const uint32_t fb_index = (uint32_t)(state[wid].fb_width * line);
    const uint32_t a0 = state[wid].fb_address + (fb_index + (uint32_t)lo) * bpp;
    const uint32_t a1 = state[wid].fb_address + (fb_index + (uint32_t)hi) * bpp + bpp - 1u;
    const uint32_t W1 = a1 >> 3;
    const int32_t p0 = (int32_t)(a0 & 7u), p1 = (int32_t)(a1 & 7u);
    const uint32_t ppw = 8u / bpp;
    const int32_t q = (int32_t)((fb_index + (uint32_t)hi) & (ppw - 1u));
    const int32_t B = (p0 + 3) >> 2;
    const uint32_t S = (a0 >> 3) + ((q < B) ? 1u : 0u);
    uint32_t w;

    if (hi < lo || S > W1)
        return;
    for (w = S; w <= W1; w++)
    {
        uint8_t be = 0xffu;
        if (w == S && (q - B) < 2 && p0 != 0)
            be &= (uint8_t)((0xffu << (7 - p0)) & 0xffu);
        if (w == W1)
            be &= (uint8_t)(0xffu >> p1);
        if (be != 0u)
            fill_tri_write_word(wid, 0u, (int32_t)w, be);
    }
}

/* Run the sequential models over every scanline of a triangle and leave a
 * plan in each span. The models carry state from row to row, so this is a
 * walk of its own over the whole primitive: the renderer's walk visits
 * only the lines its lane owns. Mirrors the feed in cen64's edge walker.
 * Returns whether plans were attached. */
static int fill_tri_prepass(uint32_t wid, int flip, int32_t yh, int32_t ym, int32_t yl,
                            int32_t xh, int32_t xm, int32_t xl,
                            int32_t dxhdy, int32_t dxmdy, int32_t dxldy)
{
    fill_burst_state fburst;
    fill_famb_state famb;
    const int32_t sc_xh_raw = state[wid].clip.xh, sc_xl_raw = state[wid].clip.xl;
    const int32_t clipy1 = state[wid].clip.yh >> 2;
    const int32_t clipy2 = (state[wid].clip.yl + 3) >> 2;
    const int32_t ycur = yh & ~3, ylfar = yl | 3;
    int32_t fburst_start = yh >> 2, fburst_end = yl >> 2, fburst_lastj;
    int32_t fburst_xlraw0 = 0, fburst_xlclip0 = 0;
    int fburst_anyvalid = 0;
    int32_t famb_xrmax = INT_MIN, famb_xr3 = 0;
    int famb_r3valid = 0;
    int32_t xleft, xright, xleft_inc, xright_inc, startx = 0, endx = 0, k, lj;

    memset(&fburst, 0, sizeof(fburst));
    memset(&famb, 0, sizeof(famb));
    fburst.active = (dxhdy == 0 && dxmdy == 0 && !flip
                     && (state[wid].fb_address & 63u) == 0
                     && (state[wid].fb_width & 15u) == 0);
    fburst.terminal = -1;
    fburst.xl_prev = fburst.xl_prev2 = fburst.xl_prev3 = FILL_BURST_NONE64;
    fburst.xlr_prev = fburst.xlr_prev2 = FILL_BURST_NONE64;
    fburst.prev_S = FILL_BURST_NONE64;
    fburst.term_pdR = FILL_BURST_NONE64;
    fburst.prev_px0 = fburst.prev_px0_2 = FILL_BURST_NONE32;

    famb.active = (dxldy == 0 && (dxhdy != 0 || dxmdy != 0) && !flip
                   && (state[wid].fb_address & 7u) == 0
                   && (state[wid].fb_width & 1u) == 0);
    famb.clip = sc_xl_raw >> 2;
    famb.w1row = (int32_t)(state[wid].fb_width >> 1) - 1;
    famb.w1c = rdp_min32(famb.clip >> 1, famb.w1row);

    if (clipy2 <= 0)
        fburst.active = famb.active = 0;
    if (!fburst.active && !famb.active)
        return 0;
    if ((ycur >> 2) >= clipy2 && (ylfar >> 2) >= clipy2)
        return 0;
    if ((ycur >> 2) < clipy1 && (ylfar >> 2) < clipy1)
        return 0;

    if (fburst_start < clipy1) fburst_start = clipy1;
    if (fburst_start >= clipy2) fburst_start = clipy2 - 1;
    if (fburst_end < clipy1) fburst_end = clipy1;
    if (fburst_end >= clipy2) fburst_end = clipy2 - 1;
    famb.lastj = (yl - 1) >> 2;
    if (famb.lastj > fburst_end) famb.lastj = fburst_end;

    fburst_lastj = fburst_start - 1;
    for (lj = fburst_end; lj >= fburst_start; lj--)
    {
        if (4 * lj + 3 < yh || 4 * lj >= yl)
            continue;
        if (state[wid].scfield && (state[wid].sckeepodd ^ (lj & 1)))
            continue;
        fburst_lastj = lj;
        break;
    }

    xright = xh & ~1; xright_inc = (dxhdy >> 2) & ~1;
    xleft = xm & ~1;  xleft_inc = (dxmdy >> 2) & ~1;

    for (k = ycur; k <= ylfar; k++)
    {
        int32_t xleft_w, xright_w, scr_l, scr_r, xleft_c, xright_c, xstart, xend;
        const int32_t sxlo = sc_xh_raw << 14, sxhi = sc_xl_raw << 14;
        const int32_t j = k >> 2, spix = k & 3;
        int valid_y;

        if (k == ym)
        {
            xleft = xl & ~1;
            xleft_inc = (dxldy >> 2) & ~1;
        }
        xleft_w  = (int32_t)((uint32_t)xleft  << 4) >> 4;
        xright_w = (int32_t)((uint32_t)xright << 4) >> 4;
        scr_l = flip ? xright_w : xleft_w;
        scr_r = flip ? xleft_w : xright_w;
        if ((scr_l >> 14) > (scr_r >> 14))
        {
            scr_l = 0xfff << 16;
            scr_r = 0;
        }
        else
        {
            scr_l = scr_l < sxlo ? sxlo : (scr_l > sxhi ? sxhi : scr_l);
            scr_r = scr_r < sxlo ? sxlo : (scr_r > sxhi ? sxhi : scr_r);
        }
        xleft_c = flip ? scr_r : scr_l;
        xright_c = flip ? scr_l : scr_r;
        xstart = xleft_c >> 16;
        xend = xright_c >> 16;

        valid_y = !(k < yh || k >= yl);
        if (state[wid].scfield && (state[wid].sckeepodd ^ (j & 1)))
            valid_y = 0;

        if (spix == 0)
        {
            startx = flip ? 0 : 0xfff;
            endx = flip ? 0xfff : 0;
            fburst_xlraw0 = xleft_w;
            fburst_xlclip0 = xleft_c;
            fburst_anyvalid = 0;
            famb_xrmax = INT_MIN;
            famb_xr3 = 0;
            famb_r3valid = 0;
        }
        if (valid_y)
        {
            const int32_t famb_xr = flip ? xleft_w : xright_w;
            fburst_anyvalid = 1;
            if (famb_xr > famb_xrmax)
                famb_xrmax = famb_xr;
            if (spix == 3)
            {
                famb_xr3 = famb_xr;
                famb_r3valid = 1;
            }
            if (flip)
            {
                startx = rdp_max32(xstart, startx);
                endx = rdp_min32(xend, endx);
            }
            else
            {
                startx = rdp_min32(xstart, startx);
                endx = rdp_max32(xend, endx);
            }
        }
        if (spix == 3 && j >= 0 && j < 1024)
        {
            struct fill_tri_plan *pl = &state[wid].span[j].fplan;
            pl->m_fill_plan = 0;
            if (fburst.active && j >= fburst_start && j <= fburst_end)
                fill_burst_row(&fburst, fburst_anyvalid, startx, endx,
                               (int64_t)fburst_xlclip0, (int64_t)fburst_xlraw0,
                               xleft_w - fburst_xlraw0, (j == fburst_lastj) ? 1 : 0, pl);
            if (famb.active && j >= fburst_start && j <= fburst_end)
                fill_famb_row(&famb, fburst_anyvalid, startx, famb_xrmax, famb_xr3,
                              famb_r3valid, j, pl);
        }

        xright += xright_inc;
        xleft += xleft_inc;
    }
    return 1;
}
