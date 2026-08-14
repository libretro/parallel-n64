/* Higher-quality voice interpolation for the HLE audio microcodes.
 *
 * The microcode resamples each voice with a four-tap FIR chosen from a
 * 64-phase table.  Four taps cannot separate the image of a pitched-up
 * voice from the voice itself, so every playback rate above unity folds
 * energy back into the audible band - measured against a tone above the
 * cutoff the microcode's kernel rejects the alias by only 4 to 8 dB.
 * That is the grain on high-pitched samples.
 *
 * This is an opt-in replacement.  It emits exactly as many output samples
 * as the microcode would, reads the same input stream and advances the
 * same pitch accumulator, so the command list, the DMEM buffers and every
 * length the game computed are untouched: only the interpolated values
 * differ.  It is therefore NOT bit-exact against the interpreter and must
 * never be enabled on the accuracy path.
 *
 * Kernel: a Blackman-windowed sinc at 16, 32 or 64 taps.  Playback above
 * unity rate needs the cutoff pulled down to 1/ratio to keep the image
 * out of the passband, so the bank holds a set of cutoff bands and the
 * resampler picks one from the pitch.
 *
 * Quantisation.  Three approximations sit between this and an ideal
 * resampler, and each is placed so it lands below the 16-bit output floor
 * rather than above it:
 *
 *   taps    Q15 is not enough on its own: rounding each tap independently
 *           leaves the row summing to something other than unity, which is
 *           a per-phase gain error - broadband noise on any moving signal.
 *           The rows are rounded and then the largest tap is nudged so the
 *           row sums to exactly 1.0, making DC gain exact at every phase.
 *
 *   phase   the fractional position is quantised to HQ_PHASES steps rather
 *           than the accumulator's full 16 bits.  The residual is a timing
 *           jitter of up to half a step; at 256 steps that is below the
 *           output LSB for everything but full-scale content near Nyquist.
 *
 *   accum   64 taps of 16-bit input against Q15 coefficients reach 2^36,
 *           well past int32, so the accumulator is 64-bit.  The output is
 *           rounded half-up rather than truncated, which keeps the error
 *           zero-mean instead of biasing every sample downward.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

#include "resample_hq.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define HQ_PHASES  256
#define HQ_BANDS   16

static int16_t *hq_lut;         /* [band][phase][tap], Q15 */
static int      hq_taps;        /* taps in the built bank, 0 when none */
static int      hq_want;        /* taps the option asks for */

/* Band b has cutoff 1 / (1 + b/4): unity down to 1/4.75, which spans every
 * playback rate a voice realistically uses.  Quarter steps keep the band
 * from being much narrower than the rate needs, which would throw away
 * treble the material still has. */
static double hq_band_cutoff(int b)
{
    return 1.0 / (1.0 + 0.25 * (double)b);
}

static double hq_sinc(double x)
{
    if (x > -1e-12 && x < 1e-12)
        return 1.0;
    x *= M_PI;
    return sin(x) / x;
}

static void hq_free(void)
{
    free(hq_lut);
    hq_lut  = NULL;
    hq_taps = 0;
}

static int hq_build(int taps)
{
    const size_t n = (size_t)HQ_BANDS * HQ_PHASES * (size_t)taps;
    double *row;
    int b, p, t;

    hq_free();

    hq_lut = (int16_t*)malloc(n * sizeof(int16_t));
    if (hq_lut == NULL)
        return 0;

    row = (double*)malloc((size_t)taps * sizeof(double));
    if (row == NULL) {
        hq_free();
        return 0;
    }

    for (b = 0; b < HQ_BANDS; ++b) {
        const double fc = hq_band_cutoff(b);

        for (p = 0; p < HQ_PHASES; ++p) {
            const double frac = (double)p / (double)HQ_PHASES;
            int16_t *dst = hq_lut + (((size_t)b * HQ_PHASES + p) * (size_t)taps);
            double  sum  = 0.0;
            long    isum = 0;
            int     big  = 0;

            for (t = 0; t < taps; ++t) {
                const double x = (double)(t - (taps / 2 - 1)) - frac;
                const double w = 0.42
                    - 0.50 * cos(2.0 * M_PI * ((double)t + 0.5) / (double)taps)
                    + 0.08 * cos(4.0 * M_PI * ((double)t + 0.5) / (double)taps);

                row[t] = fc * hq_sinc(fc * x) * w;
                sum   += row[t];
            }

            /* scale to unity DC, round to Q15, and track the largest tap */
            for (t = 0; t < taps; ++t) {
                double v = (sum != 0.0) ? (row[t] / sum) : 0.0;
                long   q = lround(v * 32768.0);

                if (q >  32767) q =  32767;
                if (q < -32768) q = -32768;
                dst[t] = (int16_t)q;
                isum  += q;

                if (dst[t] > dst[big] || -dst[t] > dst[big])
                    big = t;
            }

            /* Nudge the largest tap so the row sums to exactly 1.0 in Q15.
             * Spreading the residue over the small taps would perturb the
             * stopband; the largest tap absorbs it without moving the
             * response measurably. */
            {
                long fix = (long)dst[big] + (32768 - isum);

                if (fix >  32767) fix =  32767;
                if (fix < -32768) fix = -32768;
                dst[big] = (int16_t)fix;
            }
        }
    }

    free(row);
    hq_taps = taps;
    return 1;
}

void resample_hq_set_quality(int taps)
{
    if (taps != RESAMPLE_HQ_16 && taps != RESAMPLE_HQ_32 && taps != RESAMPLE_HQ_64)
        taps = RESAMPLE_HQ_OFF;

    if (taps == hq_want)
        return;

    hq_want = taps;
    hq_free();               /* rebuilt lazily on the next resample */
}

int resample_hq_enabled(void)
{
    return hq_want != RESAMPLE_HQ_OFF;
}

int resample_hq_taps_count(void)
{
    return hq_taps;
}

void resample_hq_release(void)
{
    hq_free();
    hq_want = RESAMPLE_HQ_OFF;
}

const int16_t* resample_hq_taps(uint32_t pitch, uint32_t pitch_accu)
{
    unsigned band;
    unsigned phase;

    if (hq_want == RESAMPLE_HQ_OFF)
        return NULL;

    if (hq_taps != hq_want && !hq_build(hq_want)) {
        hq_want = RESAMPLE_HQ_OFF;    /* out of memory: fall back to exact */
        return NULL;
    }

    /* Smallest b with 1 + b/4 >= ratio, i.e. ceil(4*(ratio-1)).  Rounding
     * down would leave the cutoff above the rate and let the image
     * straight back through. */
    if (pitch <= 0x10000u)
        band = 0;
    else {
        band = (unsigned)((((pitch - 0x10000u) * 4u) + 0xffffu) >> 16);
        if (band >= HQ_BANDS)
            band = HQ_BANDS - 1;
    }

    phase = (pitch_accu >> 8) & (HQ_PHASES - 1);

    return hq_lut + (((size_t)band * HQ_PHASES + phase) * (size_t)hq_taps);
}

const int16_t* resample_hq_taps_capped(uint32_t pitch, uint32_t pitch_accu,
                                       int max_taps)
{
    int want = hq_want;

    if (want == RESAMPLE_HQ_OFF)
        return NULL;
    if (want > max_taps)
        want = max_taps;

    if (hq_taps != want && !hq_build(want)) {
        hq_want = RESAMPLE_HQ_OFF;
        return NULL;
    }

    return resample_hq_taps(pitch, pitch_accu);
}
