/*
 * hyp_pid.c — HyprAccel SDK-T3
 *
 * Discrete PID controller implementation with anti-windup.
 */

#include "hyprccel.h"
#include <math.h>
#include <stdbool.h>

int hyp_pid_step(hyp_pid_state_t *state, float setpoint, float measurement, bool enable, float *out_command, float *out_error)
{
    if (!state || !out_command || !out_error) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }

    float error = setpoint - measurement;
    *out_error = error;

    if (!enable) {
        /* Controller disabled: reset state and output zero */
        state->integral = 0.0f;
        state->prev_error = 0.0f;
        state->initialized = false;
        *out_command = 0.0f;
        return HYP_RUNTIME_OK;
    }

    float dt = state->sample_period_s;
    if (dt <= 0.0f) {
        dt = 1.0f; /* Safe default if misconfigured */
    }

    if (!state->initialized) {
        /* First step: initialize state, no derivative term */
        state->integral = 0.0f;
        state->prev_error = error;
        state->initialized = true;
    }

    /* Compute terms with current integral */
    float proportional = state->kp * error;
    float derivative = state->kd * (error - state->prev_error) / dt;
    float output = proportional + state->ki * state->integral + derivative;

    /* Output saturation check (before integral update) */
    bool at_max = output >= state->output_max;
    bool at_min = output <= state->output_min;
    bool saturated = at_max || at_min;

    /* Anti-windup: conditional integration
     * Only accumulate integral if:
     * - Output is not saturated, OR
     * - Error would drive output away from saturation boundary
     */
    bool allow_integral = !saturated ||
                          (at_max && error < 0.0f) ||  // At upper limit, negative error reduces output
                          (at_min && error > 0.0f);    // At lower limit, positive error increases output

    if (allow_integral && state->ki != 0.0f) {
        state->integral += error * dt;
    }

    /* Recompute output with potentially updated integral */
    output = proportional + state->ki * state->integral + derivative;

    /* Final output saturation */
    if (output > state->output_max) {
        output = state->output_max;
    } else if (output < state->output_min) {
        output = state->output_min;
    }

    *out_command = output;
    state->prev_error = error;

    return HYP_RUNTIME_OK;
}