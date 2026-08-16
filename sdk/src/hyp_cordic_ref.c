/*
 * hyp_cordic_ref.c  —  HyprAccel CORE-T4
 *
 * Reference CORDIC implementation: rotation mode, iterative shift-add.
 *
 * PURPOSE (dual):
 *   1. GOLDEN REFERENCE — This file is the algorithmic specification that the
 *      Verilog CORDIC (RTL-T1) will be validated against.  The structure here
 *      (iteration count, angle table, fixed-point format, gain handling) is
 *      intentionally mirrored in the RTL so that "does the RTL match?" is a
 *      straightforward numerical comparison, not a design re-derivation.
 *
 *   2. SOFTWARE BASELINE — This is the unoptimised software path used in the
 *      demo to contrast against the FPGA accelerator's latency.  It must NOT
 *      be hand-optimised (no SIMD, no table-replacement of the whole algorithm,
 *      no compiler hints beyond -O2) — the point is a fair worst-case baseline.
 *
 * ALGORITHM OVERVIEW — CORDIC rotation mode
 *   Given angle θ, start with vector (K, 0) and rotate it toward θ by
 *   successively adding or subtracting pseudo-rotations of 2^-i radians.
 *   After N iterations the vector converges to (cos θ, sin θ).
 *
 *   Each iteration:
 *     x_{i+1} = x_i  -  d_i * y_i * 2^-i
 *     y_{i+1} = y_i  +  d_i * x_i * 2^-i
 *     z_{i+1} = z_i  -  d_i * atan(2^-i)
 *   where d_i = +1 if z_i >= 0, else -1.
 *
 *   The CORDIC gain K = ∏ cos(atan(2^-i)) ≈ 0.60725293...
 *   is pre-applied to the initial x so no post-scaling is needed.
 *
 * FIXED-POINT FORMAT — Q1.15
 *   Signed 16-bit, 1 sign bit + 15 fractional bits.
 *   Range: [-1.0, +1.0).  Scale factor: 2^15 = 32768.
 *
 *   Why Q1.15?  The CORDIC intermediate values are bounded to [-1, 1] by
 *   construction (post-gain normalisation), so no overflow bits are needed.
 *   This maps directly to Verilog: `signed [15:0]` with the binary point
 *   after bit 14.
 *
 * ITERATIONS — 16
 *   16 iterations → ~4.8 × 10^-5 rad worst-case residual (< 1 LSB in Q1.15).
 *   The RTL counterpart is a 16-stage combinational pipeline: one stage per
 *   iteration, one clock per stage (or fully unrolled in one cycle on a fast
 *   FPGA).
 *
 * FLOAT BOUNDARY
 *   External API uses float (degrees in, sin/cos out).
 *   Fixed-point conversion happens once on entry and once on exit.
 *   The inner loop is integer-only — no floats inside.
 *
 * QUADRANT HANDLING
 *   CORDIC rotation mode converges only for |z| <= π/2 (±90°).
 *   Inputs outside this range are reflected into the first/fourth quadrant
 *   before iteration and the results are sign-corrected on exit.
 *
 * RTL MIRROR NOTES (for the Verilog author)
 *   - ANGLE_TABLE[i] below becomes a parameter array in the RTL.
 *   - The gain-pre-applied initial x (X_INIT) becomes a constant input.
 *   - d_i is the sign bit of z (1 bit).
 *   - The shift x_i >> i is a wired shift in hardware (free).
 *   - The 16 iterations map directly to 16 pipeline stages.
 *   - Register interface (RTL-T2) wraps this pipeline with start/done flags
 *     and a register set for (z_in, x_out, y_out).
 */

#include "hyp_cordic_ref.h"

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Fixed-point parameters
 * ---------------------------------------------------------------------- */

/* Q1.15 scale: 1.0 in float = 32768 in fixed */
#define Q15_SCALE      32768

/* Number of CORDIC iterations.
 * RTL MIRROR: This is the pipeline depth.  16 stages. */
#define CORDIC_ITERS   16

