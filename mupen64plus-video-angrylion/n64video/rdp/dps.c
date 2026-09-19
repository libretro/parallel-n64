/* DPS Test-Mode span-buffer stream model.
 *
 * The RDP's span buffer is CPU-visible through the DPS BUFTEST registers,
 * and after a draw it holds the span pipeline's staged framebuffer words.
 * The model is cen64's (https://gitlab.com/jgemu/cen64, src/rdp/rdp_core.c
 * and rdp_core.h, rdp_dps_*), established there against snapper64's
 * "RDP Test-Mode - Span Tri" hardware captures, and is reproduced under
 * cen64's licence:
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
 * The law, from cen64:
 *  - One stream position per rasterized pixel, S = 0 at primitive start,
 *    phase-locked S == x (mod 4); slot = S mod 16, CPU-visible when S/16
 *    is even. The value is the write-stage word image,
 *    (r<<24)|(g<<16)|(b<<8)|(((cvg-1)&7)<<5), coverage 0 included.
 *  - Spans align their first pixel to its phase (odd x0: a head slot at
 *    phase x0-1) and pad through phase 3 at the end; every skipped
 *    position takes the value last emitted at the same phase.
 *  - The first span's residual-less head stays pending: cancelled by any
 *    later write at its position mod 32, else committed at primitive end
 *    with the final residual.
 *  - Fully y-scissored spans, and spans whose raw edge range misses the
 *    x-scissor, consume nothing; clipped pixels of surviving spans
 *    likewise.
 * Scope, as in cen64: triangles in 1-cycle mode on 16-bit and 32-bit
 * colour images, armed by the first DPS register write.
 *
 * What differs here is the threading. Each worker of this renderer walks
 * only the lines it owns, so every worker keeps its own copy of the
 * model: all of them schedule the same window from a walk of the whole
 * primitive, each captures the pixels of its own lines, and the read-out
 * merges the captures.
 */

static int al_dps_armed;

static void dps_slot(struct dps_model *m, uint32_t S, uint32_t phase_seen,
                     uint32_t pend, uint32_t *pend_alive, int emit)
{
    if (!(phase_seen & (1u << (S & 3u))))
        return;
    if (*pend_alive && ((S & 31u) == ((pend - 1u) & 31u)))
        *pend_alive = 0;
    if (emit && S >= m->base && S - m->base < RDP_DPS_WIN)
        m->sched[S - m->base] = 2;
}

static uint32_t dps_walk(uint32_t wid, int emit)
{
    struct dps_model *m = &state[wid].dps;
    uint32_t S = 0, phase_seen = 0, pend = 0, pend_alive = 0, i, k;
    int32_t seed_row[4] = { -1, -1, -1, -1 };
    int16_t seed_x[4] = { 0, 0, 0, 0 };

    for (i = 0; i < m->nrows; i++)
    {
        const int32_t x0 = m->row_x0[i], x1 = m->row_x1[i];
        const uint32_t w = (uint32_t)(x1 - x0 + 1);
        const uint32_t target = (uint32_t)(x0 - (x0 & 1)) & 3u;
        struct span *sp = &state[wid].span[m->row_span[i]];

        while ((S & 3u) != target)
            dps_slot(m, S++, phase_seen, pend, &pend_alive, emit);
        if (x0 & 1)
        {
            if (phase_seen & (1u << (S & 3u)))
                dps_slot(m, S, phase_seen, pend, &pend_alive, emit);
            else if (pend == 0)
            {
                pend = S + 1u;
                pend_alive = 1;
            }
            S++;
        }

        if (pend_alive && (w >= 32u || ((((pend - 1u) - S) & 31u) < w)))
            pend_alive = 0;
        if (emit && S + w > m->base)
        {
            uint32_t p;
            sp->dps_cap = 1;
            sp->dps_cap_x0 = (int16_t)((S >= m->base) ? x0 : x0 + (int32_t)(m->base - S));
            sp->dps_cap_x1 = (int16_t)x1;
            sp->dps_voff = (int32_t)S - x0 - (int32_t)m->base;
            for (p = (S >= m->base) ? S : m->base; p < S + w; p++)
                if (p - m->base < RDP_DPS_WIN)
                    m->sched[p - m->base] = 1;
        }
        if (emit && S < m->base)
        {
            const uint32_t nb = (m->base - S < w) ? (m->base - S) : w;
            const int32_t xcap = x0 + (int32_t)nb - 1;
            for (k = 0; k < 4; k++)
            {
                const int32_t x = xcap - (int32_t)(((uint32_t)xcap - k) & 3u);
                if (x >= x0)
                {
                    seed_row[k] = (int32_t)i;
                    seed_x[k] = (int16_t)x;
                }
            }
        }
        if (w >= 4u)
            phase_seen = 0xfu;
        else
        {
            int32_t x;
            for (x = x0; x <= x1; x++)
                phase_seen |= 1u << ((uint32_t)x & 3u);
        }
        S += w;

        while (S & 3u)
            dps_slot(m, S++, phase_seen, pend, &pend_alive, emit);
    }

    if (!emit)
    {
        m->pend_pos1 = pend;
        m->pend_alive = pend_alive;
    }
    else
    {
        for (k = 0; k < 4; k++)
        {
            struct span *sp;
            if (seed_row[k] < 0)
                continue;
            sp = &state[wid].span[m->row_span[seed_row[k]]];
            if (!sp->dps_cap)
            {
                sp->dps_cap = 1;
                sp->dps_cap_x0 = 1;
                sp->dps_cap_x1 = 0;
                sp->dps_voff = 0;
            }
            sp->dps_seed_x[sp->dps_seed_n] = seed_x[k];
            sp->dps_seed_k[sp->dps_seed_n] = (uint8_t)k;
            sp->dps_seed_n++;
        }
    }
    return S;
}

