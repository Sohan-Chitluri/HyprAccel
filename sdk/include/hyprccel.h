/*
 * hyprccel.h  —  HyprAccel CORE-T1
 *
 * Public API surface for the HyprAccel SDK.
 * This is the contract that the Model-Based Design (MBD) codegen (MBD-T3)
 * will target, and the underlying software/hardware routing layer (CORE-T7)
 * must implement.
 */

#ifndef HYPRCCEL_H
#define HYPRCCEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/**
 * hyp_target_t — Execution backend selection
 */
typedef enum {
    HYP_TARGET_SOFTWARE = 0, /* Execute on host CPU (THEJAS or ESP32) */
    HYP_TARGET_HARDWARE = 1  /* Execute on FPGA accelerator */
} hyp_target_t;

/**
 * hyp_op_t — Supported computation operations
 *
 * The set of operations is currently fixed for V1 of the node vocabulary.
 */
typedef enum {
    HYP_OP_CORDIC_SINCOS = 0, /* Expects hyp_cordic_args_t */
    /* Room for KinematicsSolve, etc. */
} hyp_op_t;

/**
 * hyp_init — Initialize the HyprAccel platform
 *
 * Configures communication buses (e.g., SPI for FPGA) and sets up internal
 * routing state based on the board configuration.
 *
 * @return 0 on success, non-zero on failure.
 */
int hyp_init(void);

/**
 * hyp_route — Configure the execution target for a specific operation
 *
 * Directs subsequent calls to hyp_compute() for the given operation to 
 * execute on either software or hardware. This enables the "same graph, 
 * choose software or hardware" capability.
 *
 * @param op     The operation to route
 * @param target HYP_TARGET_SOFTWARE or HYP_TARGET_HARDWARE
 */
void hyp_route(hyp_op_t op, hyp_target_t target);

/**
 * hyp_compute — Execute a computation
 *
 * Dispatches the operation to the configured target (software or hardware)
 * transparently.
 *
 * @param op   The operation to execute
 * @param args Pointer to an operation-specific argument struct
 */
void hyp_compute(hyp_op_t op, void *args);

/**
 * hyp_publish — Publish telemetry or control data
 *
 * Routes data out of the compute graph (e.g., to a dashboard or Gazebo bridge).
 *
 * @param topic Null-terminated string identifying the data stream
 * @param data  Pointer to the payload
 * @param size  Size of the payload in bytes
 */
void hyp_publish(const char *topic, const void *data, uint32_t size);

/* -------------------------------------------------------------------------
 * Sensor / Actuator Primitives (Phase 1A)
 * ---------------------------------------------------------------------- */

/**
 * hyp_sensor_read — Read a sensor value from a configured hardware resource.
 *
 * The hardware resource is identified by a semantic hardware resource ID
 * (e.g., "adc.channel0", "i2c.imu", "uart.gps") which maps to board-specific
 * pin assignments in hyp_board_config.h.
 *
 * @param resource_id  Hardware resource identifier (e.g., "adc.channel0")
 * @param out_value    Pointer to output buffer for the read value
 * @param value_size   Size of the output buffer in bytes
 * @return 0 on success, negative error code on failure
 */
int hyp_sensor_read(const char *resource_id, void *out_value, uint32_t value_size);

/**
 * hyp_actuator_write — Write a command value to a configured actuator resource.
 *
 * The hardware resource is identified by a semantic hardware resource ID
 * (e.g., "pwm.motor0", "gpio.led", "spi.dac") which maps to board-specific
 * pin assignments in hyp_board_config.h.
 *
 * @param resource_id  Hardware resource identifier (e.g., "pwm.motor0")
 * @param in_value     Pointer to the input value to write
 * @param value_size   Size of the input value in bytes
 * @return 0 on success, negative error code on failure
 */
int hyp_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size);

/* -------------------------------------------------------------------------
 * Operation-specific argument structures
 * ---------------------------------------------------------------------- */

/**
 * Arguments for HYP_OP_CORDIC_SINCOS
 */
typedef struct {
    float angle_degrees; /* Input */
    float out_sin;       /* Output */
    float out_cos;       /* Output */
} hyp_cordic_args_t;

#ifdef __cplusplus
}
#endif

#endif /* HYPRCCEL_H */
