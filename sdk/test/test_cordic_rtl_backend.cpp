#include "hyprccel.h"

#include <cmath>
#include <cstdio>

extern "C" void hyp_esp32_compute(hyp_op_t op, void* args);

struct test_case_t {
    float degrees;
    const char* name;
};

int main() {
    const test_case_t cases[] = {
        {0.0f, "0 degrees"},
        {30.0f, "30 degrees"},
        {45.0f, "45 degrees"},
        {90.0f, "90 degrees"},
        {180.0f, "180 degrees"},
        {-30.0f, "negative angle"},
        {23.456f, "non-round angle"},
    };
    constexpr float tolerance = 1.0f / 4096.0f;

    if (hyp_init() != 0) {
        std::fprintf(stderr, "FAIL: hyp_init failed\n");
        return 1;
    }

    for (const test_case_t& test : cases) {
        hyp_cordic_args_t hardware = {test.degrees, NAN, NAN};
        hyp_cordic_args_t software = {test.degrees, NAN, NAN};

        hyp_route(HYP_OP_CORDIC_SINCOS, HYP_TARGET_HARDWARE);
        hyp_compute(HYP_OP_CORDIC_SINCOS, &hardware);
        if (std::isnan(hardware.out_sin) || std::isnan(hardware.out_cos)) {
            std::fprintf(stderr, "FAIL %s: hardware RTL backend unavailable\n", test.name);
            return 1;
        }

        /* Comparison only: the preceding hardware call has already completed
         * through cordic_top. */
        hyp_esp32_compute(HYP_OP_CORDIC_SINCOS, &software);
        if (std::fabs(hardware.out_sin - software.out_sin) > tolerance ||
            std::fabs(hardware.out_cos - software.out_cos) > tolerance) {
            std::fprintf(stderr,
                         "FAIL %s: rtl=(%.6f, %.6f), software=(%.6f, %.6f)\n",
                         test.name, hardware.out_sin, hardware.out_cos,
                         software.out_sin, software.out_cos);
            return 1;
        }
        std::printf("PASS %s: sin=%.6f cos=%.6f\n",
                    test.name, hardware.out_sin, hardware.out_cos);
    }

    std::puts("PASS: HYP_TARGET_HARDWARE executed cordic_top via Verilator");
    return 0;
}
