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

/* -----------------------------------------------------------------------
 * HYP_SDK_PINFUNC_MODE — desktop pin-function gating switch.
 *
 * hyp_board_config.h has no single marker macro that says "this header
 * carries desktop pin-function data" (see project_defines.js
 * buildPinFunctionBlock): web-only projects emit NO HYP_PINFUNC_* macros at
 * all, byte-identical to the pre-desktop header. We derive the switch
 * ourselves: if ANY HYP_PINFUNC_<GPIOn> macro is defined for any ESP32 pin
 * listed in boards.yaml, the header came from a desktop-saved project and
 * peripheral init below must consider ONLY pins that carry a matching
 * HYP_PINFUNC_<GPIOn>_<FUNCTION> macro. When no HYP_PINFUNC_* macro is
 * defined anywhere (legacy web-only headers), HYP_SDK_PINFUNC_MODE stays
 * undefined and every #if below reduces to its original HYP_RESOURCE_*
 * condition — behaviour is byte-for-byte unchanged.
 * ------------------------------------------------------------------- */
#if defined(HYP_PINFUNC_GPIO0)  || defined(HYP_PINFUNC_GPIO1)  || defined(HYP_PINFUNC_GPIO2)  || \
    defined(HYP_PINFUNC_GPIO3)  || defined(HYP_PINFUNC_GPIO4)  || defined(HYP_PINFUNC_GPIO5)  || \
    defined(HYP_PINFUNC_GPIO12) || defined(HYP_PINFUNC_GPIO13) || defined(HYP_PINFUNC_GPIO14) || \
    defined(HYP_PINFUNC_GPIO15) || defined(HYP_PINFUNC_GPIO16) || defined(HYP_PINFUNC_GPIO17) || \
    defined(HYP_PINFUNC_GPIO18) || defined(HYP_PINFUNC_GPIO19) || defined(HYP_PINFUNC_GPIO21) || \
    defined(HYP_PINFUNC_GPIO22) || defined(HYP_PINFUNC_GPIO23) || defined(HYP_PINFUNC_GPIO25) || \
    defined(HYP_PINFUNC_GPIO26) || defined(HYP_PINFUNC_GPIO27) || defined(HYP_PINFUNC_GPIO32) || \
    defined(HYP_PINFUNC_GPIO33) || defined(HYP_PINFUNC_GPIO34) || defined(HYP_PINFUNC_GPIO35) || \
    defined(HYP_PINFUNC_GPIO36) || defined(HYP_PINFUNC_GPIO39)
#define HYP_SDK_PINFUNC_MODE 1
#endif

/* -----------------------------------------------------------------------
 * HYP_SDK_UART0_ACTIVE — is UART0 actually the pin-owning peripheral for
 * GPIO1/GPIO3?
 *
 * boards/codegen/gen_board_config.js generateHeader() emits
 * HYP_RESOURCE_UART_UART0 unconditionally for every ESP32 header (it is a
 * BOARD-level macro straight from boards.yaml, not a project one) — so
 * `defined(HYP_RESOURCE_UART_UART0)` can never be used to tell whether a
 * desktop-saved project actually left GPIO1/GPIO3 wired to UART0 or
 * reassigned them to plain GPIO. In legacy/web mode (no PINFUNC data) there
 * is no way to know either way, so we keep the historical assumption that
 * UART0 is always the console (byte-for-byte unchanged behaviour). In
 * PINFUNC mode we know precisely: UART0 is active iff GPIO1's or GPIO3's
 * pin-function macro says so.
 *
 * This single macro is the one place that decides UART0's fate: it gates
 * the UART0 init block below, the GPIO1/GPIO3 plain-GPIO blocks (mutually
 * exclusive with UART0 use), is_configured_bus_resource()'s uart0 case, and
 * HYP_SDK_CONSOLE_ENABLED (whether the SDK's own Serial logging may run).
 * ------------------------------------------------------------------- */
/* UART0 pins "claimed": GPIO1/GPIO3 carry a pin function other than their
 * UART0 role. Unassigned pins are NOT claimed — the console keeps them. */
