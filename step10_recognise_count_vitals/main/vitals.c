/*
 * vitals.c - see vitals.h. No FPU on the C5: float only, no per-sample
 * trigonometry (dsp_hann and dsp_fft use rotation recurrences), and at most
 * one FFT per vitals_push() call.
 *
 * Nothing here has been validated against a real chest yet. The host tests
 * (test/test_vitals.c) only show that the arithmetic recovers known rates
 * from SYNTHETIC amplitudes; see the README, Step 10, for the reference-
 * device procedure that would show whether it works on a person.
 */

#include "vitals.h"
#include "csi_dsp.h"

#include <math.h>
#include <string.h>

/* Width of one FFT bin in Hz: 10 / 512 = 0.01953125 (exact in float). */
#define VIT_BIN_HZ  ((float)VIT_FS_HZ / (float)VIT_NFFT)

static const char *const s_state_names[] = {
    "off", "moving", "settling", "no-signal", "ok",
};

/* ------------------------------------------------------------------ */

void vitals_init(vitals_t *v, const vitals_cfg_t *cfg)
{
    vitals_cfg_t c;

    memset(v, 0, sizeof(*v));

    if (cfg) {
        c = *cfg;
    } else {
        /* the Kconfig defaults */
        c.window_s    = 30;
        c.min_s       = 20;
        c.br_conf_min = 4.0f;
        c.hr_conf_min = 5.0f;
    }

    /* A window shorter than 16 s holds too few breaths (and resolves only
     * 0.06 Hz) to tell one rate from its neighbour; longer than the ring
     * is impossible. */
    if (c.window_s < 16) c.window_s = 16;
    if (c.window_s > VIT_RING / VIT_FS_HZ) c.window_s = VIT_RING / VIT_FS_HZ;
    if (c.min_s < 10) c.min_s = 10;
    if (c.min_s > c.window_s) c.min_s = c.window_s;
    /* peak / median is >= 1 by construction, so a threshold below 1 would
     * silently mean "always report". The negated test also catches NaN. */
    if (!(c.br_conf_min >= 1.0f)) c.br_conf_min = 1.0f;
    if (!(c.hr_conf_min >= 1.0f)) c.hr_conf_min = 1.0f;
    v->cfg = c;

    /* Inclusive FFT-bin ranges that lie wholly inside each band:
     * breathing 6..25 (0.117-0.488 Hz), heart 41..102 (0.801-1.992 Hz). */
    v->br_lo = (int)ceilf(VIT_BR_LO_HZ / VIT_BIN_HZ);
    v->br_hi = (int)floorf(VIT_BR_HI_HZ / VIT_BIN_HZ);
    v->hr_lo = (int)ceilf(VIT_HR_LO_HZ / VIT_BIN_HZ);
    v->hr_hi = (int)floorf(VIT_HR_HI_HZ / VIT_BIN_HZ);

    v->configured = false;
    v->out.state  = VIT_OFF;
}

void vitals_configure(vitals_t *v, const uint8_t *selected, int nsub)
{
    int nsel = 0;
    if (selected) {
        for (int k = 0; k < nsub; k++) {
            if (selected[k]) nsel++;
        }
    }
    int nch = (nsel < VIT_NCH) ? nsel : VIT_NCH;

    /* Channel i takes the selected subcarrier at ordinal
     * ((2i+1) * nsel) / (2 * nch): the centre of the i-th of nch equal
     * slices of the selected set. Spreading them out matters because
     * which subcarriers see the chest depends on the multipath, so
     * neighbours would mostly repeat each other. Since nsel >= nch the
     * ordinals are strictly increasing, so one pass assigns them all. */
    int i = 0, ord = 0;
    for (int k = 0; k < nsub && i < nch; k++) {
        if (!selected[k]) continue;
        if (ord == ((2 * i + 1) * nsel) / (2 * nch)) {
            v->chan[i++] = (int16_t)k;
        }
        ord++;
    }

    v->nch        = nch;
    v->configured = (nch > 0);
    vitals_reset(v);
}

