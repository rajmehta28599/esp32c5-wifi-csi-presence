/*
 * csi_baseline.h - the calibration record shared by Steps 06, 07 and 08.
 *
 * This is what "calibration" physically means in this project: a small
 * struct, computed once in an empty room, saved in NVS, and reloaded on
 * every boot. Steps 07 and 08 refuse to run without it.
 */

#pragma once

#include <stdint.h>

#define CSI_MAX_CSI_BYTES  768
#define CSI_MAX_SUBC       (CSI_MAX_CSI_BYTES / 2)   /* 384 */

#define CSI_BASELINE_MAGIC   0x43354253u   /* "C5BS" */
#define CSI_BASELINE_VERSION 2

#define CSI_NVS_NAMESPACE  "csi"
#define CSI_NVS_KEY        "baseline"

typedef struct {
    uint32_t magic;
    uint16_t version;

    /* --- the CSI shape this baseline is valid for --------------------
     * If the shape changes (different router, different channel, HT vs HE)
     * the baseline is meaningless. Steps 07/08 check these and refuse.  */
    uint16_t csi_len;      /* info->len we locked onto                   */
    uint16_t nsub;         /* number of subcarriers                      */
    uint8_t  fwi;          /* first_word_invalid: skip 4 bytes if set    */
    uint8_t  channel;      /* WiFi channel it was calibrated on          */
    uint16_t n_selected;   /* how many subcarriers survived selection    */

    float    rate_hz;      /* measured CSI packet rate during calibration*/

    /* --- per-subcarrier statistics from the empty room ---------------- */
    float    mu[CSI_MAX_SUBC];      /* mean normalised amplitude          */
    float    sigma[CSI_MAX_SUBC];   /* std dev - the per-subcarrier noise */
    uint8_t  selected[CSI_MAX_SUBC];/* 1 = use this subcarrier            */

    /* --- empty-room distribution of the two detector metrics ---------
     * Thresholds are derived from these, so the detector auto-tunes to
     * the noise floor of YOUR room instead of a magic constant.        */
    float    m_mean, m_std;   /* motion metric,    empty room            */
    float    d_mean, d_std;   /* deviation metric, empty room            */
} csi_baseline_t;