#if defined(HYP_SDK_PINFUNC_MODE) && \
    ((defined(HYP_PINFUNC_GPIO1) && !defined(HYP_PINFUNC_GPIO1_UART_UART0_TX)) || \
     (defined(HYP_PINFUNC_GPIO3) && !defined(HYP_PINFUNC_GPIO3_UART_UART0_RX)))
#define HYP_SDK_UART0_PINS_CLAIMED 1
#endif

#if defined(HYP_RESOURCE_UART_UART0) && \
    (!defined(HYP_SDK_PINFUNC_MODE) || \
     (!defined(HYP_SDK_UART0_PINS_CLAIMED) && \
      (defined(HYP_PINFUNC_GPIO1_UART_UART0_TX) || defined(HYP_PINFUNC_GPIO3_UART_UART0_RX))))
#define HYP_SDK_UART0_ACTIVE 1
#endif

/* -----------------------------------------------------------------------
 * HYP_SDK_CONSOLE_ENABLED — may the SDK use Serial for its own [INFO]/
 * [WARN]/[ERROR] logging?
 *
 * mbd/editor's frozen generated main.cpp (server.js materializeEsp32
 * runtime wrapper) calls Serial.begin(115200) unconditionally before
 * hyp_esp32_hw_init() runs, on every board — web AND desktop projects
 * alike; web projects never carry PINFUNC data at all, so fully fixing the
 * desktop case (never starting Serial in the first place when the console
 * is not wanted) needs a change outside sdk/, in a future desktop-specific
 * firmware wrapper. That is out of scope here.
 *
 * What the SDK CAN do unilaterally: once it knows (via HYP_SDK_UART0_ACTIVE)
 * that GPIO1/GPIO3 were reassigned away from UART0, it must stop driving
 * that peripheral itself — both by ending the port main.cpp already opened
 * (see the `Serial.end()` call at the top of hyp_esp32_hw_init(), which
 * releases GPIO1/GPIO3 back to GPIO ownership before this function's own
 * pinMode() calls run) and by turning every one of its own Serial.print()/
 * println() calls into a no-op (HYP_LOG_PRINT/HYP_LOG_PRINTLN below), so the
 * SDK never toggles GPIO1 (UART0 TX) as a side effect of logging once the
 * user owns that pin as plain GPIO. In legacy/web mode HYP_SDK_UART0_ACTIVE
 * is always defined, so HYP_SDK_CONSOLE_ENABLED collapses to "always on" —
 * logging behaviour for web builds is unchanged.
 * ------------------------------------------------------------------- */
/* The console (Serial on UART0) stays on unless a project gave GPIO1/GPIO3
 * another function; a desktop project that simply leaves them unassigned
 * keeps its serial output (and the runtime markers Verify looks for). */
#if !defined(HYP_SDK_UART0_PINS_CLAIMED)
#define HYP_SDK_CONSOLE_ENABLED 1
#endif

#if defined(HYP_SDK_CONSOLE_ENABLED)
#define HYP_LOG_PRINTLN(x) Serial.println(x)
#define HYP_LOG_PRINT(x) Serial.print(x)
#else
#define HYP_LOG_PRINTLN(x) ((void)0)
#define HYP_LOG_PRINT(x) ((void)0)
#endif

/* FW-P5: PWM channel counter for Arduino cores < 3 */
#if ESP_ARDUINO_VERSION_MAJOR < 3
static int next_pwm_channel = 0;
#endif

/* SDK-T6: persistent SPI bus handles so runtime transactions can reuse the
 * peripheral configured in hyp_esp32_hw_init(). These remain NULL until the
 * corresponding bus is initialized; hyp_spi_transfer() treats NULL as
 * "not initialized" rather than failing silently. The captured frequency and
 * mode are applied per-transaction via SPISettings. */
static SPIClass *g_hspi_bus = NULL;
static SPIClass *g_vspi_bus = NULL;
static uint32_t  g_hspi_freq = 1000000;
static uint32_t  g_vspi_freq = 1000000;
static uint8_t   g_hspi_mode = SPI_MODE0;
static uint8_t   g_vspi_mode = SPI_MODE0;

