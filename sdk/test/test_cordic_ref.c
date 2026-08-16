/*
 * test_cordic_ref.c  —  HyprAccel CORE-T4 test harness
 *
 * Feature-test macros must appear before any system header.
 * _POSIX_C_SOURCE 200809L: enables clock_gettime(), struct timespec, CLOCK_MONOTONIC.
 * _GNU_SOURCE:             ensures M_PI is available from <math.h> on glibc.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/*
 * Two modes:
 *   --test          Accuracy sweep: 0–360° in 1° steps, compare vs math.h.
 *                   Reports max absolute error, mean absolute error, and
 *                   the worst-case angle.  Exit 0 on pass, 1 on failure.
 *
 *   --bench [N]     Benchmark: run N calls to hyp_cordic_ref() and report
 *                   total wall-clock time and nanoseconds per call.
 *                   Default N = 10,000,000.  Exit 0 always.
 *
 * This harness is also used by RTL-T1: the same angle table and expected
 * values from --test become the testbench stimulus for the Verilog CORDIC.
 * Run with --dump to print angle/sin/cos/expected_sin/expected_cos as CSV
 * (suitable for feeding directly to a Verilog $readmemh stimulus file
 * after conversion).
 *
 * Build:
 *   gcc -O2 -o test_cordic test_cordic_ref.c ../src/hyp_cordic_ref.c -lm
 *
 * Usage:
 *   ./test_cordic --test
 *   ./test_cordic --bench 10000000
 *   ./test_cordic --dump > stimulus.csv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#include "../src/hyp_cordic_ref.h"

/* Maximum tolerated absolute error vs math.h (sin or cos).
 * Q1.15 resolution is 1/32768 ≈ 3e-5; we allow 5× headroom for
 * rounding accumulated over 16 iterations. */
#define ERROR_THRESHOLD  2e-4f

/* Default benchmark iteration count */
#define DEFAULT_BENCH_N  10000000UL

/* -------------------------------------------------------------------------
 * Utility: wall-clock time in nanoseconds (POSIX CLOCK_MONOTONIC)
 * ---------------------------------------------------------------------- */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* -------------------------------------------------------------------------
 * mode_test()
 *
 * Sweep 0–360° in 1° steps.  Compare hyp_cordic_ref() against math.h.
 * Print a table header, then per-angle results for any angle with error
 * above 1e-5 (verbose=1) or just the summary (verbose=0).
 *
 * Returns 0 if max error <= ERROR_THRESHOLD, 1 otherwise.
 * ---------------------------------------------------------------------- */
static int mode_test(int verbose)
{
    float max_sin_err  = 0.0f;
    float max_cos_err  = 0.0f;
    float sum_sin_err  = 0.0f;
    float sum_cos_err  = 0.0f;
    float worst_sin_angle = 0.0f;
    float worst_cos_angle = 0.0f;
    int   n = 0;

    printf("=== HyprAccel CORE-T4 — CORDIC accuracy test ===\n");
    printf("Sweep: 0° to 360° in 1° steps (%d points)\n", 361);
    printf("Fixed-point format: Q1.15  |  Iterations: 16\n");
    printf("Error threshold: %.1e\n\n", (double)ERROR_THRESHOLD);

    if (verbose) {
        printf("%-8s  %-12s  %-12s  %-12s  %-12s  %-12s  %-12s\n",
               "angle", "ref_sin", "ref_cos", "math_sin", "math_cos",
               "sin_err", "cos_err");
        printf("%-8s  %-12s  %-12s  %-12s  %-12s  %-12s  %-12s\n",
               "------", "-------", "-------", "--------", "--------",
               "-------", "-------");
    }

    for (int deg = 0; deg <= 360; deg++) {
        float angle = (float)deg;
        float ref_sin, ref_cos;

        hyp_cordic_ref(angle, &ref_sin, &ref_cos);

        double rad       = (double)deg * M_PI / 180.0;
        float  math_sin  = (float)sin(rad);
        float  math_cos  = (float)cos(rad);

        float  sin_err   = fabsf(ref_sin - math_sin);
        float  cos_err   = fabsf(ref_cos - math_cos);

        if (sin_err > max_sin_err) { max_sin_err = sin_err; worst_sin_angle = angle; }
        if (cos_err > max_cos_err) { max_cos_err = cos_err; worst_cos_angle = angle; }
        sum_sin_err += sin_err;
        sum_cos_err += cos_err;
        n++;

        if (verbose && (sin_err > 1e-5f || cos_err > 1e-5f)) {
            printf("%-8.1f  %-+12.6f  %-+12.6f  %-+12.6f  %-+12.6f  %-12.2e  %-12.2e\n",
                   (double)angle,
                   (double)ref_sin, (double)ref_cos,
                   (double)math_sin, (double)math_cos,
                   (double)sin_err, (double)cos_err);
        }
    }

    float mean_sin_err = sum_sin_err / (float)n;
    float mean_cos_err = sum_cos_err / (float)n;

    printf("\n--- Results ---\n");
    printf("  Max |sin error|:  %.2e  at %.1f°\n",
           (double)max_sin_err, (double)worst_sin_angle);
    printf("  Max |cos error|:  %.2e  at %.1f°\n",
           (double)max_cos_err, (double)worst_cos_angle);
    printf("  Mean |sin error|: %.2e\n", (double)mean_sin_err);
    printf("  Mean |cos error|: %.2e\n", (double)mean_cos_err);
    printf("  Threshold:        %.2e\n", (double)ERROR_THRESHOLD);

    int pass = (max_sin_err <= ERROR_THRESHOLD && max_cos_err <= ERROR_THRESHOLD);
    printf("\n  [%s] CORDIC accuracy test %s\n\n",
           pass ? "PASS" : "FAIL",
           pass ? "passed" : "FAILED — max error exceeds threshold");

    return pass ? 0 : 1;
}