void vitals_reset(vitals_t *v)
{
    v->have_bin     = false;
    v->bin_start_us = 0;
    v->bin_n        = 0;
    for (int c = 0; c < VIT_NCH; c++) {
        v->bin_sum[c] = 0.0f;
    }

    v->ring_head           = 0;
    v->ring_count          = 0;
    v->bins_since_analysis = 0;

    v->analysing = false;
    v->an_ch     = 0;
    v->an_len    = 0;

    /* The heart-rate median belongs to one still episode: after motion the
     * person may have changed posture, or be a different person. */
    v->hr_hist_n = 0;

    /* Never leave a rate from before the reset on display. updates keeps
     * counting so a caller can still detect "a new analysis happened". */
    uint32_t updates = v->out.updates;
    memset(&v->out, 0, sizeof(v->out));
    v->out.updates = updates;
    v->out.state   = v->configured ? VIT_MOVING : VIT_OFF;
}

/* ------------------------------------------------------------------ */

static void ring_push_bin(vitals_t *v, const float *val)
{
    for (int c = 0; c < v->nch; c++) {
        v->ring[c][v->ring_head] = val[c];
    }
    v->ring_head = (v->ring_head + 1) % VIT_RING;
    if (v->ring_count < VIT_RING) v->ring_count++;
    v->bins_since_analysis++;
}

static void bin_start_with(vitals_t *v, int64_t start_us, const float *na)
{
    v->bin_start_us = start_us;
    for (int c = 0; c < v->nch; c++) {
        v->bin_sum[c] = na[v->chan[c]];
    }
    v->bin_n = 1;
}

/* Squared magnitude into re[k] for the bins of one band. */
static void band_power(vitals_t *v, int lo, int hi)
{
    for (int k = lo; k <= hi; k++) {
        v->re[k] = v->re[k] * v->re[k] + v->im[k] * v->im[k];
    }
}

/* Add one channel's band spectrum (power in re[lo..hi]) to acc[lo..hi],
 * normalised to unit power in the band so that a channel with a large
 * amplitude swing cannot dominate just because it is loud. Powers are
 * added, never complex values, so two subcarriers moving in opposite
 * phase reinforce, not cancel.
 *
 * Every channel gets the SAME weight. The first design weighted each
 * channel by r^2, r = its own peak/median, so that channels showing a
 * clean periodicity would dominate. On noise that selects the luckiest
 * channel instead: one channel's peak/median over ~20 noise bins has a
 * median of 3.8 and a p99 of 14, so r^2 hands the most extreme noise
 * channel most of the weight and the "combined" spectrum is really that
 * one channel's. Measured on white noise: with r^2 weights 8.5% of 20000
 * simulated 30 s windows (and 41 of 400 analyses in test_vitals case 3)
 * passed the 4.0 breathing threshold, peak/median up to 40; with equal
 * weights none did (p99.9 2.4, max 3.0 - and 3.2 in the heart band).
 * A false breathing detection is not harmless: with
 * CSI_VITALS_HOLDS_PRESENCE it keeps an emptied room PRESENT. The price
 * is sensitivity: a chest seen by only 2-3 of the 12 channels is now
 * usually missed rather than sometimes found. */
static void band_accumulate(vitals_t *v, int lo, int hi, float *acc)
{
    float s = 0.0f;
    for (int k = lo; k <= hi; k++) {
        s += v->re[k];
    }
    if (!(s > 0.0f)) return;          /* a flat channel carries nothing */

    float inv = 1.0f / s;
    for (int k = lo; k <= hi; k++) {
        acc[k] += v->re[k] * inv;
    }
}

/* Transform one channel over the newest an_len samples. Taking the
 * newest samples at the time of THIS call (rather than pinning the window
 * when the analysis started) means channels analysed a few packets apart
 * see windows a bin or two apart. Only power spectra are combined, which
 * do not care about a shift that small, and it keeps the ring free to
 * wrap while an analysis is in progress. */