int hyp_esp32_hw_init(void)
{
#if !defined(HYP_SDK_CONSOLE_ENABLED)
    /* UART0's pins (GPIO1/GPIO3) were reassigned to plain GPIO by this
     * desktop project. mbd/editor's frozen generated main.cpp still calls
     * Serial.begin(115200) unconditionally before this function runs (see
     * boards.yaml-independent code in mbd/editor/server.js's materializeEsp32
     * runtime wrapper); end the port here so the GPIO1/GPIO3 pinMode() calls
     * below take pin ownership back from the UART0 peripheral cleanly, and
     * so nothing below can accidentally toggle GPIO1 (UART0 TX) via Serial. */
    Serial.end();
#endif
    HYP_LOG_PRINTLN("[INFO] Initializing ESP32 Hardware...");

    // -------------------------------------------------------------------------
    // 0. Clock Configuration (desktop Studio clock tab, applied best-effort)
    // -------------------------------------------------------------------------
    // Runs first, before any peripheral whose baud rate / timing derives from
    // APB_CLK, so UART/SPI/I2C/PWM init below observe the configured frequency.
#ifdef HYP_CLOCK_CONFIG_PRESENT
    {
        bool cpu_freq_ok = setCpuFrequencyMhz(HYP_CPU_FREQ_MHZ);
        #ifdef HYP_CLOCK_NONSTANDARD_CPU_FREQ
            HYP_LOG_PRINTLN("[WARN] HYP_CPU_FREQ_MHZ is not one of the ESP-IDF supported CPU frequencies (80/160/240 MHz); applying best-effort.");
        #endif
        if (!cpu_freq_ok) {
            HYP_LOG_PRINTLN("[ERROR] setCpuFrequencyMhz() rejected HYP_CPU_FREQ_MHZ; continuing at the default CPU frequency.");
        } else {
            HYP_LOG_PRINT("[INFO] CPU frequency set to ");
            HYP_LOG_PRINT(HYP_CPU_FREQ_MHZ);
            HYP_LOG_PRINTLN(" MHz.");
        }
    }
#endif

    // -------------------------------------------------------------------------
    // 1. GPIO Configuration
    // -------------------------------------------------------------------------
    // Note: Standard C++ preprocessor does not allow #ifdef/#endif inside a macro.
    // Each GPIO pin configuration is written explicitly below.

#if defined(HYP_RESOURCE_GPIO_GPIO0) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO0_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO1) && \
    ((defined(HYP_SDK_PINFUNC_MODE) && defined(HYP_PINFUNC_GPIO1_GPIO)) || \
     (!defined(HYP_SDK_PINFUNC_MODE) && !defined(HYP_RESOURCE_UART_UART0)))
    /* GPIO1 = UART0 TX: only configure as plain GPIO when UART0 is not active. */
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

#if defined(HYP_RESOURCE_GPIO_GPIO2) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO2_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO3) && \
    ((defined(HYP_SDK_PINFUNC_MODE) && defined(HYP_PINFUNC_GPIO3_GPIO)) || \
     (!defined(HYP_SDK_PINFUNC_MODE) && !defined(HYP_RESOURCE_UART_UART0)))
    /* GPIO3 = UART0 RX: only configure as plain GPIO when UART0 is not active. */
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

#if defined(HYP_RESOURCE_GPIO_GPIO4) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO4_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO5) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO5_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO12) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO12_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO13) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO13_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO14) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO14_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO15) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO15_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO16) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO16_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO17) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO17_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO18) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO18_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO19) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO19_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO21) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO21_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO22) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO22_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO23) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO23_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO25) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO25_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO26) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO26_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO27) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO27_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO32) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO32_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO33) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO33_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO34) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO34_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO35) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO35_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO36) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO36_GPIO))
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

#if defined(HYP_RESOURCE_GPIO_GPIO39) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO39_GPIO))
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
#if defined(HYP_SDK_UART0_ACTIVE)
    {
        /* UART0 / Serial is already initialised by setup() before hyp_esp32_hw_init()
         * is called.  Calling Serial.begin() a second time reinitialises the UART
         * hardware peripheral and can flush / corrupt bytes that are still queued
         * in the TX FIFO (observed as truncated HYPRACCEL_MBD_T9_READY output).
         * Skip the redundant begin(); the port is already open at 115200 baud. */
        HYP_LOG_PRINTLN("[INFO] UART0 already initialised by setup().");
    }
#endif

#if defined(HYP_RESOURCE_UART_UART1) && defined(HYP_RESOURCE_UART_UART1_TX_PIN) && defined(HYP_RESOURCE_UART_UART1_RX_PIN) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO10_UART_UART1_TX) || defined(HYP_PINFUNC_GPIO9_UART_UART1_RX))
    /* ESP32-WROOM-32: GPIO9 and GPIO10 are routed to the internal SPI flash in
     * QIO mode.  Attempting Serial1.begin() on those pins corrupts flash access
     * and causes a hard fault.  Skip UART1 init when the board config assigns
     * either of those pins to the UART1 bus. */
#  if (HYP_RESOURCE_UART_UART1_TX_PIN == 9  || HYP_RESOURCE_UART_UART1_TX_PIN == 10 || \
       HYP_RESOURCE_UART_UART1_RX_PIN == 9  || HYP_RESOURCE_UART_UART1_RX_PIN == 10)
    HYP_LOG_PRINTLN("[WARN] UART1 skipped: TX/RX pin conflicts with internal SPI flash (GPIO9/GPIO10).");
