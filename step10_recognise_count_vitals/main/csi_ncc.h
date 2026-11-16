/*
 * csi_ncc.h - a tiny nearest-centroid classifier with rejection, used for
 *             BOTH person recognition (gait_id features) and occupancy
 *             counting (count_est features).
 *
 * Why nearest-centroid and not something cleverer: it trains on-device from
 * a few dozen windows, needs no floating-point matrix inverse, stores in a
 * few hundred bytes of NVS, and fails in an obvious way ("unknown") instead
 * of confidently. With 2-3 enrolled people that is about as much model as
 * the data from one antenna can support.
 *
 * Each class keeps a running (Welford) mean and sum of squared deviations
 * per feature. Distances are measured in units of the POOLED within-class
 * standard deviation of each feature, averaged over the features
 * (RMS), so a window from an enrolled class sits near 1.0 from its own
 * centroid regardless of how many features there are.
 *
 * ESP-IDF free: the model is a plain struct that main.c stores as one NVS
 * blob. Host-testable with MSVC.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NCC_MAX_FEAT     18
#define NCC_MAX_CLASSES  4
#define NCC_NAME_LEN     16          /* including the terminating NUL */

#define NCC_MAGIC        0x3143434Eu /* "NCC1" little-endian */
#define NCC_VERSION      1

typedef struct {
    char     name[NCC_NAME_LEN];     /* NUL-terminated; "" = unused slot   */
    uint32_t n;                      /* feature vectors accumulated         */
    float    mean[NCC_MAX_FEAT];
    float    m2[NCC_MAX_FEAT];       /* Welford: sum of squared deviations  */
} ncc_class_t;

typedef struct {
    uint32_t    magic;
    uint16_t    version;
    uint16_t    nfeat;               /* features in use, <= NCC_MAX_FEAT     */
    uint16_t    csi_len;             /* CSI shape trained on (0 = unknown)   */
    uint8_t     channel;             /* Wi-Fi channel trained on (0 = unknown) */
    uint8_t     reserved;
    ncc_class_t cls[NCC_MAX_CLASSES];
} ncc_model_t;

typedef struct {
    int   best;      /* winning class index, or -1 = unknown / no usable model */
    int   nearest;   /* nearest class even when rejected (-1 if no model)      */
    float dist;      /* RMS standardised distance to the nearest class         */
    float second;    /* distance to the runner-up; 1e30f if there is none      */
} ncc_result_t;

/* Empty model for nfeat features (clamped to NCC_MAX_FEAT). */
void ncc_init(ncc_model_t *m, int nfeat);

/* Sanity check after loading a blob from NVS: magic, version, nfeat, and
 * every name NUL-terminated. */
bool ncc_valid(const ncc_model_t *m, int nfeat);

/* Forget one class (statistics and name). */
void ncc_clear_class(ncc_model_t *m, int c);

/* Set a class name (truncated to NCC_NAME_LEN-1). False on bad index or an
 * empty name. */
bool ncc_set_name(ncc_model_t *m, int c, const char *name);

/* Add one feature vector (m->nfeat floats) to class c. Non-finite inputs
 * are rejected (returns false) so one NaN cannot poison a template. */
bool ncc_add(ncc_model_t *m, int c, const float *x);

/* Number of classes with at least min_n vectors. */
int ncc_trained_classes(const ncc_model_t *m, uint32_t min_n);

/* Classify x against every class with n >= min_n.
 *   reject_rms : nearest distance above this -> best = -1 (unknown).
 *   margin     : with 2+ usable classes, require second/dist >= margin,
 *                otherwise best = -1 (ambiguous). margin <= 1 disables it.
 * Pooled per-feature std is sqrt(sum_c m2_c / sum_c (n_c - 1)) over usable
 * classes, floored at 1e-4 so a constant feature cannot divide by zero.
 * No usable class -> best = nearest = -1. */
ncc_result_t ncc_classify(const ncc_model_t *m, const float *x, uint32_t min_n,
                          float reject_rms, float margin);

/* ncc_distances() writes this for a class that has no distance: unused,
 * or fewer than min_n vectors. Negative, so "d < 0 = none" (never NaN,
 * which would compare false with every threshold). */
#define NCC_NO_DIST      (-1.0f)

/* The RMS standardised distance from x to EVERY usable class (n >= min_n),
 * for explaining a decision: out[c] is exactly the number ncc_classify
 * compares for class c - the same pooled std, the same code, the same
 * 1e30f cap - so out[r.nearest] == r.dist and the smallest of the others
 * == r.second, bit for bit. Unusable classes get NCC_NO_DIST. Returns the
 * number of usable classes; 0 (and NCC_NO_DIST everywhere) when there is
 * none, or when x holds a NaN or an infinity - ncc_classify's "no result". */
int ncc_distances(const ncc_model_t *m, const float *x, uint32_t min_n,
                  float out[NCC_MAX_CLASSES]);
