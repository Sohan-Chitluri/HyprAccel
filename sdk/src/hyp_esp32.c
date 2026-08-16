/*
 * hyp_esp32.c  —  HyprAccel CORE-T6
 *
 * ESP32 (WROOM-32) Software Computation Path
 *
 * ==============================================================================
 * IMPORTANT: NOT A PRODUCTION COMPUTE PATH
 * ==============================================================================
 * In a real, deployed system, the ESP32 CPU orchestrates tasks and delegates
 * heavy computation (like CORDIC) to the FPGA. The CPU never computes these 
 * operations itself.
 * 
 * This file exists EXCLUSIVELY for two reasons:
 * 1. Sanity Checking: A secondary check against the FPGA's RTL output during dev.
 * 2. Demonstration Baseline: Serves as the deliberately-unoptimized software 
 *    baseline in a live demo (SIM-T6) to visually demonstrate the FPGA winning 
 *    as load scales up.
 *
 * DO NOT use this path for actual orchestration-time compute.
 * ==============================================================================
 */

#include "hyprccel.h"
#include "hyp_cordic_ref.h"

/**
 * hyp_esp32_compute
 * 
 * Executes an operation purely in software on the ESP32 CPU.
 * Uses the reference CORDIC implementation.
 */
void hyp_esp32_compute(hyp_op_t op, void *args) 
{
    if (!args) return;

    switch (op) {
        case HYP_OP_CORDIC_SINCOS: {
            hyp_cordic_args_t *cordic_args = (hyp_cordic_args_t *)args;
            hyp_cordic_ref(cordic_args->angle_degrees, 
                           &cordic_args->out_sin, 
                           &cordic_args->out_cos);
            break;
        }
        default:
            /* Unsupported operation for software baseline */
            break;
    }
}
