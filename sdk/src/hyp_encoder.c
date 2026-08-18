/*
 * hyp_encoder.c — HyprAccel SDK-T5
 *
 * Encoder/PCNT implementation for ESP32.
 * Host test build provides mock implementation.
 */

#include "hyprccel.h"
#include <string.h>

#if defined(ARDUINO) || defined(ESP_IDF_VERSION)
/* ESP32 target build - include ESP-IDF headers */
#include "hyp_esp32_hw.h"
#include <driver/pcnt.h>

/* Encoder instance state */
typedef struct {
    pcnt_unit_t unit;
    int32_t prev_count;
    uint32_t prev_timestamp_us;
    bool initialized;
    int pulses_per_revolution;
    bool quadrature;
} hyp_encoder_instance_t;

/* Maximum 4 encoder units on ESP32 */
static hyp_encoder_instance_t encoder_instances[PCNT_UNIT_MAX] = {0};
static bool encoder_initialized[PCNT_UNIT_MAX] = {false};

/* Map resource ID to PCNT unit */
static int encoder_unit_for_resource(const char *instance) {
    if (strcmp(instance, "ENCODER0") == 0) return 0;
    if (strcmp(instance, "ENCODER1") == 0) return 1;
    if (strcmp(instance, "ENCODER2") == 0) return 2;
    if (strcmp(instance, "ENCODER3") == 0) return 3;
    return -1;
}

int hyp_encoder_read(const char *resource_id, int32_t *out_position, float *out_velocity) {
    if (!resource_id) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }
    if (!out_position && !out_velocity) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }

    char resource_type[32];
    char instance[64];
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type), instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }

    if (strcmp(resource_type, "encoder") != 0) {
        return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
    }

    int unit = encoder_unit_for_resource(instance);
    if (unit < 0 || unit >= PCNT_UNIT_MAX) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }

    if (!encoder_initialized[unit]) {
        return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    hyp_encoder_instance_t *enc = &encoder_instances[unit];
    int32_t count = 0;
    pcnt_get_counter_value(enc->unit, &count);

    uint32_t now_us = hyp_esp32_timestamp_us();
    float velocity = 0.0f;

    if (out_position) {
        *out_position = count;
    }

    if (out_velocity && enc->initialized) {
        float dt_s = (now_us - enc->prev_timestamp_us) / 1e6f;
        if (dt_s > 0.0f) {
            float pulses_per_sec = (count - enc->prev_count) / dt_s;
            velocity = (pulses_per_sec * 2.0f * 3.14159265358979323846f) / enc->pulses_per_revolution;
        }
        *out_velocity = velocity;
    }

    enc->prev_count = count;
    enc->prev_timestamp_us = now_us;
    enc->initialized = true;

    return HYP_RUNTIME_OK;
}