static void analyse_channel(vitals_t *v, int ch)
{
    const float *ring = v->ring[ch];
    int n     = v->an_len;
    int start = v->ring_head - n;
    if (start < 0) start += VIT_RING;

    for (int i = 0; i < n; i++) {
        int j = start + i;
        if (j >= VIT_RING) j -= VIT_RING;
        v->re[i] = ring[j];
    }
    /* Detrend first: the slow drift of a warming radio would otherwise
     * leak through the window into the lowest breathing bins. */
    dsp_detrend(v->re, n);
    dsp_hann(v->re, n);
    for (int i = n; i < VIT_NFFT; i++) {
        v->re[i] = 0.0f;
    }
    memset(v->im, 0, sizeof(v->im));
    dsp_fft(v->re, v->im, VIT_NFFT);

    band_power(v, v->br_lo, v->br_hi);
    band_power(v, v->hr_lo, v->hr_hi);
    band_accumulate(v, v->br_lo, v->br_hi, v->br_acc);
    band_accumulate(v, v->hr_lo, v->hr_hi, v->hr_acc);
}

/* True if frequency f (Hz) lies within VIT_NOTCH_HZ of a breathing
 * harmonic 2..VIT_NOTCH_KMAX times f_br. A 15 /min breather has
 * harmonics at 60, 75, 90, 105 /min - squarely on top of plausible heart
 * rates, and usually far stronger than the heartbeat itself. */
static bool is_notched(float f, float f_br)
{
    for (int h = 2; h <= VIT_NOTCH_KMAX; h++) {
        if (fabsf(f - (float)h * f_br) <= VIT_NOTCH_HZ) return true;
    }
    return false;
}

static void push_hr(vitals_t *v, float bpm)
{
    if (v->hr_hist_n < VIT_HR_MEDIAN) {
        v->hr_hist[v->hr_hist_n++] = bpm;
    } else {
        memmove(&v->hr_hist[0], &v->hr_hist[1],
                (VIT_HR_MEDIAN - 1) * sizeof(float));
        v->hr_hist[VIT_HR_MEDIAN - 1] = bpm;
    }
}

/* Heart-rate search over the combined heart band, given a confident
 * breathing frequency. Writes hr_conf / hr_valid / hr_bpm. */
