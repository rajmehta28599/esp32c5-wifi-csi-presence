/*
 * test_gain.c - csi_gain.c: which bytes are read, and the per-row summary.
 *
 * What this can show: the code reads bytes 22 (signed) and 23 (unsigned) of
 * the header it is given, refuses a short one, and counts changes the way
 * csi_gain.h says. What it cannot show: that those bytes ARE the receiver
 * gain on a C5. That comes from Espressif's binary (csi_gain.h) and needs a
 * capture (TESTING section 4).
 */
#include <stdio.h>
#include <string.h>
#include "test_util.h"
#include "csi_gain.h"

/* 64 bytes: sizeof(wifi_pkt_rx_ctrl_t) on the C5 with ESP-IDF v6.1
 * (riscv32-esp-elf-gcc, 2026-11-03); channel is byte 28. */
#define RX_LEN 64

static void test_read(void)
{
    uint8_t hdr[RX_LEN];
    uint8_t agc = 0x5a;
    int8_t  fft = 0x5a;
    printf("(1) reading the two bytes\n");
    for (int i = 0; i < RX_LEN; i++) hdr[i] = (uint8_t)(0xA0 + i);
    hdr[22] = 0xF6;            /* fft -10 */
    hdr[23] = 200;             /* agc 200 */
    hdr[28] = 6;               /* channel, must not be read */
    CHECK(csi_gain_read(hdr, RX_LEN, &agc, &fft), "64-byte header refused");
    CHECK(agc == 200 && fft == -10, "read agc %u fft %d, want 200 / -10", agc, fft);
    hdr[22] = 0x7F; hdr[23] = 0;
    CHECK(csi_gain_read(hdr, RX_LEN, &agc, &fft) && agc == 0 && fft == 127,
          "read agc %u fft %d, want 0 / 127", agc, fft);
    hdr[22] = 0x80; hdr[23] = 255;
    CHECK(csi_gain_read(hdr, RX_LEN, &agc, &fft) && agc == 255 && fft == -128,
          "read agc %u fft %d, want 255 / -128", agc, fft);

    /* 24 bytes is the shortest header that holds byte 23 */
    CHECK(csi_gain_read(hdr, 24, &agc, &fft), "24-byte header refused");
    agc = 7; fft = 7;
    CHECK(!csi_gain_read(hdr, 23, &agc, &fft), "23-byte header accepted");
    CHECK(agc == 7 && fft == 7, "a refused read changed the outputs (%u, %d)", agc, fft);
    CHECK(!csi_gain_read(NULL, RX_LEN, &agc, &fft), "NULL header accepted");
    CHECK(!csi_gain_read(hdr, RX_LEN, NULL, &fft), "NULL agc accepted");
    CHECK(!csi_gain_read(hdr, RX_LEN, &agc, NULL), "NULL fft accepted");
}

