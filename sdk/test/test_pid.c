/*
 * test_pid.c — HyprAccel SDK-T3 test harness
 *
 * Tests for hyp_pid_step() primitive
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "../../sdk/include/hyprccel.h"

static int test_pid_basic_proportional(void) {
    hyp_pid_state_t state = {
        .kp = 1.0f,
        .ki = 0.0f,
        .kd = 0.0f,
        .sample_period_s = 0.001f,  // 1ms
        .output_min = -10.0f,
        .output_max = 10.0f,
        .integral = 0.0f,
        .prev_error = 0.0f,
        .initialized = false
    };
    
    float command, error;
    int result = hyp_pid_step(&state, 5.0f, 3.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    if (fabsf(error - 2.0f) > 0.001f) return 2;
    if (fabsf(command - 2.0f) > 0.001f) return 3;  // kp * error = 1.0 * 2.0
    return 0;
}

static int test_pid_integral_accumulation(void) {
    hyp_pid_state_t state = {
        .kp = 0.0f,
        .ki = 10.0f,
        .kd = 0.0f,
        .sample_period_s = 0.001f,
        .output_min = -10.0f,
        .output_max = 10.0f,
        .integral = 0.0f,
        .prev_error = 0.0f,
        .initialized = false
    };
    
    float command, error;
    // First step: error = 1.0, integral = 1.0 * 0.001 = 0.001
    int result = hyp_pid_step(&state, 1.0f, 0.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    if (fabsf(error - 1.0f) > 0.001f) return 2;
    // output = ki * integral = 10.0 * 0.001 = 0.01
    if (fabsf(command - 0.01f) > 0.001f) return 3;
    
    // Second step: error still 1.0, integral = 0.001 + 1.0 * 0.001 = 0.002
    result = hyp_pid_step(&state, 1.0f, 0.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 4;
    // output = 10.0 * 0.002 = 0.02
    if (fabsf(command - 0.02f) > 0.001f) return 5;
    return 0;
}

static int test_pid_derivative(void) {
    hyp_pid_state_t state = {
        .kp = 0.0f,
        .ki = 0.0f,
        .kd = 2.0f,
        .sample_period_s = 0.001f,
        .output_min = -10.0f,
        .output_max = 10.0f,
        .integral = 0.0f,
        .prev_error = 0.0f,
        .initialized = false
    };
    
    float command, error;
    // First step: error goes from 0 to 1.0, derivative = (1.0 - 0) / 0.001 = 1000
    int result = hyp_pid_step(&state, 1.0f, 0.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    // First step has no derivative (initialized = false)
    if (fabsf(command - 0.0f) > 0.001f) return 2;
    
    // Second step: error goes from 1.0 to 2.0, derivative = (2.0 - 1.0) / 0.001 = 1000
    result = hyp_pid_step(&state, 2.0f, 0.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 3;
    // output = kd * derivative = 2.0 * 1000 = 2000 (clamped to 10.0)
    if (fabsf(command - 10.0f) > 0.001f) return 4;
    return 0;
}

static int test_pid_anti_windup(void) {
    hyp_pid_state_t state = {
        .kp = 0.0f,
        .ki = 1000.0f,  // Large ki to force windup
        .kd = 0.0f,
        .sample_period_s = 0.001f,
        .output_min = -1.0f,
        .output_max = 1.0f,
        .integral = 0.0f,
        .prev_error = 0.0f,
        .initialized = false
    };
    
    float command, error;
    // First step: error = 10.0
    int result = hyp_pid_step(&state, 10.0f, 0.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    if (command > 1.0f) return 2;  // Should be clamped
    
    // Continue many steps - integral should NOT accumulate beyond saturation
    for (int i = 0; i < 100; i++) {
        result = hyp_pid_step(&state, 10.0f, 0.0f, true, &command, &error);
        if (result != HYP_RUNTIME_OK) return 3;
        if (command > 1.001f || command < 0.999f) return 4;  // Should stay at max
    }
    return 0;
}

static int test_pid_disable_reset(void) {
    hyp_pid_state_t state = {
        .kp = 1.0f,
        .ki = 1.0f,
        .kd = 1.0f,
        .sample_period_s = 0.001f,
        .output_min = -10.0f,
        .output_max = 10.0f,
        .integral = 5.0f,  // Pre-filled
        .prev_error = 2.0f,
        .initialized = true
    };
    
    float command, error;
    // Disable should reset state
    int result = hyp_pid_step(&state, 1.0f, 0.0f, false, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    if (command != 0.0f) return 2;
    if (state.integral != 0.0f) return 3;
    if (state.prev_error != 0.0f) return 4;
    if (state.initialized != false) return 5;
    return 0;
}

static int test_pid_output_saturation(void) {
    hyp_pid_state_t state = {
        .kp = 10.0f,
        .ki = 0.0f,
        .kd = 0.0f,
        .sample_period_s = 0.001f,
        .output_min = -5.0f,
        .output_max = 5.0f,
        .integral = 0.0f,
        .prev_error = 0.0f,
        .initialized = false
    };
    
    float command, error;
    // Large error should saturate
    int result = hyp_pid_step(&state, 10.0f, 0.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    if (fabsf(command - 5.0f) > 0.001f) return 2;  // Clamped to max
    
    // Negative saturation
    result = hyp_pid_step(&state, 0.0f, 10.0f, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 3;
    if (fabsf(command - (-5.0f)) > 0.001f) return 4;  // Clamped to min
    return 0;
}

static int test_pid_null_args(void) {
    hyp_pid_state_t state = {0};
    float command, error;
    
    if (hyp_pid_step(NULL, 1.0f, 0.0f, true, &command, &error) != HYP_RUNTIME_INVALID_ARGUMENT) return 1;
    if (hyp_pid_step(&state, 1.0f, 0.0f, true, NULL, &error) != HYP_RUNTIME_INVALID_ARGUMENT) return 2;
    if (hyp_pid_step(&state, 1.0f, 0.0f, true, &command, NULL) != HYP_RUNTIME_INVALID_ARGUMENT) return 3;
    return 0;
}

static int test_pid_complete_cycle(void) {
    // Simulate a typical PID control loop
    hyp_pid_state_t state = {
        .kp = 2.0f,
        .ki = 5.0f,
        .kd = 0.1f,
        .sample_period_s = 0.01f,  // 10ms
        .output_min = -100.0f,
        .output_max = 100.0f,
        .integral = 0.0f,
        .prev_error = 0.0f,
        .initialized = false
    };
    
    float command, error;
    float setpoint = 10.0f;
    float measurement = 0.0f;
    
    // Step 1: Large error
    int result = hyp_pid_step(&state, setpoint, measurement, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 1;
    if (fabsf(error - 10.0f) > 0.001f) return 2;
    
    // Step 2: Verify function runs and produces output
    measurement = 1.0f;  // Simulated movement toward setpoint
    result = hyp_pid_step(&state, setpoint, measurement, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 3;
    
    // Step 3: Continued operation
    measurement = 2.0f;
    result = hyp_pid_step(&state, setpoint, measurement, true, &command, &error);
    if (result != HYP_RUNTIME_OK) return 4;
    
    // Just verify it runs without error - actual control behavior depends on plant model
    return 0;
}

int main(void) {
    int passed = 0, failed = 0;
    
    printf("=== HyprAccel SDK PID Primitive Tests ===\n\n");
    
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"pid_basic_proportional", test_pid_basic_proportional},
        {"pid_integral_accumulation", test_pid_integral_accumulation},
        {"pid_derivative", test_pid_derivative},
        {"pid_anti_windup", test_pid_anti_windup},
        {"pid_disable_reset", test_pid_disable_reset},
        {"pid_output_saturation", test_pid_output_saturation},
        {"pid_null_args", test_pid_null_args},
        {"pid_complete_cycle", test_pid_complete_cycle},
    };
    
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        int result = tests[i].fn();
        if (result == 0) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s (code %d)\n", tests[i].name, result);
            failed++;
        }
    }
    
    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}