#  else
    {
        long baud = 115200;
        #ifdef HYP_RESOURCE_UART_UART1_BAUD_RATE
            baud = HYP_RESOURCE_UART_UART1_BAUD_RATE;
        #endif
        Serial1.begin(baud, SERIAL_8N1, HYP_RESOURCE_UART_UART1_RX_PIN, HYP_RESOURCE_UART_UART1_TX_PIN);
        HYP_LOG_PRINTLN("[INFO] UART1 initialized.");
    }
#  endif
#endif

#if defined(HYP_RESOURCE_UART_UART2) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO17_UART_UART2_TX) || defined(HYP_PINFUNC_GPIO16_UART_UART2_RX))
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
        HYP_LOG_PRINTLN("[INFO] UART2 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 3. SPI Configuration
    // -------------------------------------------------------------------------
#if defined(HYP_RESOURCE_SPI_HSPI) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO14_SPI_HSPI_SCK) || defined(HYP_PINFUNC_GPIO13_SPI_HSPI_MOSI) || defined(HYP_PINFUNC_GPIO12_SPI_HSPI_MISO) || defined(HYP_PINFUNC_GPIO15_SPI_HSPI_CS))
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
        g_hspi_bus = new SPIClass(HSPI);
        g_hspi_bus->begin(HYP_RESOURCE_SPI_HSPI_SCK_PIN,
                          HYP_RESOURCE_SPI_HSPI_MISO_PIN,
                          HYP_RESOURCE_SPI_HSPI_MOSI_PIN,
                          HYP_RESOURCE_SPI_HSPI_CS_PIN);
        g_hspi_freq = (uint32_t)freq;
        g_hspi_mode = (uint8_t)mode;
        pinMode(HYP_RESOURCE_SPI_HSPI_CS_PIN, OUTPUT);
        digitalWrite(HYP_RESOURCE_SPI_HSPI_CS_PIN, HIGH);
        HYP_LOG_PRINTLN("[INFO] SPI HSPI initialized.");
    }
#endif

