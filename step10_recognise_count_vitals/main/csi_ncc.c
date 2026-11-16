/*
 * csi_ncc.c - see csi_ncc.h. No FPU on the C5: float only. The expensive
 * soft-float operations (division, sqrtf) are paid once per feature per
 * classification, not once per class, and classification runs once per
 * gait window / count block, never per packet. ncc_distances (main.c's
 * "explain" output) repeats that setup once per window / block - a few
 * hundred operations beside a gait window's ~58k.
 *
 * Robustness is the point of most of the code below. A template is built
 * from minutes of someone walking around and then lives in NVS for months,
 * so:
 *   - one bad feature vector (NaN, inf, or finite values whose update
 *     overflows) must not poison it: ncc_add computes the whole update in
 *     temporaries and commits nothing unless every result is finite;
 *   - a corrupted blob that still passes ncc_valid (huge but finite
 *     numbers) must not make ncc_classify produce NaN: the pooled variance
 *     is capped, and a non-finite distance is mapped to NCC_FAR ("very
 *     far", which rejects) instead of silently failing every comparison.
 * NaN is the dangerous value here because every comparison with it is
 * false: a NaN distance is never "> reject_rms", so it would be ACCEPTED.
 */

#include "csi_ncc.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* Floor on the pooled per-feature std: a feature that never varied during
 * enrolment (a band that is always 0 at a low ping rate, for example)
 * would otherwise divide by zero. 1e-4 is below the resolution of every
 * feature gait_id and count_est produce. */
#define NCC_STD_FLOOR  1e-4f

/* "No distance" / "infinitely far". Also the header's value for `second`
 * when there is no runner-up. Finite on purpose: main.c prints distances
 * with printf("%.2f"), and "inf" is not valid JSON. */
#define NCC_FAR        1e30f

/* Portable finiteness test (MSVC C11 and GCC agree on it; no isfinite()
 * macro dependence). The first comparison is false only for NaN, the
 * second is false for NaN and +/-inf. Relies on IEEE comparisons, i.e. no
 * -ffast-math, which neither the ESP-IDF build nor run_tests.ps1 uses. */
static bool ncc_finite(float v)
{
    return (v == v) && fabsf(v) <= FLT_MAX;
}

void ncc_init(ncc_model_t *m, int nfeat)
{
    if (!m) return;
    /* The struct is written to NVS byte for byte: zero everything,
     * including padding and `reserved`, so two identical models are
     * identical blobs. */
    memset(m, 0, sizeof(*m));
    if (nfeat < 0) nfeat = 0;
    if (nfeat > NCC_MAX_FEAT) nfeat = NCC_MAX_FEAT;
    m->magic   = NCC_MAGIC;
    m->version = NCC_VERSION;
    m->nfeat   = (uint16_t)nfeat;
}

bool ncc_valid(const ncc_model_t *m, int nfeat)
{
    if (!m) return false;
    if (m->magic != NCC_MAGIC || m->version != NCC_VERSION) return false;
    /* A zero-feature model cannot classify anything, so it is not valid
     * even if the caller asked for 0. */
    if (nfeat < 1 || nfeat > NCC_MAX_FEAT || (int)m->nfeat != nfeat) return false;

    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        const ncc_class_t *k = &m->cls[c];
        /* main.c prints names with %s: an unterminated one would read
         * past the struct. */
        if (memchr(k->name, '\0', NCC_NAME_LEN) == NULL) return false;
        if (k->n == 0) continue;   /* unused: its statistics are never read */
        for (int f = 0; f < nfeat; f++) {
            /* m2 < 0 is a true corruption detector, not a rounding trap:
             * each Welford step adds d * (x - mean_new), and mean_new lies
             * between the old mean and x, so both factors have the same
             * sign (or one is 0) in float as well as in exact arithmetic. */
            if (!ncc_finite(k->mean[f]) || !ncc_finite(k->m2[f]) || k->m2[f] < 0.0f) {
                return false;
            }
        }
    }
    return true;
}

