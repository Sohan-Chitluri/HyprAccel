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
 * Internal representation: Q1.15 fixed-point, 16 iterations.
 * See hyp_cordic_ref.c for full algorithm and RTL mirror documentation.
 */

#ifndef HYP_CORDIC_REF_H
#define HYP_CORDIC_REF_H

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
 * Precision: max absolute error < 2e-4 vs math.h (Q1.15, 16 iterations).
 * Not thread-safe (pure function, but see note on float normalisation loop).
 */
void hyp_cordic_ref(float angle_degrees, float *out_sin, float *out_cos);

#ifdef __cplusplus
}
#endif

#endif /* HYP_CORDIC_REF_H */
