/*
 * csi_gain.h - receiver gain per CSI packet, for logging only (D-176)
 *
 * WHY: CSI amplitudes are measured AFTER the receiver's automatic gain
 * control. A gain step changes every amplitude at once; the per-packet
 * median division removes a pure common factor, but not the int8 rounding
 * or the shape changes a step can bring (test_e2e (f), TESTING section 3.3
 * item 11). The detector cannot tell a gain step from a person, so the
 * first thing to do is SEE the steps: this module reads the gain the
 * receiver used for each packet and counts how often it changed.
 *
 * WHERE THE BYTES COME FROM: ESP-IDF documents no gain field. Bytes 22
 * and 23 of the C5's rx_ctrl header are reserved bits in ESP-IDF v6.0.2
 * and v6.1 (esp_wifi_he_types.h, the SOC_WIFI_MAC_VERSION_NUM == 3
 * layout: noise_floor is byte 20, channel byte 28). Espressif's own
 * esp_csi_gain_ctrl v0.1.5 (Apache-2.0, step09 managed_components),
 * library 6.0/esp32c5, reads them in esp_csi_gain_ctrl_get_rx_gain:
 *     lbu  a5,23(a0)  -> agc_gain (unsigned)
 *     lb   a5,22(a0)  -> fft_gain (signed)
 * (riscv32-esp-elf-objdump, 2026-11-03; the c6 and c61 builds read the
 * same bytes). So this follows a vendor binary, not a documented field,
 * and NOTHING HERE HAS BEEN SEEN ON SILICON. First-capture sanity check
 * (TESTING section 4): agc should move with distance / RSSI and stay
 * nearly constant on a still link; a value that never changes, or that
 * equals the channel, means the bytes are not the gain.
 *
 * No ESP-IDF headers: host-tested by test/test_gain.c. main.c passes the
 * address and size of info->rx_ctrl.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CSI_GAIN_FFT_BYTE 22   /* int8  fft_gain, esp_csi_gain_ctrl v0.1.5 */
#define CSI_GAIN_AGC_BYTE 23   /* uint8 agc_gain                            */

/* Read the gain bytes from a raw rx_ctrl header of rx_len bytes.
 * false (outputs untouched) when the header is too short or NULL. */
bool csi_gain_read(const void *rx_ctrl, size_t rx_len, uint8_t *agc, int8_t *fft);

/* Summary over the packets since the last csi_gain_take(). */
typedef struct {
    bool     have;        /* at least one packet since the last take        */
    bool     ever;        /* a packet was ever seen (prev is valid)         */
    uint8_t  agc;         /* last packet's values                           */
    int8_t   fft;
    uint8_t  agc_lo, agc_hi;
    uint16_t changes;     /* packets whose (agc, fft) differed from the
                             packet before (across takes); saturates       */
    uint32_t packets;     /* packets since the last take; saturates        */
} csi_gain_track_t;

void csi_gain_reset(csi_gain_track_t *t);

/* One packet's gain. */
void csi_gain_push(csi_gain_track_t *t, uint8_t agc, int8_t fft);

/* Copy the summary to *out and start a new period; the last values stay
 * known, so a change across the boundary is still counted. Returns
 * out->have. */
bool csi_gain_take(csi_gain_track_t *t, csi_gain_track_t *out);