void ncc_clear_class(ncc_model_t *m, int c)
{
    if (!m || c < 0 || c >= NCC_MAX_CLASSES) return;
    memset(&m->cls[c], 0, sizeof(m->cls[c]));
}

bool ncc_set_name(ncc_model_t *m, int c, const char *name)
{
    if (!m || c < 0 || c >= NCC_MAX_CLASSES || !name || name[0] == '\0') {
        return false;
    }
    /* A copy loop rather than strncpy: it always terminates, and it zero
     * fills the rest of the field, so a shorter name written over a longer
     * one leaves no stale bytes in the NVS blob. */
    char *dst = m->cls[c].name;
    int   i   = 0;
    for (; i < NCC_NAME_LEN - 1 && name[i] != '\0'; i++) {
        dst[i] = name[i];
    }
    for (; i < NCC_NAME_LEN; i++) {
        dst[i] = '\0';
    }
    return true;
}

bool ncc_add(ncc_model_t *m, int c, const float *x)
{
    float new_mean[NCC_MAX_FEAT];
    float new_m2[NCC_MAX_FEAT];

    if (!m || !x || c < 0 || c >= NCC_MAX_CLASSES) return false;
    int nf = m->nfeat;
    if (nf < 1 || nf > NCC_MAX_FEAT) return false;

    ncc_class_t *k = &m->cls[c];
    if (k->n == UINT32_MAX) return false;

    /* Check every input first, so a NaN in the LAST feature cannot leave
     * the earlier features updated. */
    for (int f = 0; f < nf; f++) {
        if (!ncc_finite(x[f])) return false;
    }

    /* Welford: numerically stable in float where the textbook
     * sum(x^2) - n*mean^2 is not (a feature like log10(variance) sits far
     * from 0 with a small spread, and the difference of two large sums
     * would cancel to noise). A class with n == 0 starts from scratch
     * whatever its stored statistics say - ncc_valid does not look at
     * unused classes, so they may hold anything. */
    float n1 = (float)(k->n + 1u);
    for (int f = 0; f < nf; f++) {
        float mean0 = (k->n > 0) ? k->mean[f] : 0.0f;
        float m20   = (k->n > 0) ? k->m2[f]   : 0.0f;
        float d     = x[f] - mean0;
        float mu    = mean0 + d / n1;
        float s     = m20 + d * (x[f] - mu);
        /* finite inputs can still overflow (x = 3e38 after -3e38) */
        if (!ncc_finite(mu) || !ncc_finite(s)) return false;
        new_mean[f] = mu;
        new_m2[f]   = s;
    }

    for (int f = 0; f < nf; f++) {
        k->mean[f] = new_mean[f];
        k->m2[f]   = new_m2[f];
    }
    k->n++;
    return true;
}

int ncc_trained_classes(const ncc_model_t *m, uint32_t min_n)
{
    if (!m) return 0;
    if (min_n < 1) min_n = 1;
    int count = 0;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (m->cls[c].n >= min_n) count++;
    }
    return count;
}

/* What ncc_classify and ncc_distances share: which classes are usable,
 * and the reciprocal pooled std per feature. ONE implementation, so the
 * distances the firmware explains (ncc_distances) are bit for bit the ones
 * it decided on (ncc_classify). Returns the number of usable classes, or
 * 0 - "no result" - when there is none, the model is malformed, or x holds
 * a NaN or an infinity. */