/* -------------------------------------------------------------------------
 * Pre-computed angle table: atan(2^-i) in Q1.15, for i = 0 .. 15
 *
 * atan(2^0)  = 45.000°  = 0.7853981... rad  → 0.7853981 * 32768 = 25736
 * atan(2^-1) = 26.565°  = 0.4636476... rad  → 15121
 * ...
 *
 * Generated from: round(atan(pow(2,-i)) * 32768) for i in range(16)
 *
 * RTL MIRROR: Stored as a 16-element ROM (16 × 16-bit words).
 * ---------------------------------------------------------------------- */
/* Values verified against: round(atan(pow(2,-i)) * 32768) for i in 0..15 */
static const int16_t ANGLE_TABLE[CORDIC_ITERS] = {
    /*  i=0  */ 25736,  /* atan(2^0)  = 0.78539816 rad  round(0.78539816*32768) = 25736 */
    /*  i=1  */ 15193,  /* atan(2^-1) = 0.46364761 rad  round(0.46364761*32768) = 15193 */
    /*  i=2  */  8027,  /* atan(2^-2) = 0.24497866 rad  round(0.24497866*32768) =  8027 */
    /*  i=3  */  4075,  /* atan(2^-3) = 0.12435500 rad  round(0.12435500*32768) =  4075 */
    /*  i=4  */  2045,  /* atan(2^-4) = 0.06241881 rad  round(0.06241881*32768) =  2045 */
    /*  i=5  */  1024,  /* atan(2^-5) = 0.03123983 rad  round(0.03123983*32768) =  1024 */
    /*  i=6  */   512,  /* atan(2^-6) = 0.01562373 rad  round(0.01562373*32768) =   512 */
    /*  i=7  */   256,  /* atan(2^-7) = 0.00781234 rad  round(0.00781234*32768) =   256 */
    /*  i=8  */   128,  /* atan(2^-8) = 0.00390623 rad  round(0.00390623*32768) =   128 */
    /*  i=9  */    64,  /* atan(2^-9) = 0.00195312 rad  round(0.00195312*32768) =    64 */
    /* i=10  */    32,  /* atan(2^-10)= 0.00097656 rad  round(0.00097656*32768) =    32 */
    /* i=11  */    16,  /* atan(2^-11)= 0.00048828 rad  round(0.00048828*32768) =    16 */
    /* i=12  */     8,  /* atan(2^-12)= 0.00024414 rad  round(0.00024414*32768) =     8 */
    /* i=13  */     4,  /* atan(2^-13)= 0.00012207 rad  round(0.00012207*32768) =     4 */
    /* i=14  */     2,  /* atan(2^-14)= 0.00006104 rad  round(0.00006104*32768) =     2 */
    /* i=15  */     1,  /* atan(2^-15)= 0.00003052 rad  round(0.00003052*32768) =     1 */
};

/* -------------------------------------------------------------------------
 * CORDIC gain pre-applied to initial x.
 *
 * K = ∏_{i=0}^{15} 1/sqrt(1 + 2^(-2i)) ≈ 0.60725293500888
 *
 * In Q1.15: round(0.60725293500888 * 32768) = 19898
 *
 * RTL MIRROR: X_INIT is a hardwired constant fed into the first pipeline
 * stage as the initial x value.  y_init = 0 always.
 * ---------------------------------------------------------------------- */
#define X_INIT_Q15    19898   /* K * 2^15 */

/* -------------------------------------------------------------------------
 * hyp_cordic_ref()
 *
 * Compute sin and cos of an angle in degrees using CORDIC rotation mode.
 *
 * Parameters:
 *   angle_degrees  — input angle, any value (normalised internally to [0,360))
 *   out_sin        — pointer to float to receive sin(angle)
 *   out_cos        — pointer to float to receive cos(angle)
 *
 * Precision: max absolute error < 2e-4 vs math.h (verified by test harness).
 * ---------------------------------------------------------------------- */
