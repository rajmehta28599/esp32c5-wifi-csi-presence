/*
 * count_est.h - features for telling ONE moving person from TWO OR MORE
 *               (EXPERIMENTAL). Classification is csi_ncc.h with two
 *               classes, trained on site; this module only produces the
 *               feature vectors.
 *
 * WHAT THIS CAN AND CANNOT DO
 *   One link cannot count people. What it can sometimes do is tell one
 *   mover from several: independent movers perturb different subcarrier
 *   groups independently, so the 8x8 group covariance has more than one
 *   significant eigenvalue - the PADS correlation-eigenvalue idea
 *   (docs/REFERENCES.md section 3.2) applied across subcarrier groups
 *   instead of antennas. Output is therefore 0 / 1 / 2+ at best, needs a
 *   per-room training session, and says nothing about people who sit
 *   still. Real counting (0-5 people, median absolute error 0.35) took
 *   four crossing ESP32 links (Wi-CaL, docs/REFERENCES.md).
 *
 * BLOCKS
 *   Non-overlapping blocks of COUNT_NWIN packets, accumulated as running
 *   sums, so there is no sample ring (memory: < 1 KB).
 *
 * FEATURES (COUNT_NFEAT = 6), in this order, over one block:
 *   Let C be the 8x8 covariance of the group signals, lambda_1 >= ... its
 *   eigenvalues, e(t) = sum_g grp_g(t)^2. No full eigen-decomposition is
 *   run: [0] needs none, and lambda_1, lambda_2 come from two power
 *   iterations (dsp_top_eig, the second on C - lambda_1 v v') refined by
 *   a 2x2 Rayleigh-Ritz step - see count_est.c for how close that gets.
 *   [0] participation ratio (sum lambda)^2 / sum lambda^2, in [1, 8],
 *       computed exactly as trace(C)^2 / ||C||_F^2 (the same number for a
 *       symmetric C)
 *   [1] lambda_2 / lambda_1 (0 if lambda_1 <= 0)
 *   [2] lambda_1 / sum lambda = lambda_1 / trace(C) (0 if the sum <= 0)
 *   [3] log10(trace(C) + 1e-6)
 *   [4] duty: fraction of the block's packets with moving == true
 *   [5] burstiness: std(e) / mean(e) (0 if mean(e) <= 0)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "csi_dsp.h"

#define COUNT_NWIN        256
#define COUNT_NFEAT       6
#define COUNT_MIN_DUTY    0.3f     /* main.c: classify only above this    */

typedef struct {
    int    n;
    int    n_moving;
    float  sum[CSI_NGROUPS];
    float  sum_xy[CSI_NGROUPS * (CSI_NGROUPS + 1) / 2];  /* upper triangle */
    float  e_sum, e_sum2;
} count_est_t;

void count_init(count_est_t *c);
void count_reset(count_est_t *c);

/* Once per accepted packet. Returns true when a block completed; feat then
 * holds COUNT_NFEAT features (regardless of duty - the caller decides). */
bool count_push(count_est_t *c, const float grp[CSI_NGROUPS], bool moving,
                float feat[COUNT_NFEAT]);
