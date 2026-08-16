/*
 * hyp_cordic_ref.h  —  HyprAccel CORE-T4
 *
 * Public header for the reference CORDIC implementation.
 * This is the software-callable interface that the RTL testbench driver
 * (RTL-T1) and the software execution path (CORE-T5) both include.
 *
 * Signature contract:
 *   angle_degrees  — input in degrees, any float value (normalised internally)
 *   *out_sin       — sin(angle_degrees), float, range [-1.0, 1.0]
 *   *out_cos       — cos(angle_degrees), float, range [-1.0, 1.0]
 *
 * Internal representation: Q4.12 fixed-point, 8 iterations, round-to-nearest.
 * See hyp_cordic_ref.c for full algorithm and RTL mirror documentation.
 */

#ifndef HYP_CORDIC_REF_H
#define HYP_CORDIC_REF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * hyp_cordic_ref — Compute sin/cos via CORDIC rotation mode.
 *
 * @param angle_degrees  Angle in degrees (any value, normalised to [0,360))
 * @param out_sin        Output: sin(angle_degrees)
 * @param out_cos        Output: cos(angle_degrees)
 *
 * Precision: max absolute error loose due to 8 iterations.
 */
void hyp_cordic_ref(float angle_degrees, float *out_sin, float *out_cos);

/**
 * hyp_cordic_core — Internal fixed-point CORDIC core for bit-exact RTL validation.
 * 
 * Matches golden_model.py exactly. 
 * Format: Q4.12. 
 * Width: 16-bit internal (though passed as int32_t to match python logic).
 */
void hyp_cordic_core(int32_t x_in, int32_t y_in, int32_t z_in, int saturate,
                     int32_t *x_out, int32_t *y_out, int32_t *z_out, int *overflow);

#ifdef __cplusplus
}
#endif

#endif /* HYP_CORDIC_REF_H */