static void finish_heart(vitals_t *v, float f_br)
{
    vitals_out_t *o  = &v->out;
    const float  *acc = v->hr_acc;
    int lo = v->hr_lo, hi = v->hr_hi;

    /* The harmonic bins are removed from BOTH the peak search and the
     * median: a strong harmonic left in the median would not change it
     * much, but left in the peak search it would be "the heart". The
     * surviving values are gathered into re[] (free after the last
     * channel) for the median. */
    int   m = 0, kp = -1;
    float peak = 0.0f;
    for (int k = lo; k <= hi; k++) {
        if (is_notched((float)k * VIT_BIN_HZ, f_br)) continue;
        v->re[m++] = acc[k];
        if (kp < 0 || acc[k] > peak) {
            peak = acc[k];
            kp   = k;
        }
    }
    if (m < 3 || !(peak > 0.0f)) return;

    float med = dsp_median(v->re, m, v->im);
    if (!(med > 0.0f)) return;
    o->hr_conf = peak / med;

    /* A maximum on the first or last band bin is most likely the skirt of
     * something outside the band (a strong 2x breathing harmonic below
     * 0.8 Hz, say), not a heart peak - there is no evidence it peaks
     * inside the band at all. */
    if (kp == lo || kp == hi) return;
    if (o->hr_conf < v->cfg.hr_conf_min) return;

    /* The same argument inside the band: a maximum whose EXCLUDED
     * neighbour is higher still is not a peak but the shoulder of the
     * harmonic that was notched out. VIT_NOTCH_HZ (0.05 Hz) is narrower
     * than a Hann main lobe (+/-2 FFT bins of the data: +/-0.10 Hz for a
     * 20 s window, +/-0.067 Hz for 30 s), so a strong harmonic spills
     * past its notch. Measured on synthetic breathing with harmonics
     * (2nd/3rd/4th at 0.5/0.3/0.2 of the fundamental) and NO heart, 30
     * seeds x 20 analyses: without this test 10-21% of analyses reported
     * a heart rate (12, 15 and 20 /min breathing); with it none did, in
     * 100 seeds at 8-25 /min either. The cost is a true heart rate within
     * a few bpm of a harmonic: 72 bpm under 15 /min breathing (5th
     * harmonic at 75) is withheld in ~8% of analyses instead of never.
     * Reporting nothing there is honest - a heartbeat that close to a
     * harmonic cannot be told from it. */
    if (acc[kp - 1] > acc[kp] || acc[kp + 1] > acc[kp]) return;

    /* Next to a notch the neighbour's value is the harmonic's, not the
     * heart's, so interpolating toward it would bias the estimate: report
     * the bin centre instead (+/- 0.59 /min). */
    float off = 0.0f;
    float fm1 = (float)(kp - 1) * VIT_BIN_HZ, fp1 = (float)(kp + 1) * VIT_BIN_HZ;
    if (!is_notched(fm1, f_br) && !is_notched(fp1, f_br)) {
        off = dsp_parabolic_offset(acc[kp - 1], acc[kp], acc[kp + 1]);
    }
    float bpm = 60.0f * ((float)kp + off) * VIT_BIN_HZ;

    /* One window's heart peak is noisy; the median of the last few
     * confident ones within this still episode is what gets reported. */
    push_hr(v, bpm);
    o->hr_bpm   = dsp_median(v->hr_hist, v->hr_hist_n, v->im);
    o->hr_valid = true;
}

static void finish_analysis(vitals_t *v)
{
    vitals_out_t *o  = &v->out;
    const float  *acc = v->br_acc;
    int lo = v->br_lo, hi = v->br_hi;

    /* Every analysis replaces all four numbers: a rate that is no longer
     * confident is withdrawn, not held. */
    o->br_valid = false;
    o->br_bpm   = 0.0f;
    o->br_conf  = 0.0f;
    o->hr_valid = false;
    o->hr_bpm   = 0.0f;
    o->hr_conf  = 0.0f;

    int   kp   = lo;
    float peak = acc[lo];
    for (int k = lo + 1; k <= hi; k++) {
        if (acc[k] > peak) {
            peak = acc[k];
            kp   = k;
        }
    }
    float med = dsp_median(&acc[lo], hi - lo + 1, v->im);

    float f_br = 0.0f;
    if (peak > 0.0f && med > 0.0f) {
        o->br_conf = peak / med;
        /* Peak on a band edge: leakage from outside the band (a slow drift
         * the detrend missed, or a sigh-scale motion) - not confident. */
        if (kp > lo && kp < hi && o->br_conf >= v->cfg.br_conf_min) {
            float off = dsp_parabolic_offset(acc[kp - 1], acc[kp], acc[kp + 1]);
            f_br        = ((float)kp + off) * VIT_BIN_HZ;
            o->br_bpm   = 60.0f * f_br;
            o->br_valid = true;
        }
    }

    /* Heart only on top of confident breathing: without a breathing
     * frequency the harmonics cannot be removed, and without a detectable
     * chest at all a 0.1 mm heartbeat certainly is not detectable. */
    if (o->br_valid) {
        finish_heart(v, f_br);
    }

    o->state = o->br_valid ? VIT_OK : VIT_NO_SIGNAL;
    o->updates++;
}

/* ------------------------------------------------------------------ */

