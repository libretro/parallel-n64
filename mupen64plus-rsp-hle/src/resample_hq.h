#ifndef RESAMPLE_HQ_H
#define RESAMPLE_HQ_H

#include <stdint.h>

/* Tap counts the quality setting can select. */
enum {
    RESAMPLE_HQ_OFF = 0,
    RESAMPLE_HQ_16  = 16,
    RESAMPLE_HQ_32  = 32,
    RESAMPLE_HQ_64  = 64
};

/* Select the kernel.  taps must be one of the enum values; anything else
 * (including 0) turns the enhancement off.  Building the tap bank is
 * deferred to the first resample, so calling this is cheap. */
void resample_hq_set_quality(int taps);

/* Non-zero when an enhanced kernel is selected. */
int  resample_hq_enabled(void);

/* Number of taps the current kernel uses. */
int  resample_hq_taps_count(void);

/* As resample_hq_taps, but never wider than max_taps.  For callers whose
 * input window is too short to centre the full kernel in. */
const int16_t* resample_hq_taps_capped(uint32_t pitch, uint32_t pitch_accu,
                                       int max_taps);

/* Taps for this pitch and phase, or NULL if the enhancement is off or the
 * tap bank could not be allocated. */
const int16_t* resample_hq_taps(uint32_t pitch, uint32_t pitch_accu);

/* Release the tap bank. */
void resample_hq_release(void);

#endif
