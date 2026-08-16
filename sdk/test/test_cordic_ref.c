/*
 * test_cordic_ref.c  —  HyprAccel CORE-T4 test harness
 *
 * Feature-test macros must appear before any system header.
 * _POSIX_C_SOURCE 200809L: enables clock_gettime(), struct timespec, CLOCK_MONOTONIC.
 * _GNU_SOURCE:             ensures M_PI is available from <math.h> on glibc.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#include "../src/hyp_cordic_ref.h"

/* 
 * Maximum tolerated absolute error vs math.h.
 * For 8-iteration CORDIC, precision is significantly lower than 16 iterations.
 * Worst-case error approaches ~1-2% for certain angles.
 * We set threshold to 1.5e-2. 
 */
#define ERROR_THRESHOLD  1.5e-2f

#define DEFAULT_BENCH_N  10000000UL

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int mode_test(int verbose)
{
    float max_sin_err  = 0.0f, max_cos_err  = 0.0f;
    float sum_sin_err  = 0.0f, sum_cos_err  = 0.0f;
    float worst_sin_angle = 0.0f, worst_cos_angle = 0.0f;
    int   n = 0;

    printf("=== HyprAccel CORE-T4 — CORDIC accuracy test ===\n");
    printf("Sweep: 0° to 360° in 1° steps\n");
    printf("Format: Q4.12 | Iterations: 8\n");
    printf("Error threshold: %.2e\n\n", (double)ERROR_THRESHOLD);

    if (verbose) {
        printf("%-8s  %-12s  %-12s  %-12s  %-12s  %-12s  %-12s\n",
               "angle", "ref_sin", "ref_cos", "math_sin", "math_cos", "sin_err", "cos_err");
    }

    for (int deg = 0; deg <= 360; deg++) {
        float angle = (float)deg;
        float ref_sin, ref_cos;

        hyp_cordic_ref(angle, &ref_sin, &ref_cos);

        double rad = (double)deg * M_PI / 180.0;
        float math_sin = (float)sin(rad);
        float math_cos = (float)cos(rad);

        float sin_err = fabsf(ref_sin - math_sin);
        float cos_err = fabsf(ref_cos - math_cos);

        if (sin_err > max_sin_err) { max_sin_err = sin_err; worst_sin_angle = angle; }
        if (cos_err > max_cos_err) { max_cos_err = cos_err; worst_cos_angle = angle; }
        sum_sin_err += sin_err;
        sum_cos_err += cos_err;
        n++;

        if (verbose && (sin_err > 5e-3f || cos_err > 5e-3f)) {
            printf("%-8.1f  %-+12.6f  %-+12.6f  %-+12.6f  %-+12.6f  %-12.2e  %-12.2e\n",
                   (double)angle, (double)ref_sin, (double)ref_cos,
                   (double)math_sin, (double)math_cos, (double)sin_err, (double)cos_err);
        }
    }

    printf("\n--- Results ---\n");
    printf("  Max |sin error|:  %.2e  at %.1f°\n", (double)max_sin_err, (double)worst_sin_angle);
    printf("  Max |cos error|:  %.2e  at %.1f°\n", (double)max_cos_err, (double)worst_cos_angle);
    printf("  Mean |sin error|: %.2e\n", (double)(sum_sin_err / n));
    printf("  Mean |cos error|: %.2e\n", (double)(sum_cos_err / n));
    printf("  Threshold:        %.2e\n", (double)ERROR_THRESHOLD);

    int pass = (max_sin_err <= ERROR_THRESHOLD && max_cos_err <= ERROR_THRESHOLD);
    printf("\n  [%s] CORDIC accuracy test %s\n\n", pass ? "PASS" : "FAIL", pass ? "passed" : "FAILED");
    return pass ? 0 : 1;
}

