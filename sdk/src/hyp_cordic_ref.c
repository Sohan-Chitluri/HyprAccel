/*
 * hyp_cordic_ref.c  —  HyprAccel CORE-T4 (Revised)
 *
 * Reference CORDIC implementation: rotation mode, iterative shift-add.
 * 
 * REVISED TO MATCH CONFIRMED RTL PARAMETERS:
 * - 8 iterations / pipeline stages.
 * - Format: 16-bit signed, Q4.12 (12 fractional bits).
 * - Shift operations use round-to-nearest.
 * - K prescaling using fixed 2488 multiplier matching RTL shift-add.
 */

#include "hyp_cordic_ref.h"
#include <stdint.h>

#define CORDIC_ITERS   8
#define Q12_SCALE      4096
#define MAX_POS        32767
#define MIN_NEG        (-32768)

/* atan(2^-i) in Q4.12 */
static const int16_t ANGLE_TABLE[CORDIC_ITERS] = {
    3217, 1899, 1003, 509, 256, 128, 64, 32
};

static int32_t wrap(int32_t v) {
    v &= 0xFFFF;
    if (v >= 32768) v -= 65536;
    return v;
}

static void addsub(int32_t a, int32_t b, int sub, int sat, int32_t *res, int *ovf) {
    int32_t full = sub ? (a - b) : (a + b);
    *ovf = (full < MIN_NEG || full > MAX_POS);
    if (sat && *ovf) {
        *res = (full > MAX_POS) ? MAX_POS : MIN_NEG;
    } else {
        *res = wrap(full);
    }
}

static int32_t arith_shift_right(int32_t val, int shift) {
    if (shift <= 0) return val;
    int32_t biased = val + (1 << (shift - 1));
    return biased >> shift;
}

static int32_t k_prescale(int32_t val) {
    int32_t acc = val * 2488;
    return (acc + 2048) >> 12;
}

void hyp_cordic_core(int32_t x_in, int32_t y_in, int32_t z_in, int saturate,
                     int32_t *x_out, int32_t *y_out, int32_t *z_out, int *overflow) 
{
    int32_t x = k_prescale(x_in);
    int32_t y = k_prescale(y_in);
    int32_t z = wrap(z_in);
    int ovf_flag = 0;

    for (int i = 0; i < CORDIC_ITERS; i++) {
        int sigma_pos = (z >= 0);

        int32_t x_sh = arith_shift_right(x, i);
        int32_t y_sh = arith_shift_right(y, i);

        int32_t x_mux = sigma_pos ? x_sh : -x_sh;
        int32_t y_mux = sigma_pos ? y_sh : -y_sh;

        int32_t x_new, y_new, z_new;
        int ovf_x, ovf_y, ovf_z;

        addsub(x, y_mux, 1, saturate, &x_new, &ovf_x);
        addsub(y, x_mux, 0, saturate, &y_new, &ovf_y);

        int32_t angle = ANGLE_TABLE[i];
        int32_t z_b = sigma_pos ? angle : -angle;
        addsub(z, z_b, 1, 0, &z_new, &ovf_z);

        ovf_flag = ovf_x || ovf_y;
        x = x_new;
        y = y_new;
        z = z_new;
    }

    *x_out = x;
    *y_out = y;
    *z_out = z;
    *overflow = ovf_flag;
}

void hyp_cordic_ref(float angle_degrees, float *out_sin, float *out_cos) 
{
    while (angle_degrees < 0.0f)   angle_degrees += 360.0f;
    while (angle_degrees >= 360.0f) angle_degrees -= 360.0f;

    int negate_cos = 0;
    int negate_sin = 0;

    if (angle_degrees <= 90.0f) {
        /* Q1 */
    } else if (angle_degrees <= 180.0f) {
        angle_degrees = 180.0f - angle_degrees;
        negate_cos = 1;
    } else if (angle_degrees <= 270.0f) {
        angle_degrees = angle_degrees - 180.0f;
        negate_cos = 1;
        negate_sin = 1;
    } else {
        angle_degrees = 360.0f - angle_degrees;
        negate_sin = 1;
    }

    /* angle_rad = angle_deg * pi/180
       angle_q12 = angle_rad * 4096 = angle_deg * (pi/180 * 4096) = angle_deg * 71.488686f */
    int32_t z_in = (int32_t)(angle_degrees * 71.488686f);

    int32_t xo, yo, zo;
    int ovf;
    
    hyp_cordic_core(Q12_SCALE, 0, z_in, 1, &xo, &yo, &zo, &ovf);

    float cos_val = (float)xo / (float)Q12_SCALE;
    float sin_val = (float)yo / (float)Q12_SCALE;

    if (negate_cos) cos_val = -cos_val;
    if (negate_sin) sin_val = -sin_val;

    *out_cos = cos_val;
    *out_sin = sin_val;
}
