/*
 * Host-side bridge to the canonical SystemVerilog cordic_top module.
 * No CORDIC arithmetic is implemented here: the only CORDIC execution is the
 * Verilated RTL.  The small helpers only adapt the established SDK degree API
 * to the RTL rotation-mode Q4.12 input convention.
 */
#include "hyp_cordic_rtl_backend.h"

#include "Vcordic_top.h"
#include "verilated.h"

#include <cstdint>

namespace {
constexpr int kQ12Scale = 4096;
constexpr int kFixedIterations = 8;
constexpr int kMaxWaitCycles = 32;

void tick(Vcordic_top& dut) {
    dut.clk = 0;
    dut.eval();
    dut.clk = 1;
    dut.eval();
}

/* This is the SDK's existing rotation-mode range reduction convention from
 * hyp_cordic_ref.c. It does not evaluate a CORDIC or use trigonometric APIs. */
void prepare_rotation(float angle_degrees, int16_t* z_in,
                      bool* negate_sin, bool* negate_cos) {
    while (angle_degrees < 0.0f) angle_degrees += 360.0f;
    while (angle_degrees >= 360.0f) angle_degrees -= 360.0f;

    *negate_sin = false;
    *negate_cos = false;
    if (angle_degrees <= 90.0f) {
        /* First quadrant. */
    } else if (angle_degrees <= 180.0f) {
        angle_degrees = 180.0f - angle_degrees;
        *negate_cos = true;
    } else if (angle_degrees <= 270.0f) {
        angle_degrees -= 180.0f;
        *negate_sin = true;
        *negate_cos = true;
    } else {
        angle_degrees = 360.0f - angle_degrees;
        *negate_sin = true;
    }

    *z_in = static_cast<int16_t>(static_cast<int32_t>(angle_degrees * 71.488686f));
}

void reset_and_configure(Vcordic_top& dut) {
    dut.clk = 0;
    dut.rst_n = 0;
    dut.x_in = 0;
    dut.y_in = 0;
    dut.z_in = 0;
    dut.valid_in = 0;
    dut.ready_in = 1;
    dut.cfg_iterations = kFixedIterations;
    dut.cfg_saturate = 1;
    dut.config_valid = 0;
    dut.eval();
    tick(dut);
    tick(dut);

    dut.rst_n = 1;
    dut.config_valid = 1;
    tick(dut);                 // capture cfg_saturate before the transaction
    dut.config_valid = 0;
    tick(dut);
}
}  // namespace

extern "C" hyp_cordic_rtl_status_t hyp_cordic_rtl_compute(hyp_op_t op, void *args) {
    if (op != HYP_OP_CORDIC_SINCOS) return HYP_CORDIC_RTL_UNSUPPORTED_OPERATION;
    if (args == nullptr) return HYP_CORDIC_RTL_INVALID_ARGUMENT;

    hyp_cordic_args_t* cordic_args = static_cast<hyp_cordic_args_t*>(args);
    int16_t z_in;
    bool negate_sin;
    bool negate_cos;
    prepare_rotation(cordic_args->angle_degrees, &z_in, &negate_sin, &negate_cos);

    Vcordic_top dut;
    reset_and_configure(dut);

    /* One in-flight transaction, permanently-ready output consumer. */
    dut.x_in = static_cast<uint16_t>(kQ12Scale);
    dut.y_in = 0;
    dut.z_in = static_cast<uint16_t>(z_in);
    dut.valid_in = 1;
    tick(dut);
    dut.valid_in = 0;

    for (int cycle = 0; cycle < kMaxWaitCycles; ++cycle) {
        tick(dut);
        if (dut.valid_out) {
            float cos_value = static_cast<float>(static_cast<int16_t>(dut.x_out)) / kQ12Scale;
            float sin_value = static_cast<float>(static_cast<int16_t>(dut.y_out)) / kQ12Scale;
            cordic_args->out_sin = negate_sin ? -sin_value : sin_value;
            cordic_args->out_cos = negate_cos ? -cos_value : cos_value;
            dut.final();
            return HYP_CORDIC_RTL_SUCCESS;
        }
    }

    dut.final();
    return HYP_CORDIC_RTL_TIMEOUT;
}
