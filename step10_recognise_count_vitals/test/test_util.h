/*
 * test_util.h - shared helpers for Step 10's host unit tests.
 *
 * These tests run on the PC (MSVC, see run_tests.ps1), not on the C5.
 * They prove the DSP and classifier code does what it claims on SYNTHETIC
 * signals with known answers. They prove nothing about real rooms, real
 * people or real CSI - that needs the hardware procedure in the README.
 */

#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_failures++;                                                 \
            printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol, what)                                       \
    CHECK(fabs((double)(a) - (double)(b)) <= (double)(tol),               \
          "%s: got %.6g, want %.6g +/- %.3g", (what), (double)(a),        \
          (double)(b), (double)(tol))

/* Call at the end of main(): prints a summary, returns the exit code. */
static int test_summary(const char *name)
{
    printf("%s: %d checks, %d failures -> %s\n", name, g_checks, g_failures,
           g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}

/* Deterministic PRNG (xorshift32) so every run sees identical "noise". */
static uint32_t g_rng = 0x12345678u;

static void rng_seed(uint32_t s) { g_rng = s ? s : 0x12345678u; }

static uint32_t rng_u32(void)
{
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

/* uniform in [0, 1) */
static float rng_uniform(void)
{
    return (float)(rng_u32() >> 8) * (1.0f / 16777216.0f);
}

/* standard normal (Box-Muller) */
static inline float rng_gauss(void)   /* inline: test_pvitals never calls it, gcc -Wunused-function */
{
    float u1 = rng_uniform();
    float u2 = rng_uniform();
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.28318530718f * u2);
}