static void mode_bench(unsigned long n)
{
    float angle = 23.456f;
    volatile float sink_sin, sink_cos;
    uint64_t t0 = now_ns();
    for (unsigned long i = 0; i < n; i++) {
        float s, c;
        hyp_cordic_ref(angle, &s, &c);
        sink_sin = s; sink_cos = c;
    }
    uint64_t t1 = now_ns();
    (void)sink_sin; (void)sink_cos;
    double per_call_ns = (double)(t1 - t0) / (double)n;
    printf("=== CORDIC Benchmark ===\n");
    printf("  Iterations: %lu\n  Total: %.3f ms\n  Per call: %.2f ns\n", n, (t1 - t0)/1e6, per_call_ns);
}

/* 
 * Naive custom JSON parser to validate against regression_10k.json 
 * Format is strict from python script.
 */
static int mode_validate(const char *json_path) 
{
    FILE *f = fopen(json_path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", json_path);
        return 1;
    }
    printf("=== HyprAccel CORE-T4 — Bit-Exact Vector Validation ===\n");
    
    int passes = 0;
    int fails = 0;
    char line[256];
    
    int32_t x_in = 0, y_in = 0, z_in = 0, saturate = 0;
    int32_t exp_x = 0, exp_y = 0, exp_z = 0, exp_ovf = 0;
    int vector_ready = 0;
    
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if      (strstr(p, "\"x_in\":"))        { sscanf(p, "\"x_in\": %d", &x_in); }
        else if (strstr(p, "\"y_in\":"))        { sscanf(p, "\"y_in\": %d", &y_in); }
        else if (strstr(p, "\"z_in\":"))        { sscanf(p, "\"z_in\": %d", &z_in); }
        else if (strstr(p, "\"cfg_saturate\":")) { sscanf(p, "\"cfg_saturate\": %d", &saturate); }
        else if (strstr(p, "\"x_out\":"))       { sscanf(p, "\"x_out\": %d", &exp_x); }
        else if (strstr(p, "\"y_out\":"))       { sscanf(p, "\"y_out\": %d", &exp_y); }
        else if (strstr(p, "\"z_out\":"))       { sscanf(p, "\"z_out\": %d", &exp_z); }
        else if (strstr(p, "\"overflow\":"))    {
            /* overflow is always the last field in expected{} — use it as the
               commit signal to run the core and compare. */
            sscanf(p, "\"overflow\": %d", &exp_ovf);
            vector_ready = 1;
        }

        if (vector_ready) {
            vector_ready = 0;
            int32_t act_x, act_y, act_z;
            int act_ovf;
            hyp_cordic_core(x_in, y_in, z_in, saturate, &act_x, &act_y, &act_z, &act_ovf);
            
            if (act_x == exp_x && act_y == exp_y && act_z == exp_z && act_ovf == exp_ovf) {
                passes++;
            } else {
                fails++;
                if (fails <= 5) {
                    printf("Mismatch at vector %d:\n", passes + fails);
                    printf("  Input:  x=%d y=%d z=%d sat=%d\n", x_in, y_in, z_in, saturate);
                    printf("  Expect: x=%d y=%d z=%d ovf=%d\n", exp_x, exp_y, exp_z, exp_ovf);
                    printf("  Actual: x=%d y=%d z=%d ovf=%d\n", act_x, act_y, act_z, act_ovf);
                }
            }
        }
    }
    
    fclose(f);
    
    printf("Processed %d vectors.\n", passes + fails);
    printf("  Pass: %d\n  Fail: %d\n", passes, fails);
    if (fails == 0) {
        printf("\n  [PASS] Bit-exact validation successful!\n\n");
        return 0;
    } else {
        printf("\n  [FAIL] Bit-exact validation failed.\n\n");
        return 1;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) return 1;

    if (strcmp(argv[1], "--test") == 0) {
        int verbose = (argc >= 3 && strcmp(argv[2], "--verbose") == 0);
        return mode_test(verbose);
    } else if (strcmp(argv[1], "--bench") == 0) {
        mode_bench(DEFAULT_BENCH_N);
        return 0;
    } else if (strcmp(argv[1], "--validate") == 0) {
        if (argc < 3) return 1;
        return mode_validate(argv[2]);
    }
    return 1;
}