#if defined(HYP_RESOURCE_SPI_VSPI) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO18_SPI_VSPI_SCK) || defined(HYP_PINFUNC_GPIO23_SPI_VSPI_MOSI) || defined(HYP_PINFUNC_GPIO19_SPI_VSPI_MISO) || defined(HYP_PINFUNC_GPIO5_SPI_VSPI_CS))
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
        g_vspi_bus = new SPIClass(VSPI);
        g_vspi_bus->begin(HYP_RESOURCE_SPI_VSPI_SCK_PIN,
                          HYP_RESOURCE_SPI_VSPI_MISO_PIN,
                          HYP_RESOURCE_SPI_VSPI_MOSI_PIN,
                          HYP_RESOURCE_SPI_VSPI_CS_PIN);
        g_vspi_freq = (uint32_t)freq;
        g_vspi_mode = (uint8_t)mode;
        pinMode(HYP_RESOURCE_SPI_VSPI_CS_PIN, OUTPUT);
        digitalWrite(HYP_RESOURCE_SPI_VSPI_CS_PIN, HIGH);
        HYP_LOG_PRINTLN("[INFO] SPI VSPI initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 4. I2C Configuration
    // -------------------------------------------------------------------------
#if defined(HYP_RESOURCE_I2C_I2C0) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO21_I2C_I2C0_SDA) || defined(HYP_PINFUNC_GPIO22_I2C_I2C0_SCL))
    {
        #if !defined(HYP_RESOURCE_I2C_I2C0_SDA_PIN) || !defined(HYP_RESOURCE_I2C_I2C0_SCL_PIN)
            #error "I2C I2C0 enabled but HYP_RESOURCE_I2C_I2C0_SDA_PIN / _SCL_PIN not defined"
        #endif
        long freq = 400000;
        #ifdef HYP_RESOURCE_I2C_I2C0_FREQUENCY_HZ
            freq = HYP_RESOURCE_I2C_I2C0_FREQUENCY_HZ;
        #endif
        Wire.begin(HYP_RESOURCE_I2C_I2C0_SDA_PIN, HYP_RESOURCE_I2C_I2C0_SCL_PIN, (uint32_t)freq);
        HYP_LOG_PRINTLN("[INFO] I2C I2C0 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 5. PWM Configuration
    // -------------------------------------------------------------------------
#if defined(HYP_RESOURCE_PWM_GPIO25) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO25_PWM))
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
        HYP_LOG_PRINTLN("[INFO] PWM GPIO25 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_PWM_GPIO26) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO26_PWM))
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
        HYP_LOG_PRINTLN("[INFO] PWM GPIO26 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_PWM_GPIO27) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO27_PWM))
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
        HYP_LOG_PRINTLN("[INFO] PWM GPIO27 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_PWM_GPIO32) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO32_PWM))
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
        HYP_LOG_PRINTLN("[INFO] PWM GPIO32 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_PWM_GPIO33) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO33_PWM))
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
        HYP_LOG_PRINTLN("[INFO] PWM GPIO33 initialized.");
    }
#endif

    // -------------------------------------------------------------------------
    // 6. ADC Configuration
    // -------------------------------------------------------------------------
#if defined(HYP_RESOURCE_ADC_GPIO32) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO32_ADC))
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
        HYP_LOG_PRINTLN("[INFO] ADC GPIO32 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_ADC_GPIO33) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO33_ADC))
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
        HYP_LOG_PRINTLN("[INFO] ADC GPIO33 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_ADC_GPIO34) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO34_ADC))
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
        HYP_LOG_PRINTLN("[INFO] ADC GPIO34 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_ADC_GPIO35) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO35_ADC))
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
        HYP_LOG_PRINTLN("[INFO] ADC GPIO35 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_ADC_GPIO36) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO36_ADC))
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
        HYP_LOG_PRINTLN("[INFO] ADC GPIO36 initialized.");
    }
#endif

#if defined(HYP_RESOURCE_ADC_GPIO39) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO39_ADC))
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
        HYP_LOG_PRINTLN("[INFO] ADC GPIO39 initialized.");
    }
#endif

#if ESP_ARDUINO_VERSION_MAJOR < 3
    if (next_pwm_channel > 16) {
        HYP_LOG_PRINTLN("[ERROR] Exceeded maximum number of PWM channels (16).");
        return -1;
    }
#endif

    HYP_LOG_PRINTLN("[INFO] ESP32 Hardware Initialized successfully.");
    return 0;
}

/* ==========================================================================
 * Sensor / Actuator Runtime Operations
 * ========================================================================== */

/**
 * Parse hardware resource ID (e.g., "adc.channel0", "pwm.motor0")
 * Returns the resource type (adc, gpio, pwm, etc.) and instance identifier.
 */
int parse_resource_id(const char *resource_id, char *type_out, size_t type_size, char *instance_out, size_t instance_size) {
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
    HYP_LOG_PRINT("[WARN] get_pin_for_resource: no configured pin for ");
    HYP_LOG_PRINTLN(key);
    return -1; /* resource not configured in board config */
}

/* Resource instance names are canonicalized at the boundary.  Existing board
 * configurations use both legacy uppercase bus names (for example, UART0)
 * and generated lowercase IDs (uart0); accepting either spelling preserves
 * those IDs while keeping all runtime lookups consistent. */
static bool canonical_instance_equals(const char *actual, const char *expected) {
    if (!actual || !expected) return false;
    while (*actual && *expected) {
        char actual_lower = (*actual >= 'A' && *actual <= 'Z') ? (char)(*actual - 'A' + 'a') : *actual;
        char expected_lower = (*expected >= 'A' && *expected <= 'Z') ? (char)(*expected - 'A' + 'a') : *expected;
        if (actual_lower != expected_lower) return false;
        ++actual;
        ++expected;
    }
    return *actual == '\0' && *expected == '\0';
}

/* Bus resources do not map to one GPIO. Resolve their existence from the
 * generated board configuration before attempting an operation. */
static bool is_configured_bus_resource(const char *resource_type, const char *instance) {
    if (!resource_type || !instance) return false;
    if (strcmp(resource_type, "uart") == 0) {
#ifdef HYP_SDK_UART0_ACTIVE
        /* HYP_RESOURCE_UART_UART0 alone is not sufficient: it is emitted
         * unconditionally for every ESP32 header regardless of whether this
         * desktop project reassigned GPIO1/GPIO3 to plain GPIO.
         * HYP_SDK_UART0_ACTIVE is the pin-function-aware derivation (see its
         * definition near the top of this file). */
        if (canonical_instance_equals(instance, "uart0")) return true;
#endif
#ifdef HYP_RESOURCE_UART_UART1
        if (canonical_instance_equals(instance, "uart1")) return true;
#endif
#ifdef HYP_RESOURCE_UART_UART2
        if (canonical_instance_equals(instance, "uart2")) return true;
#endif
        return false;
    }
    if (strcmp(resource_type, "spi") == 0) {
#ifdef HYP_RESOURCE_SPI_HSPI
        if (canonical_instance_equals(instance, "hspi")) return true;
#endif
#ifdef HYP_RESOURCE_SPI_VSPI
        if (canonical_instance_equals(instance, "vspi")) return true;
#endif
        return false;
    }
    if (strcmp(resource_type, "i2c") == 0) {
#ifdef HYP_RESOURCE_I2C_I2C0
        if (canonical_instance_equals(instance, "i2c0")) return true;
#endif
        return false;
    }
    return false;
}

static HardwareSerial *serial_for_resource(const char *instance) {
    if (canonical_instance_equals(instance, "uart0")) return &Serial;
    if (canonical_instance_equals(instance, "uart1")) return &Serial1;
    if (canonical_instance_equals(instance, "uart2")) return &Serial2;
    return NULL;
}

/* ==========================================================================
 * SDK-T6: SPI / I2C Bus Transaction Primitives
 * ========================================================================== */

/* Resolve an SPI instance name to its initialized bus handle plus the clock
 * frequency and mode captured at init time. Returns NULL when the instance is
 * unknown or the bus was never initialized (handle still NULL). */
static SPIClass *spi_bus_for_instance(const char *instance, uint32_t *out_freq, uint8_t *out_mode) {
#ifdef HYP_RESOURCE_SPI_HSPI
    if (canonical_instance_equals(instance, "hspi")) {
        if (out_freq) *out_freq = g_hspi_freq;
        if (out_mode) *out_mode = g_hspi_mode;
        return g_hspi_bus;
    }
#endif
#ifdef HYP_RESOURCE_SPI_VSPI
    if (canonical_instance_equals(instance, "vspi")) {
        if (out_freq) *out_freq = g_vspi_freq;
        if (out_mode) *out_mode = g_vspi_mode;
        return g_vspi_bus;
    }
#endif
    (void)out_freq; (void)out_mode;
    return NULL;
}

/* Resolve an SPI instance name to its chip-select GPIO from board config. */
static int spi_cs_pin_for_instance(const char *instance) {
#ifdef HYP_RESOURCE_SPI_HSPI_CS_PIN
    if (canonical_instance_equals(instance, "hspi")) return HYP_RESOURCE_SPI_HSPI_CS_PIN;
#endif
#ifdef HYP_RESOURCE_SPI_VSPI_CS_PIN
    if (canonical_instance_equals(instance, "vspi")) return HYP_RESOURCE_SPI_VSPI_CS_PIN;
#endif
    return -1;
}

int hyp_spi_transfer(const char *resource_id, const uint8_t *tx_buf, uint8_t *rx_buf, size_t len) {
    if (!resource_id || len == 0 || (!tx_buf && !rx_buf)) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }

    char resource_type[32];
    char instance[64];
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type),
                          instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }
    if (strcmp(resource_type, "spi") != 0) {
        return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
    }
    if (!is_configured_bus_resource("spi", instance)) {
        return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    uint32_t freq = 1000000;
    uint8_t mode = SPI_MODE0;
    SPIClass *bus = spi_bus_for_instance(instance, &freq, &mode);
    if (!bus) {
        /* Bus appears in the board map but hyp_esp32_hw_init() never created the
         * handle (init not run, or new SPIClass failed). Surface it explicitly. */
        HYP_LOG_PRINT("[ERROR] hyp_spi_transfer: SPI bus not initialized for ");
        HYP_LOG_PRINTLN(instance);
        return HYP_RUNTIME_NOT_INITIALIZED;
    }

    int cs = spi_cs_pin_for_instance(instance);
    if (cs < 0) {
        return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    bus->beginTransaction(SPISettings(freq, MSBFIRST, mode));
    digitalWrite(cs, LOW);
    for (size_t i = 0; i < len; i++) {
        uint8_t out_byte = tx_buf ? tx_buf[i] : 0x00;
        uint8_t in_byte = bus->transfer(out_byte);
        if (rx_buf) rx_buf[i] = in_byte;
    }
    digitalWrite(cs, HIGH);
    bus->endTransaction();
    return HYP_RUNTIME_OK;
}

/* I2C device address table (SDK-T6). Populated from HYP_I2C_DEVICE_TABLE, which
 * gen_board_config.js emits from the "devices" map under each I2C bus in
 * boards.yaml. Empty (sentinel only) when no named devices are configured. */
typedef struct { const char *device; uint8_t address; } hyp_i2c_device_entry_t;
static const hyp_i2c_device_entry_t hyp_i2c_device_table[] = {
#ifdef HYP_I2C_DEVICE_TABLE
    HYP_I2C_DEVICE_TABLE,
#endif
    { NULL, 0 } /* sentinel */
};

/* Look up a device's 7-bit slave address by semantic instance name. */
static int i2c_address_for_device(const char *instance, uint8_t *out_addr) {
    for (int i = 0; hyp_i2c_device_table[i].device != NULL; i++) {
        if (canonical_instance_equals(instance, hyp_i2c_device_table[i].device)) {
            if (out_addr) *out_addr = hyp_i2c_device_table[i].address;
            return 0;
        }
    }
    return -1;
}

/* Largest single I2C payload we accept. The Arduino TwoWire buffer is 128 bytes
 * on ESP32; capping below that (leaving room for the register-address byte)
 * guards against silent truncation inside Wire.write(). */
#define HYP_I2C_MAX_PAYLOAD 127

/* Register-less raw I2C transfer used by the generic sensor/actuator dispatch:
 * reads or writes `len` bytes to/from the device's current register pointer. */
static int i2c_raw_transfer(uint8_t dev_addr, uint8_t *data, size_t len, bool is_write) {
    if (is_write) {
        Wire.beginTransmission(dev_addr);
        Wire.write(data, len);
        uint8_t rc = Wire.endTransmission();
        if (rc != 0) {
            HYP_LOG_PRINT("[ERROR] i2c raw write NACK/timeout rc=");
            HYP_LOG_PRINTLN(rc);
            return HYP_RUNTIME_IO_ERROR;
        }
        return HYP_RUNTIME_OK;
    }
    size_t got = Wire.requestFrom((int)dev_addr, (int)len);
    if (got != len) {
        HYP_LOG_PRINTLN("[ERROR] i2c raw read short/timeout");
        return HYP_RUNTIME_IO_ERROR;
    }
    for (size_t i = 0; i < len; i++) data[i] = (uint8_t)Wire.read();
    return HYP_RUNTIME_OK;
}

int hyp_i2c_transact(const char *resource_id, uint8_t reg_addr, uint8_t *data, size_t len, bool is_write) {
    if (!resource_id || !data || len == 0) {
        return HYP_RUNTIME_INVALID_ARGUMENT;
    }
    if (len > HYP_I2C_MAX_PAYLOAD) {
        /* Out-of-range transfer length: would overrun the TwoWire buffer. */
        return HYP_RUNTIME_BUFFER_TOO_SMALL;
    }

    char resource_type[32];
    char instance[64];
    if (parse_resource_id(resource_id, resource_type, sizeof(resource_type),
                          instance, sizeof(instance)) != 0) {
        return HYP_RUNTIME_INVALID_RESOURCE_ID;
    }
    if (strcmp(resource_type, "i2c") != 0) {
        return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
    }

#ifndef HYP_RESOURCE_I2C_I2C0
    /* No I2C bus configured on this board -> Wire.begin() was never called. */
    return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
#else
    uint8_t dev_addr = 0;
    if (i2c_address_for_device(instance, &dev_addr) != 0) {
        HYP_LOG_PRINT("[WARN] hyp_i2c_transact: unknown I2C device ");
        HYP_LOG_PRINTLN(instance);
        return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
    }

    if (is_write) {
        Wire.beginTransmission(dev_addr);
        Wire.write(reg_addr);
        Wire.write(data, len);
        uint8_t rc = Wire.endTransmission();
        if (rc != 0) {
            HYP_LOG_PRINT("[ERROR] hyp_i2c_transact write NACK/timeout rc=");
            HYP_LOG_PRINTLN(rc);
            return HYP_RUNTIME_IO_ERROR;
        }
        return HYP_RUNTIME_OK;
    }

    /* Register read: address the register, issue a repeated start, then read. */
    Wire.beginTransmission(dev_addr);
    Wire.write(reg_addr);
    uint8_t rc = Wire.endTransmission(false); /* false = repeated start (no STOP) */
    if (rc != 0) {
        HYP_LOG_PRINT("[ERROR] hyp_i2c_transact reg-select NACK/timeout rc=");
        HYP_LOG_PRINTLN(rc);
        return HYP_RUNTIME_IO_ERROR;
    }
    size_t got = Wire.requestFrom((int)dev_addr, (int)len);
    if (got != len) {
        HYP_LOG_PRINTLN("[ERROR] hyp_i2c_transact read short/timeout");
        return HYP_RUNTIME_IO_ERROR;
    }
    for (size_t i = 0; i < len; i++) data[i] = (uint8_t)Wire.read();
    return HYP_RUNTIME_OK;
#endif
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

    if (strcmp(resource_type, "spi") == 0) {
        /* Full-duplex read: clock out zeros and capture the received bytes.
         * hyp_spi_transfer validates configuration and initialization state. */
        return hyp_spi_transfer(resource_id, NULL, (uint8_t *)out_value, value_size);
    }

    if (strcmp(resource_type, "i2c") == 0) {
        /* Register-less read of value_size bytes from the named device. Callers
         * needing register-addressed access use hyp_i2c_transact() directly. */
        uint8_t dev_addr = 0;
        if (i2c_address_for_device(instance, &dev_addr) != 0) {
            return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        }
        return i2c_raw_transfer(dev_addr, (uint8_t *)out_value, value_size, false);
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

    if (strcmp(resource_type, "spi") == 0) {
        /* Full-duplex write: clock out the supplied bytes, discard received. */
        return hyp_spi_transfer(resource_id, (const uint8_t *)in_value, NULL, value_size);
    }

    if (strcmp(resource_type, "i2c") == 0) {
        /* Register-less write of value_size bytes to the named device. */
        uint8_t dev_addr = 0;
        if (i2c_address_for_device(instance, &dev_addr) != 0) {
            return HYP_RUNTIME_RESOURCE_NOT_CONFIGURED;
        }
        return i2c_raw_transfer(dev_addr, (uint8_t *)in_value, value_size, true);
    }

    return HYP_RUNTIME_UNSUPPORTED_RESOURCE;
}
