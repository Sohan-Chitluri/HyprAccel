/*
 * hyp_esp32_hw.cpp
 *
 * ESP32 Hardware Initialization Layer
 * Part of the HyprAccel SDK.
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>
#include "hyprccel.h"
#include "hyp_esp32_hw.h"

#if defined(ARDUINO)
#include "hyp_board_config.h"
#else
#include "../../boards/codegen/hyp_board_config.h"
#endif

/* FW-P5: PWM channel counter for Arduino cores < 3 */
#if ESP_ARDUINO_VERSION_MAJOR < 3
static int next_pwm_channel = 0;
#endif

int hyp_esp32_hw_init(void)
{
    Serial.println("[INFO] Initializing ESP32 Hardware...");

    // -------------------------------------------------------------------------
    // 1. GPIO Configuration
    // -------------------------------------------------------------------------
    // Note: Standard C++ preprocessor does not allow #ifdef/#endif inside a macro.
    // Each GPIO pin configuration is written explicitly below.

#ifdef HYP_RESOURCE_GPIO_GPIO0
    {
        int pin = 0; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO0_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO0_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO0_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO0_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO0_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO0_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO0_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO0_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO0_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO0_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO1
    {
        int pin = 1; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO1_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO1_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO1_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO1_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO1_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO1_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO1_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO1_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO1_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO1_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO2
    {
        int pin = 2; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO2_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO2_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO2_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO2_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO2_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO2_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO2_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO2_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO2_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO2_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO3
    {
        int pin = 3; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO3_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO3_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO3_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO3_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO3_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO3_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO3_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO3_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO3_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO3_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO4
    {
        int pin = 4; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO4_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO4_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO4_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO4_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO4_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO4_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO4_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO4_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO4_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO4_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO5
    {
        int pin = 5; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO5_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO5_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO5_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO5_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO5_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO5_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO5_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO5_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO5_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO5_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO12
    {
        int pin = 12; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO12_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO12_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO12_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO12_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO12_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO12_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO12_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO12_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO12_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO12_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO13
    {
        int pin = 13; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO13_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO13_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO13_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO13_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO13_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO13_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO13_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO13_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO13_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO13_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO14
    {
        int pin = 14; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO14_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO14_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO14_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO14_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO14_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO14_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO14_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO14_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO14_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO14_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO15
    {
        int pin = 15; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO15_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO15_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO15_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO15_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO15_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO15_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO15_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO15_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO15_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO15_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO16
    {
        int pin = 16; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO16_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO16_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO16_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO16_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO16_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO16_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO16_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO16_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO16_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO16_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO17
    {
        int pin = 17; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO17_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO17_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO17_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO17_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO17_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO17_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO17_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO17_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO17_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO17_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO18
    {
        int pin = 18; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO18_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO18_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO18_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO18_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO18_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO18_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO18_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO18_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO18_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO18_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO19
    {
        int pin = 19; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO19_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO19_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO19_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO19_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO19_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO19_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO19_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO19_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO19_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO19_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO21
    {
        int pin = 21; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO21_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO21_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO21_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO21_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO21_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO21_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO21_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO21_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO21_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO21_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO22
    {
        int pin = 22; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO22_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO22_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO22_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO22_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO22_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO22_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO22_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO22_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO22_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO22_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO23
    {
        int pin = 23; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO23_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO23_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO23_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO23_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO23_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO23_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO23_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO23_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO23_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO23_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO25
    {
        int pin = 25; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO25_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO25_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO25_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO25_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO25_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO25_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO25_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO25_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO25_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO25_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO26
    {
        int pin = 26; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO26_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO26_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO26_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO26_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO26_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO26_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO26_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO26_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO26_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO26_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO27
    {
        int pin = 27; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO27_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO27_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO27_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO27_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO27_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO27_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO27_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO27_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO27_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO27_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO32
    {
        int pin = 32; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO32_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO32_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO32_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO32_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO32_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO32_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO32_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO32_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO32_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO32_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO33
    {
        int pin = 33; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO33_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO33_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO33_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO33_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO33_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO33_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO33_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO33_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO33_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO33_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO34
    {
        int pin = 34; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO34_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO34_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO34_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO34_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO34_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO34_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO34_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO34_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO34_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO34_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO35
    {
        int pin = 35; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO35_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO35_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO35_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO35_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO35_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO35_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO35_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO35_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO35_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO35_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO36
    {
        int pin = 36; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO36_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO36_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO36_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO36_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO36_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO36_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO36_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO36_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO36_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO36_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO39
    {
        int pin = 39; int mode = INPUT; bool is_output = false;
        #ifdef HYP_RESOURCE_GPIO_GPIO39_DIRECTION
            if (strcmp(HYP_RESOURCE_GPIO_GPIO39_DIRECTION, "output") == 0) { mode = OUTPUT; is_output = true; }
        #endif
        #ifdef HYP_RESOURCE_GPIO_GPIO39_PULL
            if (!is_output) {
                if (strcmp(HYP_RESOURCE_GPIO_GPIO39_PULL, "up") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO39_PULL, "pull-up") == 0) mode = INPUT_PULLUP;
                else if (strcmp(HYP_RESOURCE_GPIO_GPIO39_PULL, "down") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO39_PULL, "pull-down") == 0) mode = INPUT_PULLDOWN;
            }
        #endif
        pinMode(pin, mode);
        #ifdef HYP_RESOURCE_GPIO_GPIO39_INITIAL_STATE
            if (is_output) digitalWrite(pin, (strcmp(HYP_RESOURCE_GPIO_GPIO39_INITIAL_STATE, "high") == 0 || strcmp(HYP_RESOURCE_GPIO_GPIO39_INITIAL_STATE, "1") == 0) ? HIGH : LOW);
        #endif
    }
#endif

    // -------------------------------------------------------------------------
    // 2. UART Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_UART_UART0
    {
        long baud = 115200;
        #ifdef HYP_RESOURCE_UART_UART0_BAUD_RATE
            baud = HYP_RESOURCE_UART_UART0_BAUD_RATE;
        #endif
        // Serial is already open, but let's update baud rate if it is different
        Serial.begin(baud);
        Serial.println("[INFO] UART0 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_UART_UART1
    {
        long baud = 115200;
        #ifdef HYP_RESOURCE_UART_UART1_BAUD_RATE
            baud = HYP_RESOURCE_UART_UART1_BAUD_RATE;
        #endif
        #if defined(HYP_RESOURCE_UART_UART1_TX_PIN) && defined(HYP_RESOURCE_UART_UART1_RX_PIN)
            Serial1.begin(baud, SERIAL_8N1, HYP_RESOURCE_UART_UART1_RX_PIN, HYP_RESOURCE_UART_UART1_TX_PIN);
        #else
            #error "UART1 enabled but HYP_RESOURCE_UART_UART1_TX_PIN / _RX_PIN not defined in hyp_board_config.h"
        #endif
        Serial.println("[INFO] UART1 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_UART_UART2
    {
        long baud = 115200;
        #ifdef HYP_RESOURCE_UART_UART2_BAUD_RATE
            baud = HYP_RESOURCE_UART_UART2_BAUD_RATE;
        #endif
        #if defined(HYP_RESOURCE_UART_UART2_TX_PIN) && defined(HYP_RESOURCE_UART_UART2_RX_PIN)
            Serial2.begin(baud, SERIAL_8N1, HYP_RESOURCE_UART_UART2_RX_PIN, HYP_RESOURCE_UART_UART2_TX_PIN);
        #else
            #error "UART2 enabled but HYP_RESOURCE_UART_UART2_TX_PIN / _RX_PIN not defined in hyp_board_config.h"
        #endif
        Serial.println("[INFO] UART2 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 3. SPI Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_SPI_HSPI
    {
        #if !defined(HYP_RESOURCE_SPI_HSPI_SCK_PIN) || !defined(HYP_RESOURCE_SPI_HSPI_MOSI_PIN) \
         || !defined(HYP_RESOURCE_SPI_HSPI_MISO_PIN) || !defined(HYP_RESOURCE_SPI_HSPI_CS_PIN)
            #error "SPI HSPI enabled but pin macros missing from hyp_board_config.h"
        #endif
        long freq = 1000000;
        #ifdef HYP_RESOURCE_SPI_HSPI_FREQUENCY_HZ
            freq = HYP_RESOURCE_SPI_HSPI_FREQUENCY_HZ;
        #endif
        int mode = SPI_MODE0;
        #ifdef HYP_RESOURCE_SPI_HSPI_MODE
            int raw_mode = HYP_RESOURCE_SPI_HSPI_MODE;
            if (raw_mode == 1) mode = SPI_MODE1;
            else if (raw_mode == 2) mode = SPI_MODE2;
            else if (raw_mode == 3) mode = SPI_MODE3;
        #endif
        (void)mode; (void)freq; /* used if device transaction APIs are called */
        SPIClass *hspi = new SPIClass(HSPI);
        hspi->begin(HYP_RESOURCE_SPI_HSPI_SCK_PIN,
                    HYP_RESOURCE_SPI_HSPI_MISO_PIN,
                    HYP_RESOURCE_SPI_HSPI_MOSI_PIN,
                    HYP_RESOURCE_SPI_HSPI_CS_PIN);
        pinMode(HYP_RESOURCE_SPI_HSPI_CS_PIN, OUTPUT);
        digitalWrite(HYP_RESOURCE_SPI_HSPI_CS_PIN, HIGH);
        Serial.println("[INFO] SPI HSPI initialized.");
    }
#endif

#ifdef HYP_RESOURCE_SPI_VSPI
    {
        #if !defined(HYP_RESOURCE_SPI_VSPI_SCK_PIN) || !defined(HYP_RESOURCE_SPI_VSPI_MOSI_PIN) \
         || !defined(HYP_RESOURCE_SPI_VSPI_MISO_PIN) || !defined(HYP_RESOURCE_SPI_VSPI_CS_PIN)
            #error "SPI VSPI enabled but pin macros missing from hyp_board_config.h"
        #endif
        long freq = 1000000;
        #ifdef HYP_RESOURCE_SPI_VSPI_FREQUENCY_HZ
            freq = HYP_RESOURCE_SPI_VSPI_FREQUENCY_HZ;
        #endif
        int mode = SPI_MODE0;
        #ifdef HYP_RESOURCE_SPI_VSPI_MODE
            int raw_mode = HYP_RESOURCE_SPI_VSPI_MODE;
            if (raw_mode == 1) mode = SPI_MODE1;
            else if (raw_mode == 2) mode = SPI_MODE2;
            else if (raw_mode == 3) mode = SPI_MODE3;
        #endif
        (void)mode; (void)freq;
        SPIClass *vspi = new SPIClass(VSPI);
        vspi->begin(HYP_RESOURCE_SPI_VSPI_SCK_PIN,
                    HYP_RESOURCE_SPI_VSPI_MISO_PIN,
                    HYP_RESOURCE_SPI_VSPI_MOSI_PIN,
                    HYP_RESOURCE_SPI_VSPI_CS_PIN);
        pinMode(HYP_RESOURCE_SPI_VSPI_CS_PIN, OUTPUT);
        digitalWrite(HYP_RESOURCE_SPI_VSPI_CS_PIN, HIGH);
        Serial.println("[INFO] SPI VSPI initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 4. I2C Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_I2C_I2C0
    {
        #if !defined(HYP_RESOURCE_I2C_I2C0_SDA_PIN) || !defined(HYP_RESOURCE_I2C_I2C0_SCL_PIN)
            #error "I2C I2C0 enabled but HYP_RESOURCE_I2C_I2C0_SDA_PIN / _SCL_PIN not defined"
        #endif
        long freq = 400000;
        #ifdef HYP_RESOURCE_I2C_I2C0_FREQUENCY_HZ
            freq = HYP_RESOURCE_I2C_I2C0_FREQUENCY_HZ;
        #endif
        Wire.begin(HYP_RESOURCE_I2C_I2C0_SDA_PIN, HYP_RESOURCE_I2C_I2C0_SCL_PIN, (uint32_t)freq);
        Serial.println("[INFO] I2C I2C0 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 5. PWM Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_PWM_GPIO25
    {
        int pin = 25; long freq = 50; int res_bits = 16; int initial_duty = 0;
        #ifdef HYP_RESOURCE_PWM_GPIO25_FREQUENCY_HZ
            freq = HYP_RESOURCE_PWM_GPIO25_FREQUENCY_HZ;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO25_RESOLUTION_BITS
            res_bits = HYP_RESOURCE_PWM_GPIO25_RESOLUTION_BITS;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO25_INITIAL_DUTY
            initial_duty = HYP_RESOURCE_PWM_GPIO25_INITIAL_DUTY;
        #endif
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(pin, freq, res_bits); ledcWrite(pin, initial_duty);
        #else
            int chan = next_pwm_channel++;
            if (chan < 16) { ledcSetup(chan, freq, res_bits); ledcAttachPin(pin, chan); ledcWrite(chan, initial_duty); }
        #endif
        Serial.println("[INFO] PWM GPIO25 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_PWM_GPIO26
    {
        int pin = 26; long freq = 50; int res_bits = 16; int initial_duty = 0;
        #ifdef HYP_RESOURCE_PWM_GPIO26_FREQUENCY_HZ
            freq = HYP_RESOURCE_PWM_GPIO26_FREQUENCY_HZ;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO26_RESOLUTION_BITS
            res_bits = HYP_RESOURCE_PWM_GPIO26_RESOLUTION_BITS;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO26_INITIAL_DUTY
            initial_duty = HYP_RESOURCE_PWM_GPIO26_INITIAL_DUTY;
        #endif
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(pin, freq, res_bits); ledcWrite(pin, initial_duty);
        #else
            int chan = next_pwm_channel++;
            if (chan < 16) { ledcSetup(chan, freq, res_bits); ledcAttachPin(pin, chan); ledcWrite(chan, initial_duty); }
        #endif
        Serial.println("[INFO] PWM GPIO26 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_PWM_GPIO27
    {
        int pin = 27; long freq = 50; int res_bits = 16; int initial_duty = 0;
        #ifdef HYP_RESOURCE_PWM_GPIO27_FREQUENCY_HZ
            freq = HYP_RESOURCE_PWM_GPIO27_FREQUENCY_HZ;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO27_RESOLUTION_BITS
            res_bits = HYP_RESOURCE_PWM_GPIO27_RESOLUTION_BITS;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO27_INITIAL_DUTY
            initial_duty = HYP_RESOURCE_PWM_GPIO27_INITIAL_DUTY;
        #endif
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(pin, freq, res_bits); ledcWrite(pin, initial_duty);
        #else
            int chan = next_pwm_channel++;
            if (chan < 16) { ledcSetup(chan, freq, res_bits); ledcAttachPin(pin, chan); ledcWrite(chan, initial_duty); }
        #endif
        Serial.println("[INFO] PWM GPIO27 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_PWM_GPIO32
    {
        int pin = 32; long freq = 50; int res_bits = 16; int initial_duty = 0;
        #ifdef HYP_RESOURCE_PWM_GPIO32_FREQUENCY_HZ
            freq = HYP_RESOURCE_PWM_GPIO32_FREQUENCY_HZ;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO32_RESOLUTION_BITS
            res_bits = HYP_RESOURCE_PWM_GPIO32_RESOLUTION_BITS;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO32_INITIAL_DUTY
            initial_duty = HYP_RESOURCE_PWM_GPIO32_INITIAL_DUTY;
        #endif
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(pin, freq, res_bits); ledcWrite(pin, initial_duty);
        #else
            int chan = next_pwm_channel++;
            if (chan < 16) { ledcSetup(chan, freq, res_bits); ledcAttachPin(pin, chan); ledcWrite(chan, initial_duty); }
        #endif
        Serial.println("[INFO] PWM GPIO32 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_PWM_GPIO33
    {
        int pin = 33; long freq = 50; int res_bits = 16; int initial_duty = 0;
        #ifdef HYP_RESOURCE_PWM_GPIO33_FREQUENCY_HZ
            freq = HYP_RESOURCE_PWM_GPIO33_FREQUENCY_HZ;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO33_RESOLUTION_BITS
            res_bits = HYP_RESOURCE_PWM_GPIO33_RESOLUTION_BITS;
        #endif
        #ifdef HYP_RESOURCE_PWM_GPIO33_INITIAL_DUTY
            initial_duty = HYP_RESOURCE_PWM_GPIO33_INITIAL_DUTY;
        #endif
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(pin, freq, res_bits); ledcWrite(pin, initial_duty);
        #else
            int chan = next_pwm_channel++;
            if (chan < 16) { ledcSetup(chan, freq, res_bits); ledcAttachPin(pin, chan); ledcWrite(chan, initial_duty); }
        #endif
        Serial.println("[INFO] PWM GPIO33 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 6. ADC Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_ADC_GPIO32
    {
        int pin = 32; pinMode(pin, ANALOG); adc_attenuation_t atten = ADC_11db;
        #ifdef HYP_RESOURCE_ADC_GPIO32_ATTENUATION_DB
            float atten_val = HYP_RESOURCE_ADC_GPIO32_ATTENUATION_DB;
            if (atten_val == 0.0f) atten = ADC_0db;
            else if (atten_val > 0.0f && atten_val <= 2.5f) atten = ADC_2_5db;
            else if (atten_val > 2.5f && atten_val <= 6.0f) atten = ADC_6db;
            else if (atten_val > 6.0f) atten = ADC_11db;
        #endif
        analogSetPinAttenuation(pin, atten);
        Serial.println("[INFO] ADC GPIO32 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_ADC_GPIO33
    {
        int pin = 33; pinMode(pin, ANALOG); adc_attenuation_t atten = ADC_11db;
        #ifdef HYP_RESOURCE_ADC_GPIO33_ATTENUATION_DB
            float atten_val = HYP_RESOURCE_ADC_GPIO33_ATTENUATION_DB;
            if (atten_val == 0.0f) atten = ADC_0db;
            else if (atten_val > 0.0f && atten_val <= 2.5f) atten = ADC_2_5db;
            else if (atten_val > 2.5f && atten_val <= 6.0f) atten = ADC_6db;
            else if (atten_val > 6.0f) atten = ADC_11db;
        #endif
        analogSetPinAttenuation(pin, atten);
        Serial.println("[INFO] ADC GPIO33 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_ADC_GPIO34
    {
        int pin = 34; pinMode(pin, ANALOG); adc_attenuation_t atten = ADC_11db;
        #ifdef HYP_RESOURCE_ADC_GPIO34_ATTENUATION_DB
            float atten_val = HYP_RESOURCE_ADC_GPIO34_ATTENUATION_DB;
            if (atten_val == 0.0f) atten = ADC_0db;
            else if (atten_val > 0.0f && atten_val <= 2.5f) atten = ADC_2_5db;
            else if (atten_val > 2.5f && atten_val <= 6.0f) atten = ADC_6db;
            else if (atten_val > 6.0f) atten = ADC_11db;
        #endif
        analogSetPinAttenuation(pin, atten);
        Serial.println("[INFO] ADC GPIO34 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_ADC_GPIO35
    {
        int pin = 35; pinMode(pin, ANALOG); adc_attenuation_t atten = ADC_11db;
        #ifdef HYP_RESOURCE_ADC_GPIO35_ATTENUATION_DB
            float atten_val = HYP_RESOURCE_ADC_GPIO35_ATTENUATION_DB;
            if (atten_val == 0.0f) atten = ADC_0db;
            else if (atten_val > 0.0f && atten_val <= 2.5f) atten = ADC_2_5db;
            else if (atten_val > 2.5f && atten_val <= 6.0f) atten = ADC_6db;
            else if (atten_val > 6.0f) atten = ADC_11db;
        #endif
        analogSetPinAttenuation(pin, atten);
        Serial.println("[INFO] ADC GPIO35 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_ADC_GPIO36
    {
        int pin = 36; pinMode(pin, ANALOG); adc_attenuation_t atten = ADC_11db;
        #ifdef HYP_RESOURCE_ADC_GPIO36_ATTENUATION_DB
            float atten_val = HYP_RESOURCE_ADC_GPIO36_ATTENUATION_DB;
            if (atten_val == 0.0f) atten = ADC_0db;
            else if (atten_val > 0.0f && atten_val <= 2.5f) atten = ADC_2_5db;
            else if (atten_val > 2.5f && atten_val <= 6.0f) atten = ADC_6db;
            else if (atten_val > 6.0f) atten = ADC_11db;
        #endif
        analogSetPinAttenuation(pin, atten);
        Serial.println("[INFO] ADC GPIO36 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_ADC_GPIO39
    {
        int pin = 39; pinMode(pin, ANALOG); adc_attenuation_t atten = ADC_11db;
        #ifdef HYP_RESOURCE_ADC_GPIO39_ATTENUATION_DB
            float atten_val = HYP_RESOURCE_ADC_GPIO39_ATTENUATION_DB;
            if (atten_val == 0.0f) atten = ADC_0db;
            else if (atten_val > 0.0f && atten_val <= 2.5f) atten = ADC_2_5db;
            else if (atten_val > 2.5f && atten_val <= 6.0f) atten = ADC_6db;
            else if (atten_val > 6.0f) atten = ADC_11db;
        #endif
        analogSetPinAttenuation(pin, atten);
        Serial.println("[INFO] ADC GPIO39 initialized.");
    }
#endif

#if ESP_ARDUINO_VERSION_MAJOR < 3
    if (next_pwm_channel > 16) {
        Serial.println("[ERROR] Exceeded maximum number of PWM channels (16).");
        return -1;
    }
#endif

    Serial.println("[INFO] ESP32 Hardware Initialized successfully.");
    return 0;
}

/* ==========================================================================
 * Sensor / Actuator Runtime Operations
 * ========================================================================== */

/**
 * Parse hardware resource ID (e.g., "adc.channel0", "pwm.motor0")
 * Returns the resource type (adc, gpio, pwm, etc.) and instance identifier.
 */
static int parse_resource_id(const char *resource_id, char *type_out, size_t type_size, char *instance_out, size_t instance_size) {
    if (!resource_id || !type_out || !instance_out) return -1;
    
    const char *dot = strchr(resource_id, '.');
    if (!dot || dot == resource_id || dot[1] == '\0' || strchr(dot + 1, '.') != NULL) return -1;
    
    size_t type_len = dot - resource_id;
    if (type_len >= type_size) return -1;
    strncpy(type_out, resource_id, type_len);
    type_out[type_len] = '\0';
    
    const char *instance = dot + 1;
    size_t instance_len = strlen(instance);
    if (instance_len >= instance_size) return -1;
    strcpy(instance_out, instance);
    
    return 0;
}

/**
 * hyp_resource_pin_entry — compile-time table entry mapping a resource ID
 * string to its physical GPIO pin number.
 *
 * FW-P1..P6: All entries are derived from HYP_RESOURCE_*_PIN macros that
 * gen_board_config.js emits from boards.yaml.  No pin numbers are hardcoded
 * here; the table is built entirely from the generated configuration.
 */
typedef struct { const char *resource_id; int pin; } hyp_resource_pin_entry_t;

static const hyp_resource_pin_entry_t hyp_resource_pin_table[] = {
    /* ADC resources (FW-P6) */
#ifdef HYP_RESOURCE_ADC_GPIO32_PIN
    { "adc.GPIO32", HYP_RESOURCE_ADC_GPIO32_PIN },
#endif
#ifdef HYP_RESOURCE_ADC_GPIO33_PIN
    { "adc.GPIO33", HYP_RESOURCE_ADC_GPIO33_PIN },
#endif
#ifdef HYP_RESOURCE_ADC_GPIO34_PIN
    { "adc.GPIO34", HYP_RESOURCE_ADC_GPIO34_PIN },
#endif
#ifdef HYP_RESOURCE_ADC_GPIO35_PIN
    { "adc.GPIO35", HYP_RESOURCE_ADC_GPIO35_PIN },
#endif
#ifdef HYP_RESOURCE_ADC_GPIO36_PIN
    { "adc.GPIO36", HYP_RESOURCE_ADC_GPIO36_PIN },
#endif
#ifdef HYP_RESOURCE_ADC_GPIO39_PIN
    { "adc.GPIO39", HYP_RESOURCE_ADC_GPIO39_PIN },
#endif
    /* PWM resources (FW-P5) */
#ifdef HYP_RESOURCE_PWM_GPIO25_PIN
    { "pwm.GPIO25", HYP_RESOURCE_PWM_GPIO25_PIN },
#endif
#ifdef HYP_RESOURCE_PWM_GPIO26_PIN
    { "pwm.GPIO26", HYP_RESOURCE_PWM_GPIO26_PIN },
#endif
#ifdef HYP_RESOURCE_PWM_GPIO27_PIN
    { "pwm.GPIO27", HYP_RESOURCE_PWM_GPIO27_PIN },
#endif
#ifdef HYP_RESOURCE_PWM_GPIO32_PIN
    { "pwm.GPIO32", HYP_RESOURCE_PWM_GPIO32_PIN },
#endif
#ifdef HYP_RESOURCE_PWM_GPIO33_PIN
    { "pwm.GPIO33", HYP_RESOURCE_PWM_GPIO33_PIN },
#endif
    /* GPIO resources (FW-P1) */
#ifdef HYP_RESOURCE_GPIO_GPIO0_PIN
    { "gpio.GPIO0",  HYP_RESOURCE_GPIO_GPIO0_PIN  },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO1_PIN
    { "gpio.GPIO1",  HYP_RESOURCE_GPIO_GPIO1_PIN  },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO2_PIN
    { "gpio.GPIO2",  HYP_RESOURCE_GPIO_GPIO2_PIN  },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO3_PIN
    { "gpio.GPIO3",  HYP_RESOURCE_GPIO_GPIO3_PIN  },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO4_PIN
    { "gpio.GPIO4",  HYP_RESOURCE_GPIO_GPIO4_PIN  },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO5_PIN
    { "gpio.GPIO5",  HYP_RESOURCE_GPIO_GPIO5_PIN  },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO12_PIN
    { "gpio.GPIO12", HYP_RESOURCE_GPIO_GPIO12_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO13_PIN
    { "gpio.GPIO13", HYP_RESOURCE_GPIO_GPIO13_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO14_PIN
    { "gpio.GPIO14", HYP_RESOURCE_GPIO_GPIO14_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO15_PIN
    { "gpio.GPIO15", HYP_RESOURCE_GPIO_GPIO15_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO16_PIN
    { "gpio.GPIO16", HYP_RESOURCE_GPIO_GPIO16_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO17_PIN
    { "gpio.GPIO17", HYP_RESOURCE_GPIO_GPIO17_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO18_PIN
    { "gpio.GPIO18", HYP_RESOURCE_GPIO_GPIO18_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO19_PIN
    { "gpio.GPIO19", HYP_RESOURCE_GPIO_GPIO19_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO21_PIN
    { "gpio.GPIO21", HYP_RESOURCE_GPIO_GPIO21_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO22_PIN
    { "gpio.GPIO22", HYP_RESOURCE_GPIO_GPIO22_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO23_PIN
    { "gpio.GPIO23", HYP_RESOURCE_GPIO_GPIO23_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO25_PIN
    { "gpio.GPIO25", HYP_RESOURCE_GPIO_GPIO25_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO26_PIN
    { "gpio.GPIO26", HYP_RESOURCE_GPIO_GPIO26_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO27_PIN
    { "gpio.GPIO27", HYP_RESOURCE_GPIO_GPIO27_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO32_PIN
    { "gpio.GPIO32", HYP_RESOURCE_GPIO_GPIO32_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO33_PIN
    { "gpio.GPIO33", HYP_RESOURCE_GPIO_GPIO33_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO34_PIN
    { "gpio.GPIO34", HYP_RESOURCE_GPIO_GPIO34_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO35_PIN
    { "gpio.GPIO35", HYP_RESOURCE_GPIO_GPIO35_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO36_PIN
    { "gpio.GPIO36", HYP_RESOURCE_GPIO_GPIO36_PIN },
#endif
#ifdef HYP_RESOURCE_GPIO_GPIO39_PIN
    { "gpio.GPIO39", HYP_RESOURCE_GPIO_GPIO39_PIN },
#endif
    { NULL, -1 } /* sentinel */
};

/**
 * get_pin_for_resource — look up the GPIO pin for a fully-qualified resource ID.
 *
 * resource_id format: "<type>.<instance>"  e.g. "adc.GPIO32", "pwm.GPIO25",
 * "gpio.GPIO4".
 *
 * Returns the GPIO pin number from the compile-time table, or -1 if the
 * resource is not configured in hyp_board_config.h.
 */
static int get_pin_for_resource(const char *resource_type, const char *instance) {
    if (!resource_type || !instance) return -1;
    /* Build fully-qualified key: "<type>.<instance>" on the stack */
    char key[64];
    snprintf(key, sizeof(key), "%s.%s", resource_type, instance);
    for (int i = 0; hyp_resource_pin_table[i].resource_id != NULL; i++) {
        if (strcmp(hyp_resource_pin_table[i].resource_id, key) == 0) {
            return hyp_resource_pin_table[i].pin;
        }
    }
    Serial.print("[WARN] get_pin_for_resource: no configured pin for ");
    Serial.println(key);
    return -1; /* resource not configured in board config */
}

/* Bus resources do not map to one GPIO. Resolve their existence from the
 * generated board configuration before attempting an operation. */
static bool is_configured_bus_resource(const char *resource_type, const char *instance) {
    if (!resource_type || !instance) return false;
    if (strcmp(resource_type, "uart") == 0) {
#ifdef HYP_RESOURCE_UART_UART0
        if (strcmp(instance, "UART0") == 0) return true;
#endif
#ifdef HYP_RESOURCE_UART_UART1
        if (strcmp(instance, "UART1") == 0) return true;
#endif
#ifdef HYP_RESOURCE_UART_UART2
        if (strcmp(instance, "UART2") == 0) return true;
#endif
        return false;
    }
    if (strcmp(resource_type, "spi") == 0) {
#ifdef HYP_RESOURCE_SPI_HSPI
        if (strcmp(instance, "HSPI") == 0) return true;
#endif
#ifdef HYP_RESOURCE_SPI_VSPI
        if (strcmp(instance, "VSPI") == 0) return true;
#endif
        return false;
    }
    if (strcmp(resource_type, "i2c") == 0) {
#ifdef HYP_RESOURCE_I2C_I2C0
        if (strcmp(instance, "I2C0") == 0) return true;
#endif
        return false;
    }
    return false;
}

static HardwareSerial *serial_for_resource(const char *instance) {
    if (strcmp(instance, "UART0") == 0) return &Serial;
    if (strcmp(instance, "UART1") == 0) return &Serial1;
    if (strcmp(instance, "UART2") == 0) return &Serial2;
    return NULL;
}

uint32_t hyp_esp32_timestamp_us(void) {
    return (uint32_t)micros();
}

int hyp_esp32_sensor_read(const char *resource_id, void *out_value, uint32_t value_size) {
    if (!resource_id || !out_value || value_size == 0) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }
    
    char resource_type[32];
    char instance[64];
    
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type), 
                          instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }
    
    // Handle different sensor types
    if (strcmp(resource_type, "adc") == 0) {
        // ADC read - returns 12-bit value (0-4095) or voltage
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        
        if (value_size == sizeof(uint16_t)) {
            uint16_t *out = (uint16_t *)out_value;
            *out = analogRead(pin);
            return 0;
        } else if (value_size == sizeof(float)) {
            float *out = (float *)out_value;
            uint16_t raw = analogRead(pin);
            // Convert to voltage (assuming 3.3V reference, 12-bit ADC)
            *out = (raw * 3.3f) / 4095.0f;
            return 0;
        }
        return HYP_RUNTIME_BUFFER_TOO_SMALL;
    }
    
    if (strcmp(resource_type, "gpio") == 0) {
        // Digital GPIO read - returns boolean (0 or 1)
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        
        if (value_size == sizeof(uint8_t)) {
            uint8_t *out = (uint8_t *)out_value;
            *out = digitalRead(pin) ? 1 : 0;
            return 0;
        }
        if (value_size == sizeof(float)) {
            float *out = (float *)out_value;
            *out = digitalRead(pin) ? 1.0f : 0.0f;
            return HYP_RUNTIME_OK;
        }
        return HYP_RUNTIME_BUFFER_TOO_SMALL;
    }
    
    if (strcmp(resource_type, "uart") == 0) {
        if (!is_configured_bus_resource(resource_type, instance)) {
            return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        }
        HardwareSerial *serial = serial_for_resource(instance);
        if (!serial) return HYP_RUNTIME_UNSUPPORTED_INSTANCE;
        // Report bytes currently available; this primitive does not consume data.
        if (value_size == sizeof(int)) {
            int *out = (int *)out_value;
            *out = serial->available();
            return HYP_RUNTIME_OK;
        }
        if (value_size == sizeof(float)) {
            float *out = (float *)out_value;
            *out = (float)serial->available();
            return HYP_RUNTIME_OK;
        }
        return HYP_RUNTIME_BUFFER_TOO_SMALL;
    }

    if (strcmp(resource_type, "spi") == 0 || strcmp(resource_type, "i2c") == 0) {
        return is_configured_bus_resource(resource_type, instance)
            ? HYP_RUNTIME_UNSUPPORTED_RESOURCE : HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
}

int hyp_esp32_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size) {
    if (!resource_id || !in_value || value_size == 0) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }
    
    char resource_type[32];
    char instance[64];
    
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type),
                          instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }
    
    // Handle different actuator types
    if (strcmp(resource_type, "pwm") == 0) {
        // PWM write - expects duty cycle (0-65535 for 16-bit, or 0-255 for 8-bit)
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        
        if (value_size == sizeof(uint16_t)) {
            uint16_t duty = *(const uint16_t *)in_value;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcWrite(pin, duty);
#else
            // Need to find the channel for this pin - simplified for now
            // In reality we'd need a pin->channel mapping
            ledcWrite(pin, duty); // This won't work on older Arduino cores without channel
#endif
            return 0;
        } else if (value_size == sizeof(float)) {
            // Accept float 0.0-1.0 as duty cycle fraction
            float duty_frac = *(const float *)in_value;
            if (duty_frac < 0.0f || duty_frac > 1.0f) return HYP_RUNTIME_INVALID_ARGUMENT;
            uint16_t duty = (uint16_t)(duty_frac * 65535.0f);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcWrite(pin, duty);
#endif
            return 0;
        }
        return HYP_RUNTIME_BUFFER_TOO_SMALL;
    }
    
    if (strcmp(resource_type, "gpio") == 0) {
        // Digital GPIO write - expects boolean (0 or 1)
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        
        if (value_size == sizeof(uint8_t)) {
            uint8_t val = *(const uint8_t *)in_value;
            digitalWrite(pin, val ? HIGH : LOW);
            return 0;
        }
        if (value_size == sizeof(float)) {
            float value = *(const float *)in_value;
            if (value != 0.0f && value != 1.0f) return HYP_RUNTIME_INVALID_ARGUMENT;
            digitalWrite(pin, value != 0.0f ? HIGH : LOW);
            return HYP_RUNTIME_OK;
        }
        return HYP_RUNTIME_BUFFER_TOO_SMALL;
    }
    
    if (strcmp(resource_type, "uart") == 0) {
        if (!is_configured_bus_resource(resource_type, instance)) {
            return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        }
        HardwareSerial *serial = serial_for_resource(instance);
        if (!serial) return HYP_RUNTIME_UNSUPPORTED_INSTANCE;
        serial->write((const uint8_t *)in_value, value_size);
        return HYP_RUNTIME_OK;
    }

    if (strcmp(resource_type, "spi") == 0 || strcmp(resource_type, "i2c") == 0) {
        return is_configured_bus_resource(resource_type, instance)
            ? HYP_RUNTIME_UNSUPPORTED_RESOURCE : HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
}