/* -------------------------------------------------------------------------
 * mode_bench()
 *
 * Run N calls to hyp_cordic_ref() at a fixed angle (23.456°, chosen to
 * exercise the inner-loop non-trivially).  Measure wall-clock time.
 * Use a volatile sink to prevent the compiler from optimising calls away.
 * ---------------------------------------------------------------------- */
static void mode_bench(unsigned long n)
{
    /* Angle chosen to avoid trivial cases (0°, 90°, 180°, 270°) */
    float angle = 23.456f;
    volatile float sink_sin, sink_cos;

    printf("=== HyprAccel CORE-T4 — CORDIC benchmark ===\n");
    printf("Angle: %.3f°  |  Iterations: %lu\n\n", (double)angle, n);

    uint64_t t0 = now_ns();

    for (unsigned long i = 0; i < n; i++) {
        float s, c;
        hyp_cordic_ref(angle, &s, &c);
        sink_sin = s;
        sink_cos = c;
    }

    uint64_t t1 = now_ns();
    (void)sink_sin; (void)sink_cos;   /* suppress unused-variable warning */

    uint64_t total_ns = t1 - t0;
    double   per_call_ns = (double)total_ns / (double)n;

    printf("  Iterations:    %lu\n",       n);
    printf("  Total time:    %.3f ms\n",   (double)total_ns / 1e6);
    printf("  Per call:      %.2f ns\n",   per_call_ns);
    printf("  Throughput:    %.2f Mops/s\n", 1e9 / per_call_ns / 1e6);
    printf("\n[NOTE] This is the software baseline.  Compare against RTL-T5\n");
    printf("       FPGA latency numbers to compute the hardware advantage.\n\n");
}

/* -------------------------------------------------------------------------
 * mode_dump()
 *
 * Print angle/ref_sin/ref_cos/math_sin/math_cos as CSV.
 * Useful for generating Verilog testbench stimulus (RTL-T1).
 * Redirect stdout to a file: ./test_cordic --dump > stimulus.csv
 * ---------------------------------------------------------------------- */
static void mode_dump(void)
{
    printf("angle_deg,cordic_sin,cordic_cos,math_sin,math_cos,sin_err,cos_err\n");

    for (int deg = 0; deg <= 360; deg++) {
        float angle = (float)deg;
        float ref_sin, ref_cos;

        hyp_cordic_ref(angle, &ref_sin, &ref_cos);

        double rad      = (double)deg * M_PI / 180.0;
        float  math_sin = (float)sin(rad);
        float  math_cos = (float)cos(rad);
        float  sin_err  = fabsf(ref_sin - math_sin);
        float  cos_err  = fabsf(ref_cos - math_cos);

        printf("%.1f,%.8f,%.8f,%.8f,%.8f,%.2e,%.2e\n",
               (double)angle,
               (double)ref_sin, (double)ref_cos,
               (double)math_sin, (double)math_cos,
               (double)sin_err, (double)cos_err);
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --test [--verbose]         Accuracy test (0-360 degrees)\n", prog);
    fprintf(stderr, "  %s --bench [N]                Benchmark N calls (default %lu)\n",
            prog, DEFAULT_BENCH_N);
    fprintf(stderr, "  %s --dump                     CSV dump for RTL testbench\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--test") == 0) {
        int verbose = (argc >= 3 && strcmp(argv[2], "--verbose") == 0);
        return mode_test(verbose);
    }
    else if (strcmp(argv[1], "--bench") == 0) {
        unsigned long n = DEFAULT_BENCH_N;
        if (argc >= 3) {
            char *end;
            unsigned long parsed = strtoul(argv[2], &end, 10);
            if (*end == '\0' && parsed > 0) n = parsed;
        }
        mode_bench(n);
        return 0;
    }
    else if (strcmp(argv[1], "--dump") == 0) {
        mode_dump();
        return 0;
    }
    else {
        fprintf(stderr, "Unknown option: %s\n\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
}