static int ncc_prepare(const ncc_model_t *m, const float *x, uint32_t min_n,
                       bool use[NCC_MAX_CLASSES], float inv_s[NCC_MAX_FEAT])
{
    if (!m || !x) return 0;
    int nf = m->nfeat;
    if (nf < 1 || nf > NCC_MAX_FEAT) return 0;
    if (min_n < 1) min_n = 1;

    int   usable = 0;
    float dof    = 0.0f;         /* sum over usable classes of (n - 1) */
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        use[c] = (m->cls[c].n >= min_n);
        if (use[c]) {
            usable++;
            dof += (float)(m->cls[c].n - 1u);
        }
    }
    if (usable == 0) return 0;

    /* A NaN input would make every distance NaN, and a NaN distance fails
     * every comparison - including "> reject_rms". Report "no result"
     * rather than guess. */
    for (int f = 0; f < nf; f++) {
        if (!ncc_finite(x[f])) return 0;
    }

    /* Pooled within-class std per feature. POOLED, not per class: with a
     * few dozen windows each, per-class variances are noisy, and a class
     * that happened to enrol with a wide spread would otherwise attract
     * every stranger. Only usable classes contribute, so a half-enrolled
     * slot cannot change the distances of the finished ones. Computed
     * once per feature and stored as a reciprocal, so the per-class loop
     * (ncc_dist) is multiply-add only. */
    for (int f = 0; f < nf; f++) {
        float s = NCC_STD_FLOOR;
        if (dof > 0.0f) {
            float num = 0.0f;
            for (int c = 0; c < NCC_MAX_CLASSES; c++) {
                if (use[c]) num += m->cls[c].m2[f];
            }
            float var = num / dof;
            /* Summing four finite m2 values can overflow to inf; inf/inf
             * later would be NaN. Cap it (the negation also catches NaN
             * from a model that never went through ncc_valid). */
            if (!(var <= FLT_MAX)) var = FLT_MAX;
            float sd = sqrtf(var);
            if (sd > s) s = sd;   /* false for NaN (var < 0) -> floor */
        }
        inv_s[f] = 1.0f / s;
    }
    return usable;
}

/* RMS standardised distance from x to class k, capped at NCC_FAR. */
static float ncc_dist(const ncc_class_t *k, const float *x, const float *inv_s, int nf)
{
    float sum = 0.0f;
    for (int f = 0; f < nf; f++) {
        float z = (x[f] - k->mean[f]) * inv_s[f];
        sum += z * z;
    }
    /* RMS over the features: ~1.0 for a vector from this class,
     * whatever nfeat is. */
    float d = sqrtf(sum / (float)nf);
    if (!(d <= NCC_FAR)) d = NCC_FAR;   /* inf or NaN -> "very far" */
    return d;
}

int ncc_distances(const ncc_model_t *m, const float *x, uint32_t min_n,
                  float out[NCC_MAX_CLASSES])
{
    if (!out) return 0;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        out[c] = NCC_NO_DIST;
    }
    bool  use[NCC_MAX_CLASSES];
    float inv_s[NCC_MAX_FEAT];
    int   usable = ncc_prepare(m, x, min_n, use, inv_s);
    if (usable == 0) return 0;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (use[c]) out[c] = ncc_dist(&m->cls[c], x, inv_s, m->nfeat);
    }
    return usable;
}

ncc_result_t ncc_classify(const ncc_model_t *m, const float *x, uint32_t min_n,
                          float reject_rms, float margin)
{
    ncc_result_t r;
    r.best    = -1;
    r.nearest = -1;
    r.dist    = NCC_FAR;
    r.second  = NCC_FAR;

    bool  use[NCC_MAX_CLASSES];
    float inv_s[NCC_MAX_FEAT];
    int   usable = ncc_prepare(m, x, min_n, use, inv_s);
    if (usable == 0) return r;

    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (!use[c]) continue;
        float d = ncc_dist(&m->cls[c], x, inv_s, m->nfeat);

        /* strict '<': on an exact tie the lower class index wins */
        if (r.nearest < 0 || d < r.dist) {
            r.second  = r.dist;
            r.dist    = d;
            r.nearest = c;
        } else if (d < r.second) {
            r.second = d;
        }
    }

    r.best = r.nearest;
    /* Written as !(<=) so a NaN threshold rejects instead of accepting. */
    if (!(r.dist <= reject_rms)) {
        r.best = -1;
    } else if (usable >= 2 && margin > 1.0f && r.second < margin * r.dist) {
        r.best = -1;
    }
    return r;
}