/* Walk the whole primitive, collect the rows that consume stream
 * positions, and schedule the final window. Mirrors the row collection in
 * cen64's edge walker. */
static void dps_prepass(uint32_t wid, int flip, int32_t yh, int32_t ym, int32_t yl,
                        int32_t xh, int32_t xm, int32_t xl,
                        int32_t dxhdy, int32_t dxmdy, int32_t dxldy)
{
    struct dps_model *m = &state[wid].dps;
    const int32_t sc_xh_raw = state[wid].clip.xh, sc_xl_raw = state[wid].clip.xl;
    const int32_t cx1 = (sc_xh_raw + 3) >> 2, cx2 = (sc_xl_raw + 3) >> 2;
    const int32_t clipy1 = state[wid].clip.yh >> 2, clipy2 = (state[wid].clip.yl + 3) >> 2;
    const int32_t sh = (state[wid].fb_size == PIXEL_SIZE_16BIT) ? 1 : 0;
    const int32_t ycur = yh & ~3, ylfar = yl | 3;
    int32_t yh_eff = yh, yl_eff = yl;
    int32_t xleft, xright, xleft_inc, xright_inc, startx = 0, endx = 0, k;
    int32_t rawl = 0, rawr = 0;
    int rawset = 0;
    uint32_t i;

    state[wid].dps.cap_on = 0;
    m->nrows = 0;
    if (yh_eff < state[wid].clip.yh) yh_eff = state[wid].clip.yh;
    if (yl_eff > state[wid].clip.yl) yl_eff = state[wid].clip.yl;
    if ((ycur >> 2) >= clipy2 && (ylfar >> 2) >= clipy2)
        return;
    if ((ycur >> 2) < clipy1 && (ylfar >> 2) < clipy1)
        return;

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

        valid_y = !(k < yh_eff || k >= yl_eff);
        if (state[wid].scfield && (state[wid].sckeepodd ^ (j & 1)))
            valid_y = 0;

        if (spix == 0)
        {
            startx = flip ? 0 : 0xfff;
            endx = flip ? 0xfff : 0;
            rawset = 0;
        }
        if (valid_y)
        {
            const int32_t drl = (flip ? xright_w : xleft_w) >> 16;
            const int32_t drr = (flip ? xleft_w : xright_w) >> 16;
            if (!rawset) { rawl = drl; rawr = drr; rawset = 1; }
            else { if (drl < rawl) rawl = drl; if (drr > rawr) rawr = drr; }
            if (flip) { if (xstart > startx) startx = xstart; if (xend < endx) endx = xend; }
            else      { if (xstart < startx) startx = xstart; if (xend > endx) endx = xend; }
        }
        if (spix == 3 && j >= 0 && j < 1024)
        {
            struct span *sp = &state[wid].span[j];
            sp->dps_cap = 0;
            sp->dps_seed_n = 0;
            if (m->nrows < RDP_DPS_ROWS)
            {
                const int32_t exlo = startx < endx ? startx : endx;
                const int32_t exhi = startx < endx ? endx : startx;
                const int32_t px0 = exlo < cx1 ? cx1 : exlo;
                const int32_t px1 = exhi >= cx2 ? cx2 - 1 : exhi;
                const int yvis = j >= clipy1 && j < clipy2
                    && !(state[wid].scfield && (state[wid].sckeepodd ^ (j & 1)));
                if (yvis && rawset && rawr >= cx1 && rawl < cx2 && px0 <= px1)
                {
                    m->row_x0[m->nrows] = (int16_t)(px0 >> sh);
                    m->row_x1[m->nrows] = (int16_t)(px1 >> sh);
                    m->row_span[m->nrows] = (int16_t)j;
                    m->nrows++;
                }
            }
        }
        xright += xright_inc;
        xleft += xleft_inc;
    }

    /* a primitive whose rows all consume nothing leaves the model - and
     * any earlier draw's pending image - untouched, like the buffer */
    if (m->nrows == 0)
        return;
    memset(m->sched, 0, sizeof(m->sched));
    memset(m->val_set, 0, sizeof(m->val_set));
    memset(m->seed_set, 0, sizeof(m->seed_set));
    m->total = dps_walk(wid, 0);
    m->base = (m->total > RDP_DPS_WIN - 4u) ? ((m->total - (RDP_DPS_WIN - 4u)) & ~3u) : 0;
    dps_walk(wid, 1);
    m->valid = 1;
    m->cap_on = 1;
    (void)i;
}

