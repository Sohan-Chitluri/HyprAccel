/*
 * PLAT-T1 software telemetry source.
 *
 * It deliberately measures the public SDK software route, rather than calling
 * the CORDIC reference implementation directly. Hardware measurements belong
 * here only after RTL-T4 provides a real driver.
 */

#define _POSIX_C_SOURCE 200809L

#include "hyprccel.h"

#include <stdio.h>
#include <time.h>

static double monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ((double)now.tv_sec * 1000000.0) + ((double)now.tv_nsec / 1000.0);
}

static void sleep_ms(long milliseconds)
{
    struct timespec duration = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L,
    };
    nanosleep(&duration, NULL);
}

int main(void)
{
    double total_latency_us = 0.0;
    unsigned long sample_count = 0;
    float angle_degrees = 0.0f;

    if (hyp_init() != 0) {
        fprintf(stderr, "hyp_init failed\n");
        return 1;
    }

    hyp_route(HYP_OP_CORDIC_SINCOS, HYP_TARGET_SOFTWARE);

    for (;;) {
        hyp_cordic_args_t args = { .angle_degrees = angle_degrees };
        const double started_us = monotonic_us();
        hyp_compute(HYP_OP_CORDIC_SINCOS, &args);
        const double latency_us = monotonic_us() - started_us;

        total_latency_us += latency_us;
        sample_count++;
        printf(
            "{\"angle_degrees\":%.3f,\"sin\":%.7f,\"cos\":%.7f,"
            "\"target\":\"HYP_TARGET_SOFTWARE\",\"path\":\"software baseline\","
            "\"latency_us\":%.3f,\"average_latency_us\":%.3f}\n",
            args.angle_degrees, args.out_sin, args.out_cos, latency_us,
            total_latency_us / (double)sample_count);
        fflush(stdout);

        angle_degrees += 7.5f;
        if (angle_degrees >= 360.0f) angle_degrees -= 360.0f;
        sleep_ms(250);
    }
}
