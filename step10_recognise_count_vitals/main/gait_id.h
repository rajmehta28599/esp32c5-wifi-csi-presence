/*
 * gait_id.h - motion-signature features for recognising WHICH enrolled
 *             person is walking (EXPERIMENTAL). Classification itself is
 *             csi_ncc.h; this module only turns packets into feature
 *             vectors.
 *
 * WHAT THIS CAN AND CANNOT DO
 *   The way a body perturbs the channel while walking - how fast (the
 *   spread of fluctuation frequencies), with what cadence (step rate), and
 *   which subcarriers it disturbs (body size and the path taken) - differs
 *   between people. A published ESP32 result is ~95% for 2 enrolled
 *   people and ~74% for 3 (Gaiba 2024, controlled room; abstract only, so
 *   the number of links, whether subjects walked and the classifier are
 *   unverified - docs/REFERENCES.md). Treat that as the ceiling here, for
 *   one person at a time, in the room and placement they enrolled in, and
 *   re-enrolled when anything moves - Step 10's own requirements. It cannot
 *   recognise a still person, a stranger (only "unknown"), or anyone while
 *   2+ people move.
 *
 * WINDOWS
 *   Samples are the CSI_NGROUPS group signals from dsp_group_dev(), one per
 *   packet (no resampling: frequencies are derived from the measured packet
 *   rate, so enrolment and recognition must run at the same ping rate).
 *   A window is GAIT_NWIN packets, a new one every GAIT_HOP packets. A
 *   window produces features only if at least GAIT_MIN_MOVING of its
 *   packets had moving == true.
 *
 * FEATURES (GAIT_NFEAT = 18), in this order:
 *   Let x(t) be the projection of the 8 group signals onto the top
 *   eigenvector of their 8x8 covariance over the window (first principal
 *   component, found by power iteration - dsp_top_eig; if the top two
 *   eigenvalues nearly tie it is a deterministic mix of the two leading
 *   directions, see csi_dsp.h), detrended and Hann-windowed; P(f) its
 *   GAIT_NWIN-point power spectrum, f in Hz using rate_hz.
 *   [0..5]  fraction of P(f) power in [0.5,1) [1,2) [2,4) [4,8) [8,16)
 *           [16, fs/2] Hz over total power in [0.5, fs/2]. Bands above
 *           fs/2 are 0. Sums to 1 (all 0 if there is no power).
 *   [6]     spectral centroid of P over [0.5, fs/2], Hz.
 *   [7]     cadence, Hz: 1 / lag of the highest local maximum of the
 *           autocorrelation of the detrended (un-windowed) x(t) for lags in
 *           [0.4 s, 1.4 s]. Computed DIRECTLY in the time domain (linear,
 *           biased: sum over the overlap, no 1/(N-lag)), NOT via a
 *           256-point FFT - at 100 Hz a 1.4 s lag is 140 samples, past the
 *           128 a circular 256-point autocorrelation can represent. At
 *           rate_hz <= 60 it runs on x itself: N = GAIT_NWIN samples at
 *           rate_hz. Above 60 Hz it runs on the 2:1 pair average
 *           y(k) = (x(2k) + x(2k+1)) / 2: N = GAIT_NWIN/2 samples at
 *           rate_hz/2, and the lag found is converted to seconds at that
 *           rate - same definition, a quarter of the multiply-adds. Either
 *           way lags are clamped to [2, N/2] samples, i.e. at most half the
 *           window (1.28 s at 100 Hz, as before the pair average). 0 if no
 *           local maximum in range.
 *   [8]     that autocorrelation value normalised by lag 0 of the same
 *           signal, in [-1, 1] (0 when [7] is 0). Above 60 Hz the pair
 *           average halves white noise first, so for a noisy x this can
 *           read higher than it would on the full-rate x.
 *   [9]     log10(variance of x over the window + 1e-6).
 *   [10..17] share of total variance in each of the 8 groups (diagonal of
 *           the covariance / its trace). Sums to 1.
 *
 * COST per window: 8x8 covariance (36 x 256 MACs), one power iteration
 * for the top eigenvector, one projection, one 256-point FFT and a direct
 * autocorrelation over ~50 lags - ~58k soft-float operations at 100 Hz,
 * ~73k at 50 Hz (counted on the host, gait_id.c has the breakdown),
 * roughly 12-32 ms at 240 MHz by estimate - NOT measured on the C5 -
 * once every GAIT_HOP packets. MEMORY: 8 x 256 x 4 = 8 KB ring + 3 KB
 * work (x, re, im).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "csi_dsp.h"

#define GAIT_NWIN        256
#define GAIT_HOP         128
#define GAIT_MIN_MOVING  0.8f
#define GAIT_NFEAT       18

typedef struct {
    float    rate_hz;
    float    ring[CSI_NGROUPS][GAIT_NWIN];
    uint8_t  moving[GAIT_NWIN];
    int      head;                 /* next write index                    */
    int      count;                /* valid samples, <= GAIT_NWIN         */
    int      since_window;         /* packets since the last window       */
    float    x[GAIT_NWIN];         /* work: principal-component signal    */
    float    re[GAIT_NWIN];
    float    im[GAIT_NWIN];
} gait_t;

void gait_init(gait_t *g, float rate_hz);
void gait_set_rate(gait_t *g, float rate_hz);
void gait_reset(gait_t *g);

/* Once per accepted packet. Returns true when a window completed AND was
 * moving enough; feat then holds GAIT_NFEAT features. */
bool gait_push(gait_t *g, const float grp[CSI_NGROUPS], bool moving,
               float feat[GAIT_NFEAT]);