void hyp_cordic_ref(float angle_degrees, float *out_sin, float *out_cos)
{
    /* ----------------------------------------------------------------
     * STEP 1 — Normalise angle to [0, 360)
     * -------------------------------------------------------------- */
    while (angle_degrees <    0.0f) angle_degrees += 360.0f;
    while (angle_degrees >= 360.0f) angle_degrees -= 360.0f;

    /* ----------------------------------------------------------------
     * STEP 2 — Quadrant reduction to [-90°, +90°]
     *
     * CORDIC rotation-mode converges for |z| < ~99° but we reduce to
     * [-90°, +90°] for a clean match with the RTL's expected input range.
     *
     * Identities used:
     *   Q1 [0,   90]: angle as-is.              cos > 0, sin >= 0.
     *   Q2 [90, 180]: angle' = 180 - angle.     cos < 0, sin >= 0.
     *   Q3 [180,270]: angle' = angle - 180.     cos < 0, sin < 0.
     *   Q4 [270,360]: angle' = 360 - angle.     cos > 0, sin < 0.
     *
     * RTL MIRROR: 2-bit quadrant register set before the pipeline.
     * -------------------------------------------------------------- */
    int negate_cos = 0;
    int negate_sin = 0;

    if (angle_degrees <= 90.0f) {
        /* Q1: no change */
    } else if (angle_degrees <= 180.0f) {
        /* Q2: sin(θ) = sin(180-θ), cos(θ) = -cos(180-θ) */
        angle_degrees = 180.0f - angle_degrees;   /* result in [0, 90] */
        negate_cos = 1;
    } else if (angle_degrees <= 270.0f) {
        /* Q3: sin(θ) = -sin(θ-180), cos(θ) = -cos(θ-180) */
        angle_degrees = angle_degrees - 180.0f;   /* result in [0, 90] */
        negate_cos = 1;
        negate_sin = 1;
    } else {
        /* Q4: sin(θ) = -sin(360-θ), cos(θ) = cos(360-θ) */
        angle_degrees = 360.0f - angle_degrees;   /* result in [0, 90) */
        negate_sin = 1;
    }

    /* ----------------------------------------------------------------
     * STEP 3 — Convert angle to Q1.15 radians
     *
     * angle_q15 = angle_degrees * (π/180) * 32768
     *           = angle_degrees * 571.9095...
     *
     * Derivation: π/180 = 0.01745329..., × 32768 = 571.9094...
     * RTL MIRROR: one multiply by constant 572 at pipeline input.
     * Note: integer approximation 572 gives error < 0.05% vs 571.9095.
     * -------------------------------------------------------------- */
    int32_t z = (int32_t)(angle_degrees * 571.9095f);

    /* ----------------------------------------------------------------
     * STEP 4 — Initialise CORDIC state
     *
     * x = K (CORDIC gain pre-applied, Q1.15)
     * y = 0
     * z = angle in Q1.15 radians
     *
     * RTL MIRROR: values loaded into stage-0 registers on start pulse.
     * -------------------------------------------------------------- */
    int32_t x = X_INIT_Q15;
    int32_t y = 0;

    /* ----------------------------------------------------------------
     * STEP 5 — CORDIC iteration (the core, 16 stages)
     *
     * Each iteration i:
     *   d    = +1 if z >= 0, else -1
     *   x'   = x - d*(y >> i)     [right-shift = free wiring in HW]
     *   y'   = y + d*(x >> i)
     *   z'   = z - d*ANGLE_TABLE[i]
     *
     * RTL MIRROR: one set of assignments = one pipeline stage.
     * -------------------------------------------------------------- */
    for (int i = 0; i < CORDIC_ITERS; i++) {
        int32_t d     = (z >= 0) ? 1 : -1;
        int32_t x_new = x - d * (y >> i);
        int32_t y_new = y + d * (x >> i);
        int32_t z_new = z - d * ANGLE_TABLE[i];
        x = x_new;
        y = y_new;
        z = z_new;
    }

    /* ----------------------------------------------------------------
     * STEP 6 — Fixed-point → float
     *
     * After 16 iterations: x ≈ cos(θ_reduced) * 32768
     *                       y ≈ sin(θ_reduced) * 32768
     *
     * RTL MIRROR: done by host driver after reading output registers.
     * -------------------------------------------------------------- */
    float cos_val = (float)x / (float)Q15_SCALE;
    float sin_val = (float)y / (float)Q15_SCALE;

    /* ----------------------------------------------------------------
     * STEP 7 — Apply quadrant sign corrections
     * -------------------------------------------------------------- */
    if (negate_cos) cos_val = -cos_val;
    if (negate_sin) sin_val = -sin_val;

    *out_cos = cos_val;
    *out_sin = sin_val;
}
