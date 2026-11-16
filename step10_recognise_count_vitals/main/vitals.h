/*
 * vitals.h - breathing rate and (EXPERIMENTAL) heart rate from the CSI
 *            amplitude of a person who is sitting or lying still.
 *
 * WHAT THIS CAN AND CANNOT DO
 *   Breathing moves the chest 4-12 mm; that modulates the amplitude of the
 *   subcarriers whose paths cross the chest, at 0.1-0.5 Hz. On one antenna
 *   this is measurable for a still person in the same room - it is the
 *   DeMan (JSAC 2015) idea, see docs/REFERENCES.md section 3.2.
 *   The heartbeat moves the chest ~0.1-0.5 mm, 10-50x less, in a band
 *   (0.8-2.0 Hz) that breathing harmonics also land in. The heart-rate
 *   output is therefore EXPERIMENTAL: reported only with a clear, confident
 *   peak, only while breathing is also detected, never while anyone moves,
 *   and unvalidated against a reference until you do it (README, Step 10).
 *
 * PIPELINE
 *   1. Per packet: amplitudes of VIT_NCH subcarriers spread across the
 *      selected set are accumulated into 100 ms TIME bins (not packet
 *      counts - ping replies jitter and drop), giving a uniform 10 Hz
 *      series per channel in a VIT_RING-sample ring. Bins with no packet
 *      are filled by a straight line from the last bin to the packet that
 *      ends the gap (D-175). A packet more than VIT_MAX_GAP_US + one bin
 *      (VIT_BIN_US) after the start of the open bin - a packet gap of
 *      1.0-1.1 s or more, depending on where in its bin the last packet
 *      fell - or any motion (still == false), drops the buffer and starts
 *      over; a shorter gap is filled.
 *   2. Every VIT_HOP_BINS bins, once at least cfg.min_s of still data
 *      exist, an analysis runs over the newest min(available, window_s)
 *      seconds: per channel detrend -> Hann -> zero-padded VIT_NFFT-point
 *      FFT -> power spectrum. ONE channel per vitals_push() call, so no
 *      packet ever pays for more than one FFT (~6-10 ms soft-float).
 *   3. Channel spectra are combined per band after normalising each to unit
 *      power in that band, all channels with EQUAL weight (weighting each
 *      by its own peak-to-median ratio squared made pure noise pass for a
 *      breath in ~9% of analyses - see band_accumulate in vitals.c). A
 *      power sum is used, not an amplitude sum, so channels in opposite
 *      phase cannot cancel.
 *   4. Breathing: peak of the combined 0.10-0.50 Hz spectrum, parabolic
 *      interpolation, confidence = peak / median of the band.
 *   5. Heart: if breathing is confident, bins within VIT_NOTCH_HZ of 2..8 x
 *      the breathing frequency are excluded (breathing harmonics sit right
 *      in the heart band), then the same peak / median test on
 *      0.80-2.00 Hz. A maximum on a band edge, or one lower than an
 *      excluded neighbour (the shoulder of a notched harmonic), is not
 *      reported. The reported heart rate is the median of the last
 *      VIT_HR_MEDIAN confident estimates within the current still episode.
 *
 * MEMORY: VIT_NCH * VIT_RING * 4 = 24 KB ring + 4 KB FFT work + 2 KB of
 * spectra (~30 KB; 30,984 bytes in the firmware's linker map), all inside
 * vitals_t - declare it static, never on a stack.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define VIT_FS_HZ        10            /* uniform rate after time-binning   */
#define VIT_BIN_US       100000        /* 1 / VIT_FS_HZ                     */
#define VIT_NCH          12            /* subcarriers tracked               */
#define VIT_RING         512           /* samples per channel = 51.2 s      */
#define VIT_NFFT         512           /* bin width 10/512 = 0.0195 Hz      */
#define VIT_HOP_BINS     20            /* analyse every 2 s                 */
#define VIT_MAX_GAP_US   1000000       /* longer gap -> restart             */
#define VIT_HR_MEDIAN    5

