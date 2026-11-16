/*
 * csi_gain.c - receiver gain per CSI packet, for logging only. See
 * csi_gain.h for where the two bytes come from and why nothing here has
 * been confirmed on silicon.
 */
#include "csi_gain.h"

#include <string.h>

bool csi_gain_read(const void *rx_ctrl, size_t rx_len, uint8_t *agc, int8_t *fft)
{
    if (!rx_ctrl || rx_len <= CSI_GAIN_AGC_BYTE || !agc || !fft) {
        return false;
    }
    const uint8_t *b = (const uint8_t *)rx_ctrl;
    *agc = b[CSI_GAIN_AGC_BYTE];
    *fft = (int8_t)b[CSI_GAIN_FFT_BYTE];
    return true;
}

void csi_gain_reset(csi_gain_track_t *t)
{
    memset(t, 0, sizeof(*t));
}

void csi_gain_push(csi_gain_track_t *t, uint8_t agc, int8_t fft)
{
    if (t->ever && (agc != t->agc || fft != t->fft) && t->changes < UINT16_MAX) {
        t->changes++;
    }
    if (!t->have) {
        t->agc_lo = agc;
        t->agc_hi = agc;
    } else {
        if (agc < t->agc_lo) t->agc_lo = agc;
        if (agc > t->agc_hi) t->agc_hi = agc;
    }
    t->agc  = agc;
    t->fft  = fft;
    t->have = true;
    t->ever = true;
    if (t->packets < UINT32_MAX) t->packets++;
}

bool csi_gain_take(csi_gain_track_t *t, csi_gain_track_t *out)
{
    *out       = *t;
    t->have    = false;
    t->changes = 0;
    t->packets = 0;
    return out->have;
}
