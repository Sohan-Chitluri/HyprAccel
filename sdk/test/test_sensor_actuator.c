/*
 * test_sensor_actuator.c — HyprAccel SDK-T1/T2 test harness
 *
 * Tests for hyp_sensor_read() and hyp_actuator_write() primitives
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../sdk/include/hyprccel.h"

/* Mock implementations for host testing */
static int mock_sensor_read_called = 0;
static int mock_actuator_write_called = 0;
static char last_sensor_resource[64] = {0};
static char last_actuator_resource[64] = {0};

int hyp_sensor_read(const char *resource_id, void *out_value, uint32_t value_size) {
    mock_sensor_read_called = 1;
    if (resource_id) strncpy(last_sensor_resource, resource_id, sizeof(last_sensor_resource) - 1);
    
    if (!resource_id || !out_value || value_size == 0) return HYP_RUNTIME_INVALID_ARGUMENT;
    
    if (strcmp(resource_id, "adc.GPIO32") == 0 && value_size == sizeof(float)) {
        *(float *)out_value = 3.3f; // Simulated ADC reading
        return 0;
    }
    if (strcmp(resource_id, "gpio.GPIO4") == 0 && value_size == sizeof(float)) {
        *(float *)out_value = 1.0f; // Simulated button pressed
        return 0;
    }
    return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
}

int hyp_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size) {
    mock_actuator_write_called = 1;
    if (resource_id) strncpy(last_actuator_resource, resource_id, sizeof(last_actuator_resource) - 1);
    
    if (!resource_id || !in_value || value_size == 0) return HYP_RUNTIME_INVALID_ARGUMENT;
    
    if (strcmp(resource_id, "pwm.GPIO25") == 0 && value_size == sizeof(float)) {
        float val = *(const float *)in_value;
        if (val >= 0.0f && val <= 1.0f) return HYP_RUNTIME_OK;
    }
    if (strcmp(resource_id, "gpio.GPIO4") == 0 && value_size == sizeof(uint8_t)) {
        uint8_t val = *(const uint8_t *)in_value;
        if (val == 0 || val == 1) return HYP_RUNTIME_OK;
    }
    return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
}

void hyp_publish(const char *topic, const void *data, uint32_t size) {
    (void)topic; (void)data; (void)size;
}

int hyp_init(void) { return 0; }
void hyp_route(hyp_op_t op, hyp_target_t target) { (void)op; (void)target; }
void hyp_compute(hyp_op_t op, void *args) { (void)op; (void)args; }

static void reset_mocks(void) {
    mock_sensor_read_called = 0;
    mock_actuator_write_called = 0;
    last_sensor_resource[0] = '\0';
    last_actuator_resource[0] = '\0';
}

static int test_sensor_read_valid_adc(void) {
    reset_mocks();
    float value = 0.0f;
    int result = hyp_sensor_read("adc.GPIO32", &value, sizeof(value));
    if (result != 0) return 1;
    if (!mock_sensor_read_called) return 2;
    if (strcmp(last_sensor_resource, "adc.GPIO32") != 0) return 3;
    if (value != 3.3f) return 4;
    return 0;
}

static int test_sensor_read_valid_gpio(void) {
    reset_mocks();
    float value = 0.0f;
    int result = hyp_sensor_read("gpio.GPIO4", &value, sizeof(value));
    if (result != 0) return 1;
    if (!mock_sensor_read_called) return 2;
    if (strcmp(last_sensor_resource, "gpio.GPIO4") != 0) return 3;
    if (value != 1.0f) return 4;
    return 0;
}

static int test_sensor_read_invalid_resource(void) {
    reset_mocks();
    float value = 0.0f;
    int result = hyp_sensor_read("spi.UNKNOWN", &value, sizeof(value));
    if (result >= 0) return 1;
    if (!mock_sensor_read_called) return 2;
    return 0;
}

static int test_sensor_read_null_args(void) {
    reset_mocks();
    int result = hyp_sensor_read(NULL, NULL, 0);
    if (result >= 0) return 1;
    return 0;
}

static int test_sensor_read_small_buffer(void) {
    reset_mocks();
    float value = 0.0f;
    int result = hyp_sensor_read("adc.GPIO32", &value, sizeof(uint8_t)); // Too small
    if (result >= 0) return 1;
    return 0;
}

static int test_actuator_write_valid_pwm(void) {
    reset_mocks();
    float value = 0.5f;
    int result = hyp_actuator_write("pwm.GPIO25", &value, sizeof(value));
    if (result != 0) return 1;
    if (!mock_actuator_write_called) return 2;
    if (strcmp(last_actuator_resource, "pwm.GPIO25") != 0) return 3;
    return 0;
}

static int test_actuator_write_valid_gpio(void) {
    reset_mocks();
    uint8_t value = 1;
    int result = hyp_actuator_write("gpio.GPIO4", &value, sizeof(value));
    if (result != 0) return 1;
    if (!mock_actuator_write_called) return 2;
    if (strcmp(last_actuator_resource, "gpio.GPIO4") != 0) return 3;
    return 0;
}

static int test_actuator_write_invalid_resource(void) {
    reset_mocks();
    float value = 0.5f;
    int result = hyp_actuator_write("i2c.UNKNOWN", &value, sizeof(value));
    if (result >= 0) return 1;
    if (!mock_actuator_write_called) return 2;
    return 0;
}

static int test_actuator_write_null_args(void) {
    reset_mocks();
    int result = hyp_actuator_write(NULL, NULL, 0);
    if (result >= 0) return 1;
    return 0;
}

static int test_actuator_write_out_of_range(void) {
    reset_mocks();
    float value = 2.0f; // Out of [-1, 1] range
    int result = hyp_actuator_write("pwm.GPIO25", &value, sizeof(value));
    if (result >= 0) return 1;
    return 0;
}

static int test_actuator_write_small_buffer(void) {
    reset_mocks();
    float value = 0.5f;
    int result = hyp_actuator_write("pwm.GPIO25", &value, sizeof(uint8_t)); // Too small
    if (result >= 0) return 1;
    return 0;
}

int main(void) {
    int passed = 0, failed = 0;
    
    printf("=== HyprAccel SDK Sensor/Actuator Primitive Tests ===\n\n");
    
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"sensor_read_valid_adc", test_sensor_read_valid_adc},
        {"sensor_read_valid_gpio", test_sensor_read_valid_gpio},
        {"sensor_read_invalid_resource", test_sensor_read_invalid_resource},
        {"sensor_read_null_args", test_sensor_read_null_args},
        {"sensor_read_small_buffer", test_sensor_read_small_buffer},
        {"actuator_write_valid_pwm", test_actuator_write_valid_pwm},
        {"actuator_write_valid_gpio", test_actuator_write_valid_gpio},
        {"actuator_write_invalid_resource", test_actuator_write_invalid_resource},
        {"actuator_write_null_args", test_actuator_write_null_args},
        {"actuator_write_out_of_range", test_actuator_write_out_of_range},
        {"actuator_write_small_buffer", test_actuator_write_small_buffer},
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