#define VIT_BR_LO_HZ     0.10f         /*  6 breaths/min */
#define VIT_BR_HI_HZ     0.50f         /* 30 breaths/min */
#define VIT_HR_LO_HZ     0.80f         /*  48 beats/min  */
#define VIT_HR_HI_HZ     2.00f         /* 120 beats/min  */
#define VIT_NOTCH_HZ     0.05f
#define VIT_NOTCH_KMAX   8

typedef struct {
    int   window_s;      /* analysis window, clamped to [16, VIT_RING/10]   */
    int   min_s;         /* still data needed before the first estimate,
                            clamped to [10, window_s]                        */
    float br_conf_min;   /* breathing peak/median needed to report           */
    float hr_conf_min;   /* heart peak/median needed to report               */
} vitals_cfg_t;

typedef enum {
    VIT_OFF = 0,         /* not configured (no baseline yet)                 */
    VIT_MOVING,          /* motion - buffer dropped, waiting for stillness   */
    VIT_SETTLING,        /* still, collecting data up to min_s               */
    VIT_NO_SIGNAL,       /* analysed, no confident breathing peak            */
    VIT_OK,              /* confident breathing (heart may or may not be)    */
} vitals_state_t;

typedef struct {
    vitals_state_t state;
    bool     br_valid;
    float    br_bpm;     /* breaths per minute                               */
    float    br_conf;    /* peak / median of the combined breathing band     */
    bool     hr_valid;   /* EXPERIMENTAL                                     */
    float    hr_bpm;     /* beats per minute, median of recent estimates     */
    float    hr_conf;    /* peak / median of the combined heart band         */
    float    still_s;    /* seconds of continuous still data buffered        */
    uint32_t updates;    /* +1 per completed analysis                        */
} vitals_out_t;

typedef struct {
    vitals_cfg_t cfg;
    bool     configured;
    int      nch;                        /* channels in use, <= VIT_NCH     */
    int16_t  chan[VIT_NCH];              /* subcarrier index per channel    */

    /* time binning */
    bool     have_bin;
    int64_t  bin_start_us;
    float    bin_sum[VIT_NCH];
    int      bin_n;
    float    last_val[VIT_NCH];

    /* ring of finished bins */
    float    ring[VIT_NCH][VIT_RING];
    int      ring_head;                  /* next write index                */
    int      ring_count;                 /* valid samples, <= VIT_RING      */
    int      bins_since_analysis;

    /* incremental analysis: one channel per push */
    bool     analysing;
    int      an_ch;                      /* next channel to transform       */
    int      an_len;                     /* samples in this analysis        */
    float    re[VIT_NFFT];
    float    im[VIT_NFFT];
    int      br_lo, br_hi, hr_lo, hr_hi; /* FFT bin ranges, inclusive       */
    float    br_acc[VIT_NFFT / 2];       /* combined spectra (band bins used) */
    float    hr_acc[VIT_NFFT / 2];

    float    hr_hist[VIT_HR_MEDIAN];
    int      hr_hist_n;

    vitals_out_t out;
} vitals_t;

void vitals_init(vitals_t *v, const vitals_cfg_t *cfg);

/* Pick the VIT_NCH tracked subcarriers, spread evenly across selected[]
 * (all of them if fewer are selected), and reset. Call after every
 * baseline load or calibration. */
void vitals_configure(vitals_t *v, const uint8_t *selected, int nsub);

/* Drop all buffered data and estimates (motion, link gap, recalibration).
 * Keeps the configuration. State becomes VIT_MOVING if configured. */
void vitals_reset(vitals_t *v);

/* Once per accepted packet.
 *   t_us  : packet timestamp, monotonic microseconds.
 *   na    : normalised amplitudes of ALL nsub subcarriers (the tracked
 *           indices are read from it).
 *   still : false while anyone is moving; any false resets the buffer.
 * Returns true when this call completed an analysis (vitals_get() changed). */
bool vitals_push(vitals_t *v, int64_t t_us, const float *na, bool still);

const vitals_out_t *vitals_get(const vitals_t *v);

const char *vitals_state_name(vitals_state_t s);