static void test_track(void)
{
    csi_gain_track_t t, o;
    printf("(2) per-row summary\n");
    csi_gain_reset(&t);
    CHECK(!csi_gain_take(&t, &o) && o.packets == 0, "an empty tracker reported a row");

    /* row 1: 40, 40, 44, 44, 40 -> 2 changes, lo 40, hi 44, last 40 */
    static const uint8_t a1[5] = { 40, 40, 44, 44, 40 };
    for (int i = 0; i < 5; i++) csi_gain_push(&t, a1[i], -2);
    CHECK(csi_gain_take(&t, &o), "row 1 missing");
    CHECK(o.packets == 5 && o.changes == 2 && o.agc_lo == 40 && o.agc_hi == 44 &&
          o.agc == 40 && o.fft == -2,
          "row 1: %u packets, %u changes, agc %u-%u last %u fft %d; want 5, 2, 40-44, 40, -2",
          (unsigned)o.packets, (unsigned)o.changes, o.agc_lo, o.agc_hi, o.agc, o.fft);

    /* the first packet of row 2 differs from the last of row 1: counted;
     * a change in fft alone is a change too; lo/hi restart per row */
    csi_gain_push(&t, 48, -2);
    csi_gain_push(&t, 48, -4);
    CHECK(csi_gain_take(&t, &o), "row 2 missing");
    CHECK(o.packets == 2 && o.changes == 2 && o.agc_lo == 48 && o.agc_hi == 48 &&
          o.fft == -4,
          "row 2: %u packets, %u changes, agc %u-%u fft %d; want 2, 2, 48-48, -4",
          (unsigned)o.packets, (unsigned)o.changes, o.agc_lo, o.agc_hi, o.fft);

    /* a row with no packet: have false, the last values are kept */
    CHECK(!csi_gain_take(&t, &o) && o.packets == 0 && o.changes == 0,
          "an empty row: have %d, %u packets, %u changes", o.have,
          (unsigned)o.packets, (unsigned)o.changes);
    CHECK(o.ever && o.agc == 48 && o.fft == -4, "an empty row lost the last values");

    /* constant gain: 0 changes over many packets */
    for (int i = 0; i < 3000; i++) csi_gain_push(&t, 48, -4);
    csi_gain_take(&t, &o);
    CHECK(o.changes == 0 && o.packets == 3000, "constant gain: %u changes in %u packets",
          (unsigned)o.changes, (unsigned)o.packets);

    /* saturation: 70000 alternating packets stop at 65535 changes */
    for (int i = 0; i < 70000; i++) csi_gain_push(&t, (uint8_t)(i & 1 ? 50 : 52), -4);
    csi_gain_take(&t, &o);
    CHECK(o.changes == UINT16_MAX && o.packets == 70000, "saturation: %u changes, %u packets",
          (unsigned)o.changes, (unsigned)o.packets);

    /* reset forgets the previous packet: the next one is not a change */
    csi_gain_reset(&t);
    csi_gain_push(&t, 10, 0);
    csi_gain_take(&t, &o);
    CHECK(o.changes == 0 && o.packets == 1, "after reset: %u changes", (unsigned)o.changes);
}

/* (3) random headers and random gain walks against a plain recount */
static void test_random(void)
{
    uint8_t hdr[RX_LEN];
    int     bad_read = 0, bad_count = 0;
    printf("(3) 2000 random headers, 200 random gain walks\n");
    rng_seed(4242u);
    for (int n = 0; n < 2000; n++) {
        for (int i = 0; i < RX_LEN; i++) hdr[i] = (uint8_t)(rng_u32() >> 24);
        uint8_t agc;
        int8_t  fft;
        if (!csi_gain_read(hdr, RX_LEN, &agc, &fft) || agc != hdr[23] ||
            fft != (int8_t)hdr[22]) {
            bad_read++;
        }
    }
    CHECK(bad_read == 0, "%d of 2000 random headers read wrong", bad_read);
    for (int w = 0; w < 200; w++) {
        csi_gain_track_t t, o;
        uint8_t  a = 40, pa = 0, lo = 255, hi = 0;
        int8_t   f = 0, pf = 0;
        unsigned want = 0;
        int      len = 1 + (int)(rng_uniform() * 300.0f);
        csi_gain_reset(&t);
        for (int k = 0; k < len; k++) {
            if (rng_uniform() < 0.1f) a = (uint8_t)(a + (rng_u32() & 1 ? 2 : -2));
            if (rng_uniform() < 0.05f) f = (int8_t)(f + (rng_u32() & 1 ? 1 : -1));
            if (k > 0 && (a != pa || f != pf)) want++;
            if (a < lo) lo = a;
            if (a > hi) hi = a;
            csi_gain_push(&t, a, f);
            pa = a; pf = f;
        }
        csi_gain_take(&t, &o);
        if (o.changes != want || o.packets != (uint32_t)len || o.agc_lo != lo ||
            o.agc_hi != hi || o.agc != a || o.fft != f) {
            bad_count++;
        }
    }
    CHECK(bad_count == 0, "%d of 200 random walks summarised wrong", bad_count);
}

int main(void)
{
    printf("test_gain: the rx_ctrl bytes csi_gain.c reads and its per-row counts - not\n"
           "evidence that those bytes are the C5's receiver gain.\n");
    test_read();
    test_track();
    test_random();
    return test_summary("test_gain");
}