bool vitals_push(vitals_t *v, int64_t t_us, const float *na, bool still)
{
    if (!v->configured || !na) return false;

    if (!still) {
        vitals_reset(v);
        return false;
    }

    /* A gap longer than VIT_MAX_GAP_US (more than a second of held values
     * would be invented data) or a clock that ran backwards: start over.
     * The test is on the bin start, so the packet gap it implies is at
     * least VIT_MAX_GAP_US. */
    if (v->have_bin &&
        (t_us < v->bin_start_us ||
         t_us - v->bin_start_us > (int64_t)VIT_MAX_GAP_US + VIT_BIN_US)) {
        vitals_reset(v);
    }

    if (!v->have_bin) {
        bin_start_with(v, t_us, na);
        v->have_bin  = true;
        v->out.state = VIT_SETTLING;
    } else if (t_us - v->bin_start_us >= VIT_BIN_US) {
        /* This packet lies nb >= 1 bins further on: close the current bin,
         * fill every bin nobody landed in by a straight line from the closed
         * bin's mean to this packet's value, and open the bin that contains
         * t (D-175, superseding D-85). Repeating the last value instead
         * froze a breathing swing into a step whose spectrum leaked into
         * the heart band (~50 bpm phantoms, test_vitals (12b)); the line
         * makes them much rarer there, it does not remove them. The gap
         * test above caps a fill at VIT_MAX_GAP_US / VIT_BIN_US + 1 bins,
         * so a line never spans a reset. */
        int   nb  = (int)((t_us - v->bin_start_us) / VIT_BIN_US);
        float inv = 1.0f / (float)v->bin_n;
        for (int c = 0; c < v->nch; c++) {
            v->last_val[c] = v->bin_sum[c] * inv;
        }
        ring_push_bin(v, v->last_val);
        for (int b = 1; b < nb; b++) {
            float fr = (float)b / (float)nb;
            float tmp[VIT_NCH] = { 0 };
            for (int c = 0; c < v->nch; c++) {
                tmp[c] = v->last_val[c] + fr * (na[v->chan[c]] - v->last_val[c]);
            }
            ring_push_bin(v, tmp);
        }
        bin_start_with(v, v->bin_start_us + (int64_t)nb * VIT_BIN_US, na);

        /* Before the first analysis bins_since_analysis == ring_count
         * (both restart at vitals_reset, and min_s * 10 < VIT_RING), so
         * the first time min_s is reached the hop test passes too. */
        if (!v->analysing &&
            v->ring_count >= v->cfg.min_s * VIT_FS_HZ &&
            v->bins_since_analysis >= VIT_HOP_BINS) {
            int win = v->cfg.window_s * VIT_FS_HZ;
            v->an_len = (v->ring_count < win) ? v->ring_count : win;
            v->an_ch  = 0;
            v->bins_since_analysis = 0;
            memset(v->br_acc, 0, sizeof(v->br_acc));
            memset(v->hr_acc, 0, sizeof(v->hr_acc));
            v->analysing = true;
        }
    } else {
        for (int c = 0; c < v->nch; c++) {
            v->bin_sum[c] += na[v->chan[c]];
        }
        v->bin_n++;
    }

    v->out.still_s = (float)v->ring_count / (float)VIT_FS_HZ;

    /* One FFT per call at most, so no packet pays ~6-10 ms more than once
     * and the pipeline never falls a whole analysis behind. */
    bool done = false;
    if (v->analysing) {
        analyse_channel(v, v->an_ch);
        v->an_ch++;
        if (v->an_ch >= v->nch) {
            v->analysing = false;
            finish_analysis(v);
            done = true;
        }
    }
    return done;
}

const vitals_out_t *vitals_get(const vitals_t *v)
{
    return &v->out;
}

const char *vitals_state_name(vitals_state_t s)
{
    unsigned i = (unsigned)s;
    if (i >= sizeof(s_state_names) / sizeof(s_state_names[0])) {
        return "?";
    }
    return s_state_names[i];
}
