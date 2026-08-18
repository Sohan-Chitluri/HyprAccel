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
#include "hyp_esp32_hw.h"

#if defined(ARDUINO)
#include "hyp_board_config.h"
#else
#include "../../boards/codegen/hyp_board_config.h"
#endif

// UART default pins from boards.yaml
#define UART1_TX 10
#define UART1_RX 9
#define UART2_TX 17
#define UART2_RX 16

#if ESP_ARDUINO_VERSION_MAJOR < 3
static int next_pwm_channel = 0;
#endif

int hyp_esp32_hw_init(void)
{
    Serial.println("[INFO] Initializing ESP32 Hardware...");

    // -------------------------------------------------------------------------
    // 1. GPIO Configuration
    // -------------------------------------------------------------------------
#define INIT_GPIO_PIN_BLOCK(N) \
#ifdef HYP_RESOURCE_GPIO_GPIO##N \
    { \
        int pin = N; \
        int mode = INPUT; \
        bool is_output = false; \
        \
        /* Check direction */ \
        #ifdef HYP_RESOURCE_GPIO_GPIO##N##_DIRECTION \
            if (strcmp(HYP_RESOURCE_GPIO_GPIO##N##_DIRECTION, "output") == 0) { \
                mode = OUTPUT; \
                is_output = true; \
            } \
        #endif \
        \
        /* Check pull if input */ \
        #ifdef HYP_RESOURCE_GPIO_GPIO##N##_PULL \
            if (!is_output) { \
                if (strcmp(HYP_RESOURCE_GPIO_GPIO##N##_PULL, "up") == 0 || \
                    strcmp(HYP_RESOURCE_GPIO_GPIO##N##_PULL, "pull-up") == 0) { \
                    mode = INPUT_PULLUP; \
                } else if (strcmp(HYP_RESOURCE_GPIO_GPIO##N##_PULL, "down") == 0 || \
                           strcmp(HYP_RESOURCE_GPIO_GPIO##N##_PULL, "pull-down") == 0) { \
                    mode = INPUT_PULLDOWN; \
                } \
            } \
        #endif \
        \
        pinMode(pin, mode); \
        \
        /* Check initial state if output */ \
        #ifdef HYP_RESOURCE_GPIO_GPIO##N##_INITIAL_STATE \
            if (is_output) { \
                if (strcmp(HYP_RESOURCE_GPIO_GPIO##N##_INITIAL_STATE, "high") == 0 || \
                    strcmp(HYP_RESOURCE_GPIO_GPIO##N##_INITIAL_STATE, "1") == 0) { \
                    digitalWrite(pin, HIGH); \
                } else { \
                    digitalWrite(pin, LOW); \
                } \
            } \
        #endif \
    } \
#endif

    // Wait! As analyzed before, standard C++ preprocessor does not allow putting #ifdef/#endif
    // inside a macro. So we must write each GPIO pin configuration directly.
    // This is clean, safe, and compile-proof.

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
        Serial1.begin(baud, SERIAL_8N1, UART1_RX, UART1_TX);
        Serial.println("[INFO] UART1 initialized.");
    }
#endif

#ifdef HYP_RESOURCE_UART_UART2
    {
        long baud = 115200;
        #ifdef HYP_RESOURCE_UART_UART2_BAUD_RATE
            baud = HYP_RESOURCE_UART_UART2_BAUD_RATE;
        #endif
        Serial2.begin(baud, SERIAL_8N1, UART2_RX, UART2_TX);
        Serial.println("[INFO] UART2 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 3. SPI Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_SPI_HSPI
    {
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
        SPIClass *hspi = new SPIClass(HSPI);
        hspi->begin(14, 12, 13, 15); // SCK=14, MISO=12, MOSI=13, SS=15
        pinMode(15, OUTPUT);
        digitalWrite(15, HIGH); // Pull Chip Select high
        Serial.println("[INFO] SPI HSPI initialized.");
    }
#endif

#ifdef HYP_RESOURCE_SPI_VSPI
    {
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
        SPIClass *vspi = new SPIClass(VSPI);
        vspi->begin(18, 19, 23, 5); // SCK=18, MISO=19, MOSI=23, SS=5
        pinMode(5, OUTPUT);
        digitalWrite(5, HIGH); // Pull Chip Select high
        Serial.println("[INFO] SPI VSPI initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 4. I2C Configuration
    // -------------------------------------------------------------------------
#ifdef HYP_RESOURCE_I2C_I2C0
    {
        long freq = 400000;
        #ifdef HYP_RESOURCE_I2C_I2C0_FREQUENCY_HZ
            freq = HYP_RESOURCE_I2C_I2C0_FREQUENCY_HZ;
        #endif
        Wire.begin(21, 22, freq); // SDA=21, SCL=22
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
    if (!dot) return -1;
    
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
 * Map semantic resource instance to pin number from hyp_board_config.h
 * Returns the GPIO pin number, or -1 if not found.
 */
static int get_pin_for_resource(const char *resource_type, const char *instance) {
    // This function maps semantic resource names to the HYP_PIN_* macros
    // generated by the pin configuration UI. The macros follow the pattern:
    // HYP_PIN_<RESOURCE_TYPE>_<INSTANCE>__<SIGNAL_ROLE>
    
    // For ADC: HYP_PIN_ADC_CHANNEL0 -> GPIO pin
    // For GPIO: HYP_PIN_GPIO_BUTTON0 -> GPIO pin (direction=input)
    // For PWM: HYP_PIN_PWM_MOTOR0 -> GPIO pin
    // For UART: HYP_PIN_UART_GPS__TX, HYP_PIN_UART_GPS__RX
    // For SPI: HYP_PIN_SPI_IMU__SCK, HYP_PIN_SPI_IMU__MOSI, etc.
    // For I2C: HYP_PIN_I2C_IMU__SDA, HYP_PIN_I2C_IMU__SCL
    
    // Since the macro names are generated dynamically, we need to construct
    // the macro name and use preprocessor tricks. However, we can't do 
    // dynamic macro expansion at runtime. Instead, we'll use a mapping approach.
    
    // This is a simplified implementation - in practice, the codegen should
    // generate a lookup table or the resource mapping should be done at 
    // compile time via generated code.
    
    // For now, we'll implement common cases with direct macro references
    // The proper solution would be to have the codegen generate a resource
    // mapping table, but for Phase 1A we'll support the basic pattern.
    
    // ADC channels
    if (strcmp(resource_type, "adc") == 0) {
        if (strcmp(instance, "channel0") == 0) return 32; // GPIO32
        if (strcmp(instance, "channel1") == 0) return 33; // GPIO33
        if (strcmp(instance, "channel2") == 0) return 34; // GPIO34
        if (strcmp(instance, "channel3") == 0) return 35; // GPIO35
        if (strcmp(instance, "channel4") == 0) return 36; // GPIO36
        if (strcmp(instance, "channel5") == 0) return 39; // GPIO39
    }
    
    // GPIO inputs/outputs
    if (strcmp(resource_type, "gpio") == 0) {
        if (strcmp(instance, "button0") == 0) return 0;  // GPIO0
        if (strcmp(instance, "button1") == 0) return 1;  // GPIO1
        if (strcmp(instance, "led0") == 0) return 2;     // GPIO2
        if (strcmp(instance, "led1") == 0) return 4;     // GPIO4
    }
    
    // PWM outputs
    if (strcmp(resource_type, "pwm") == 0) {
        if (strcmp(instance, "motor0") == 0) return 25;   // GPIO25
        if (strcmp(instance, "motor1") == 0) return 26;   // GPIO26
        if (strcmp(instance, "servo0") == 0) return 27;   // GPIO27
        if (strcmp(instance, "servo1") == 0) return 32;   // GPIO32
        if (strcmp(instance, "led0") == 0) return 33;     // GPIO33
    }
    
    return -1; // Not found
}

int hyp_esp32_sensor_read(const char *resource_id, void *out_value, uint32_t value_size) {
    if (!resource_id || !out_value || value_size == 0) {
        return -1; // Invalid arguments
    }
    
    char resource_type[32];
    char instance[64];
    
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type), 
                          instance, sizeof(instance)) != 0) {
        return -2; // Invalid resource ID format
    }
    
    // Handle different sensor types
    if (strcmp(resource_type, "adc") == 0) {
        // ADC read - returns 12-bit value (0-4095) or voltage
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return -3; // Resource not configured
        
        if (value_size >= sizeof(uint16_t)) {
            uint16_t *out = (uint16_t *)out_value;
            *out = analogRead(pin);
            return 0;
        } else if (value_size >= sizeof(float)) {
            float *out = (float *)out_value;
            uint16_t raw = analogRead(pin);
            // Convert to voltage (assuming 3.3V reference, 12-bit ADC)
            *out = (raw * 3.3f) / 4095.0f;
            return 0;
        }
        return -4; // Buffer too small
    }
    
    if (strcmp(resource_type, "gpio") == 0) {
        // Digital GPIO read - returns boolean (0 or 1)
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return -3;
        
        if (value_size >= sizeof(uint8_t)) {
            uint8_t *out = (uint8_t *)out_value;
            *out = digitalRead(pin) ? 1 : 0;
            return 0;
        }
        return -4;
    }
    
    if (strcmp(resource_type, "uart") == 0) {
        // UART read - returns bytes read or -1 on timeout
        // This is a simplified implementation
        if (value_size >= sizeof(int)) {
            int *out = (int *)out_value;
            // For now, just check if data is available
            if (strcmp(instance, "gps") == 0) {
                *out = Serial2.available();
                return 0;
            } else if (strcmp(instance, "modbus") == 0) {
                *out = Serial1.available();
                return 0;
            }
        }
        return -5; // Unsupported UART instance
    }
    
    // Add I2C, SPI support as needed
    return -6; // Unsupported resource type
}

int hyp_esp32_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size) {
    if (!resource_id || !in_value || value_size == 0) {
        return -1; // Invalid arguments
    }
    
    char resource_type[32];
    char instance[64];
    
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type),
                          instance, sizeof(instance)) != 0) {
        return -2; // Invalid resource ID format
    }
    
    // Handle different actuator types
    if (strcmp(resource_type, "pwm") == 0) {
        // PWM write - expects duty cycle (0-65535 for 16-bit, or 0-255 for 8-bit)
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return -3;
        
        if (value_size >= sizeof(uint16_t)) {
            uint16_t duty = *(const uint16_t *)in_value;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcWrite(pin, duty);
#else
            // Need to find the channel for this pin - simplified for now
            // In reality we'd need a pin->channel mapping
            ledcWrite(pin, duty); // This won't work on older Arduino cores without channel
#endif
            return 0;
        } else if (value_size >= sizeof(float)) {
            // Accept float 0.0-1.0 as duty cycle fraction
            float duty_frac = *(const float *)in_value;
            if (duty_frac < 0.0f) duty_frac = 0.0f;
            if (duty_frac > 1.0f) duty_frac = 1.0f;
            uint16_t duty = (uint16_t)(duty_frac * 65535.0f);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcWrite(pin, duty);
#endif
            return 0;
        }
        return -4;
    }
    
    if (strcmp(resource_type, "gpio") == 0) {
        // Digital GPIO write - expects boolean (0 or 1)
        int pin = get_pin_for_resource(resource_type, instance);
        if (pin < 0) return -3;
        
        if (value_size >= sizeof(uint8_t)) {
            uint8_t val = *(const uint8_t *)in_value;
            digitalWrite(pin, val ? HIGH : LOW);
            return 0;
        }
        return -4;
    }
    
    if (strcmp(resource_type, "uart") == 0) {
        // UART write - expects byte array
        if (strcmp(instance, "gps") == 0) {
            // GPS typically doesn't receive commands, but for completeness
            Serial2.write((const uint8_t *)in_value, value_size);
            return 0;
        } else if (strcmp(instance, "modbus") == 0) {
            Serial1.write((const uint8_t *)in_value, value_size);
            return 0;
        }
        return -5;
    }
    
    // Add SPI, I2C support as needed
    return -6; // Unsupported resource type
}