/* Capture a scheduled pixel's write-stage word image; upstream of the
 * z, blend and coverage write gates, like the hardware buffer. */
static void dps_capture(uint32_t wid, const struct span *sp, int32_t x, uint32_t cvg)
{
    struct dps_model *m = &state[wid].dps;
    const uint32_t r = (uint32_t)state[wid].pixel_color.r & 0xff;
    const uint32_t g = (uint32_t)state[wid].pixel_color.g & 0xff;
    const uint32_t b = (uint32_t)state[wid].pixel_color.b & 0xff;
    uint32_t word, k;
    int32_t c = x;

    if (state[wid].fb_size == PIXEL_SIZE_16BIT)
    {
        word = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | ((((cvg - 1u) & 7u) >> 2) & 1u);
        c = x >> 1;
        word <<= (x & 1) ? 0u : 16u;
    }
    else
        word = (r << 24) | (g << 16) | (b << 8) | (((cvg - 1u) & 7u) << 5);

    if (c >= sp->dps_cap_x0 && c <= sp->dps_cap_x1)
    {
        const uint32_t i = (uint32_t)(c + sp->dps_voff);
        if (i < RDP_DPS_WIN)
        {
            m->val[i] = (state[wid].fb_size == PIXEL_SIZE_16BIT && m->val_set[i]) ? (m->val[i] | word) : word;
            m->val_set[i] = 1;
        }
    }
    for (k = 0; k < sp->dps_seed_n; k++)
    {
        if (sp->dps_seed_x[k] == c)
        {
            const uint32_t sk = sp->dps_seed_k[k];
            m->seed_val[sk] = (state[wid].fb_size == PIXEL_SIZE_16BIT && m->seed_set[sk]) ? (m->seed_val[sk] | word) : word;
            m->seed_set[sk] = 1;
        }
    }
}

/* Compose the post-draw CPU-visible window over the host's stored words.
 * Words 4g and 4g+1 carry the two slots of group g, 4g+2 the hidden-bit
 * image - one nibble per slot from the halfword low bits - and 4g+3 reads
 * zero. The workers must be idle. Returns 0 when no draw is pending. */
static int dps_take(uint32_t workers, uint32_t words[32])
{
    struct dps_model *m = &state[0].dps;
    uint32_t res[4], ring[16], ring_ok = 0, i, k, g, h, wk;
    uint8_t res_ok[4];

    if (!m->valid)
        return 0;

    /* every worker scheduled the same window; gather what each captured */
    for (wk = 1; wk < workers; wk++)
    {
        struct dps_model *o = &state[wk].dps;
        for (i = 0; i < RDP_DPS_WIN; i++)
            if (o->val_set[i])
            {
                m->val[i] = m->val_set[i] ? (m->val[i] | o->val[i]) : o->val[i];
                m->val_set[i] = 1;
            }
        for (k = 0; k < 4; k++)
            if (o->seed_set[k])
            {
                m->seed_val[k] = m->seed_set[k] ? (m->seed_val[k] | o->seed_val[k]) : o->seed_val[k];
                m->seed_set[k] = 1;
            }
        o->valid = 0;
    }

    for (k = 0; k < 4; k++)
    {
        res[k] = m->seed_val[k];
        res_ok[k] = m->seed_set[k];
    }
    for (i = 0; i < RDP_DPS_WIN && m->base + i < m->total; i++)
    {
        const uint32_t pos = m->base + i;
        uint32_t v = 0;
        int have = 0;
        if (m->sched[i] == 1 && m->val_set[i])
        {
            v = m->val[i];
            res[pos & 3u] = v;
            res_ok[pos & 3u] = 1;
            have = 1;
        }
        else if (m->sched[i] == 2 && res_ok[pos & 3u])
        {
            v = res[pos & 3u];
            have = 1;
        }
        if (have && !((pos >> 4) & 1u))
        {
            ring[pos & 15u] = v;
            ring_ok |= 1u << (pos & 15u);
        }
    }
    if (m->pend_alive && m->pend_pos1 != 0)
    {
        const uint32_t pos = m->pend_pos1 - 1u;
        if (res_ok[pos & 3u] && !((pos >> 4) & 1u))
        {
            ring[pos & 15u] = res[pos & 3u];
            ring_ok |= 1u << (pos & 15u);
        }
    }
    for (g = 0; g < 8; g++)
    {
        uint32_t hid = words[4 * g + 2] & 0xffu;
        for (h = 0; h < 2; h++)
        {
            const uint32_t sl = 2 * g + h;
            uint32_t nib;
            if (!(ring_ok & (1u << sl)))
                continue;
            words[4 * g + h] = ring[sl];
            nib = ((((ring[sl] >> 16) & 1u) * 3u) << 2) | ((ring[sl] & 1u) * 3u);
            hid = h ? ((hid & 0xf0u) | nib) : ((hid & 0x0fu) | (nib << 4));
        }
        words[4 * g + 2] = hid;
        words[4 * g + 3] = 0;
    }
    m->valid = 0;
    return 1;
}
