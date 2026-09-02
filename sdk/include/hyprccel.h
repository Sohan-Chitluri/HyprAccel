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
#include <stdbool.h>

/* Runtime primitive result codes. Zero means the operation completed. */
#define HYP_RUNTIME_OK                       0
#define HYP_RUNTIME_INVALID_ARGUMENT       -1
#define HYP_RUNTIME_INVALID_RESOURCE_ID    -2
#define HYP_RUNTIME_RESOURCE_NOT_CONFIGURED -3
#define HYP_RUNTIME_BUFFER_TOO_SMALL       -4
#define HYP_RUNTIME_UNSUPPORTED_RESOURCE   -5
#define HYP_RUNTIME_UNSUPPORTED_INSTANCE  -6
#define HYP_RUNTIME_IO_ERROR               -7  /* Bus transaction failed: I2C NACK, timeout, or incomplete transfer */
#define HYP_RUNTIME_NOT_INITIALIZED        -8  /* Resource is configured in the board but its peripheral was not initialized at runtime */

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
 * @return HYP_RUNTIME_OK on success, otherwise a HYP_RUNTIME_* error code
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
 * @return HYP_RUNTIME_OK on success, otherwise a HYP_RUNTIME_* error code
 */
int hyp_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size);

/* -------------------------------------------------------------------------
 * SPI / I2C Bus Transaction Primitives (SDK-T6)
 * ---------------------------------------------------------------------- */

/**
 * hyp_spi_transfer — Perform a full-duplex SPI transaction on a configured bus.
 *
 * The bus is identified by a semantic resource ID of the form "spi.<instance>"
 * (e.g. "spi.hspi", "spi.vspi").  The instance's chip-select (CS) pin, clock
 * frequency, and SPI mode are resolved from the generated hyp_board_config.h
 * macros, following the same resource-resolution pattern used for GPIO/ADC/PWM.
 *
 * For each of the `len` bytes: the byte from tx_buf (or 0x00 when tx_buf is
 * NULL) is clocked out while the simultaneously received byte is stored in
 * rx_buf (when rx_buf is non-NULL).  CS is asserted low for the duration of the
 * transfer and released high afterwards.
 *
 * @param resource_id  Bus resource identifier (e.g. "spi.hspi")
 * @param tx_buf       Bytes to transmit, or NULL to clock out zeros (read-only)
 * @param rx_buf       Buffer for received bytes, or NULL to discard (write-only)
 * @param len          Number of bytes to transfer (must be > 0)
 * @return HYP_RUNTIME_OK on success, otherwise a HYP_RUNTIME_* error code.
 *         HYP_RUNTIME_NOT_INITIALIZED if the bus was never initialized.
 */
int hyp_spi_transfer(const char *resource_id, const uint8_t *tx_buf, uint8_t *rx_buf, size_t len);

/**
 * hyp_i2c_transact — Register-addressed I2C read or write to a named device.
 *
 * The device is identified by a semantic resource ID of the form
 * "i2c.<device>" (e.g. "i2c.imu").  The device's 7-bit slave address is
 * resolved from the generated HYP_RESOURCE_I2C_<DEVICE>_ADDRESS macros; the
 * underlying bus must have been initialized (Wire.begin) at startup.
 *
 * Write (is_write=true): sends reg_addr followed by `len` bytes from `data`.
 * Read  (is_write=false): sends reg_addr, issues a repeated start, then reads
 * `len` bytes into `data`.
 *
 * @param resource_id  Device resource identifier (e.g. "i2c.imu")
 * @param reg_addr     8-bit register address within the device
 * @param data         Data buffer to write from / read into (must be non-NULL)
 * @param len          Number of data bytes (must be > 0, bounded by bus buffer)
 * @param is_write     true = write to register, false = read from register
 * @return HYP_RUNTIME_OK on success, otherwise a HYP_RUNTIME_* error code.
 *         HYP_RUNTIME_IO_ERROR on NACK, timeout, or incomplete transfer.
 */
int hyp_i2c_transact(const char *resource_id, uint8_t reg_addr, uint8_t *data, size_t len, bool is_write);

/**
 * hyp_timestamp_us — Return the runtime monotonic timestamp in microseconds.
 *
 * Graph code uses this SDK primitive rather than a target framework clock API.
 */
uint32_t hyp_timestamp_us(void);

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

/* -------------------------------------------------------------------------
 * PID Control Loop Primitive (SDK-T3)
 * ---------------------------------------------------------------------- */

/**
 * PID Controller State
 * 
 * Maintains internal state for discrete PID computation with anti-windup.
 * State is persisted across step() calls.
 */
typedef struct {
    float kp;              // Proportional gain
    float ki;              // Integral gain
    float kd;              // Derivative gain
    float sample_period_s; // Sample period in seconds (samplePeriodUs / 1e6)
    float output_min;      // Output saturation minimum
    float output_max;      // Output saturation maximum
    float integral;        // Integrated error (state)
    float prev_error;      // Previous error for derivative term (state)
    bool initialized;      // First-step flag
} hyp_pid_state_t;

/**
 * hyp_pid_step — Discrete PID step with anti-windup
 * 
 * Computes one PID control cycle:
 *   error = setpoint - measurement
 *   integral += error * dt (with anti-windup clamping)
 *   derivative = (error - prev_error) / dt
 *   output = kp*error + ki*integral + kd*derivative
 *   output clamped to [output_min, output_max]
 * 
 * @param state      PID state structure (modified)
 * @param setpoint   Desired target value
 * @param measurement Current measured value
 * @param enable     Enable/disable the controller (false = reset state)
 * @param out_command Output command value
 * @param out_error   Current error value (setpoint - measurement)
 * @return HYP_RUNTIME_OK on success, HYP_RUNTIME_INVALID_ARGUMENT if pointers are NULL
 */
int hyp_pid_step(hyp_pid_state_t *state, float setpoint, float measurement, bool enable, float *out_command, float *out_error);

/* -------------------------------------------------------------------------
 * Encoder Primitive (SDK-T5)
 * ---------------------------------------------------------------------- */

/**
 * hyp_encoder_read — Read position and velocity from a quadrature encoder.
 * 
 * The hardware resource is identified by a semantic hardware resource ID
 * (e.g., "encoder.wheel0") which maps to board-specific pin assignments
 * in hyp_board_config.h.
 * 
 * @param resource_id   Hardware resource identifier (e.g., "encoder.wheel0")
 * @param out_position  Pointer to output buffer for position (int32_t*), or NULL
 * @param out_velocity  Pointer to output buffer for velocity (float*), or NULL
 * @return HYP_RUNTIME_OK on success, otherwise a HYP_RUNTIME_* error code
 */
int hyp_encoder_read(const char *resource_id, int32_t *out_position, float *out_velocity);

/**
 * hyp_encoder_init — Initialize an encoder resource.
 * 
 * @param resource_id  Hardware resource identifier (e.g., "encoder.wheel0")
 * @param pulses_per_revolution  Pulses per revolution of the encoder
 * @param quadrature  Whether to use quadrature mode (2x resolution)
 * @return HYP_RUNTIME_OK on success, otherwise a HYP_RUNTIME_* error code
 */
int hyp_encoder_init(const char *resource_id, int pulses_per_revolution, bool quadrature);

/* Internal helper for parsing resource IDs (exposed for encoder implementation) */
int parse_resource_id(const char *resource_id, char *type_out, size_t type_size, char *instance_out, size_t instance_size);

#ifdef __cplusplus
}
#endif

#endif /* HYPRCCEL_H */
