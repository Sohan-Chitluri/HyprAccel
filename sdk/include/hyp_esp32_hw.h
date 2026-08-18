/*
 * hyp_esp32_hw.h
 *
 * ESP32 Hardware Initialization Layer
 * Part of the HyprAccel SDK.
 */

#ifndef HYP_ESP32_HW_H
#define HYP_ESP32_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * hyp_esp32_hw_init
 *
 * Initializes the ESP32 hardware peripherals (GPIO, UART, SPI, I2C, PWM, ADC)
 * based on the generated configuration in hyp_board_config.h.
 *
 * @return 0 on success, non-zero on failure.
 */
int hyp_esp32_hw_init(void);

/**
 * hyp_esp32_sensor_read
 *
 * Reads a sensor value from a configured hardware resource.
 * The resource_id must match a hardware resource defined in hyp_board_config.h
 * (e.g., "adc.channel0", "gpio.button0", "i2c.imu", "uart.gps").
 *
 * @param resource_id  Hardware resource identifier
 * @param out_value    Pointer to output buffer for the read value
 * @param value_size   Size of the output buffer in bytes
 * @return 0 on success, negative error code on failure
 */
int hyp_esp32_sensor_read(const char *resource_id, void *out_value, uint32_t value_size);

/**
 * hyp_esp32_actuator_write
 *
 * Writes a command value to a configured actuator resource.
 * The resource_id must match a hardware resource defined in hyp_board_config.h
 * (e.g., "pwm.motor0", "gpio.led", "spi.dac", "uart.actuator").
 *
 * @param resource_id  Hardware resource identifier
 * @param in_value     Pointer to the input value to write
 * @param value_size   Size of the input value in bytes
 * @return 0 on success, negative error code on failure
 */
int hyp_esp32_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size);

#ifdef __cplusplus
}
#endif

#endif /* HYP_ESP32_HW_H */