int hyp_encoder_init(const char *resource_id, int pulses_per_revolution, bool quadrature) {
    if (!resource_id) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }

    char resource_type[32];
    char instance[64];
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type), instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }

    if (strcmp(resource_type, "encoder") != 0) {
        return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
    }

    int unit = encoder_unit_for_resource(instance);
    if (unit < 0 || unit >= PCNT_UNIT_MAX) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }

    /* Get pins for this encoder from board config */
    /* For now, using default pins if not configured in board config */
    int pin_a = 4;
    int pin_b = 5;
    
    #ifdef HYP_RESOURCE_ENCODER_ENCODER0_A_PIN
    if (strcmp(instance, "ENCODER0") == 0) {
        pin_a = HYP_RESOURCE_ENCODER_ENCODER0_A_PIN;
        pin_b = HYP_RESOURCE_ENCODER_ENCODER0_B_PIN;
    }
    #endif
    #ifdef HYP_RESOURCE_ENCODER_ENCODER1_A_PIN
    if (strcmp(instance, "ENCODER1") == 0) {
        pin_a = HYP_RESOURCE_ENCODER_ENCODER1_A_PIN;
        pin_b = HYP_RESOURCE_ENCODER_ENCODER1_B_PIN;
    }
    #endif
    #ifdef HYP_RESOURCE_ENCODER_ENCODER2_A_PIN
    if (strcmp(instance, "ENCODER2") == 0) {
        pin_a = HYP_RESOURCE_ENCODER_ENCODER2_A_PIN;
        pin_b = HYP_RESOURCE_ENCODER_ENCODER2_B_PIN;
    }
    #endif
    #ifdef HYP_RESOURCE_ENCODER_ENCODER3_A_PIN
    if (strcmp(instance, "ENCODER3") == 0) {
        pin_a = HYP_RESOURCE_ENCODER_ENCODER3_A_PIN;
        pin_b = HYP_RESOURCE_ENCODER_ENCODER3_B_PIN;
    }
    #endif

    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = pin_a,
        .ctrl_gpio_num = pin_b,
        .lctrl_mode = PCNT_MODE_REVERSE,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DEC,
        .counter_h_lim = INT16_MAX,
        .counter_l_lim = INT16_MIN,
        .unit = unit,
        .channel = PCNT_CHANNEL_0,
    };

    esp_err_t err = pcnt_unit_config(&pcnt_config);
    if (err != ESP_OK) {
        return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    /* Configure filter to debounce */
    pcnt_set_filter_value(unit, 100);
    pcnt_filter_enable(unit);

    /* Set up quadrature mode if enabled */
    if (quadrature) {
        /* Quadrature: use both channels */
        pcnt_config_t pcnt_config_ch1 = {
            .pulse_gpio_num = pin_b,
            .ctrl_gpio_num = pin_a,
            .lctrl_mode = PCNT_MODE_REVERSE,
            .hctrl_mode = PCNT_MODE_KEEP,
            .pos_mode = PCNT_COUNT_INC,
            .neg_mode = PCNT_COUNT_DEC,
            .counter_h_lim = INT16_MAX,
            .counter_l_lim = INT16_MIN,
            .unit = unit,
            .channel = PCNT_CHANNEL_1,
        };
        pcnt_unit_config(&pcnt_config_ch1);
    }

    pcnt_counter_pause(unit);
    pcnt_counter_clear(unit);
    pcnt_counter_resume(unit);

    encoder_instances[unit].unit = unit;
    encoder_instances[unit].prev_count = 0;
    encoder_instances[unit].prev_timestamp_us = hyp_esp32_timestamp_us();
    encoder_instances[unit].initialized = false;
    encoder_instances[unit].pulses_per_revolution = pulses_per_revolution > 0 ? pulses_per_revolution : 1024;
    encoder_instances[unit].quadrature = quadrature;
    encoder_initialized[unit] = true;

    return HYP_RUNTIME_OK;
}

#else
/* Host mock implementation for testing */

/* Mock encoder state */
static int32_t mock_encoder_position = 0;
static float mock_encoder_velocity = 0.0f;
static bool mock_encoder_initialized = false;

int hyp_encoder_read(const char *resource_id, int32_t *out_position, float *out_velocity) {
    if (!resource_id) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }
    if (!out_position && !out_velocity) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }

    char resource_type[32];
    char instance[64];
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type), instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }

    if (strcmp(resource_type, "encoder") != 0) {
        return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
    }

    /* Mock: just return simulated values */
    if (out_position) {
        *out_position = mock_encoder_position;
    }
    if (out_velocity) {
        *out_velocity = mock_encoder_velocity;
    }
    
    /* Simulate position increment for testing */
    mock_encoder_position += 10;
    mock_encoder_velocity = 1.5f;
    mock_encoder_initialized = true;

    return HYP_RUNTIME_OK;
}

int hyp_encoder_init(const char *resource_id, int pulses_per_revolution, bool quadrature) {
    (void)pulses_per_revolution;
    (void)quadrature;
    
    if (!resource_id) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }

    char resource_type[32];
    char instance[64];
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type), instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }

    if (strcmp(resource_type, "encoder") != 0) {
        return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
    }

    mock_encoder_position = 0;
    mock_encoder_velocity = 0.0f;
    mock_encoder_initialized = true;

    return HYP_RUNTIME_OK;
}

#endif