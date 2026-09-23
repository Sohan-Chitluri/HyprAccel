/*
 * test_pinfunc_gating.cpp — desktop pin-function init gating tests.
 *
 * REQUIREMENTS (see the task description feeding this change):
 *   - hyp_board_config.h headers with no HYP_PINFUNC_* macros (web-only
 *     projects) must keep initializing every HYP_RESOURCE_* peripheral
 *     exactly as before (legacy behaviour, byte-for-byte unchanged).
 *   - Headers carrying HYP_PINFUNC_* macros (desktop-saved projects) must
 *     initialize ONLY the peripherals/pins whose function macro is present.
 *   - HYP_CLOCK_CONFIG_PRESENT must drive a setCpuFrequencyMhz(HYP_CPU_FREQ_MHZ)
 *     call early in hyp_esp32_hw_init(); HYP_CLOCK_NONSTANDARD_CPU_FREQ must
 *     log a warning (not fail); setCpuFrequencyMhz() returning false must log
 *     an error and continue.
 *
 * This same .cpp is compiled FOUR separate times (see the `pinfunc-test`
 * Makefile target), each time linked against a differently-configured build
 * of sdk/src/hyp_esp32_hw.cpp (achieved by mirroring the real repo layout
 * into a scratch tree so hyp_esp32_hw.cpp's hardcoded
 * "../../boards/codegen/hyp_board_config.h" include resolves to one of the
 * fixtures under sdk/test/fixtures/ instead of the real committed header):
 *
 *   -DHYP_PINFUNC_VARIANT_LEGACY   -> real committed boards/codegen/hyp_board_config.h
 *                                     (no PINFUNC/clock macros at all)
 *   -DHYP_PINFUNC_VARIANT_DESKTOP  -> fixtures/board_config_pinfunc.h
 *                                     (PINFUNC + standard 160 MHz clock block)
 *   -DHYP_PINFUNC_VARIANT_NONSTD   -> fixtures/board_config_pinfunc_nonstd.h
 *                                     (PINFUNC + nonstandard 100 MHz clock block)
 *   -DHYP_PINFUNC_VARIANT_MINIMAL  -> fixtures/board_config_pinfunc_minimal.h
 *                                     (PINFUNC, only gpio.GPIO4 assigned, no clock)
 *
 * All four fixtures were generated with the real codegen
 * (boards/codegen/gen_board_config.js --project <scratch dir>), the same
 * path desktop-saved projects go through; see the Makefile comment above
 * the fixture rules for the exact assignments used.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "mock/Arduino.h"
#include "mock/SPI.h"
#include "mock/Wire.h"

extern "C" {
#include "../include/hyprccel.h"
#include "../include/hyp_esp32_hw.h"
}

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); g_passed++; } \
    else      { printf("[FAIL] %s  (line %d)\n", name, __LINE__); g_failed++; } \
} while (0)

static void reset_all_mocks(void)
{
    mock_pin_tracking_reset();
    mock_cpu_freq_reset();
    mock_spi_reset();
    mock_wire_reset();
    Serial.reset();
    Serial1.reset();
    Serial2.reset();
}

/* ---- LEGACY: no HYP_PINFUNC_* macros -> unchanged behaviour --------------
 * The real committed boards/codegen/hyp_board_config.h defines
 * HYP_RESOURCE_* for every pin/bus in boards.yaml and carries no PINFUNC or
 * clock macros. hyp_esp32_hw_init() must initialize everything, exactly as
 * it did before this change (HYP_SDK_PINFUNC_MODE must stay off). */
#if defined(HYP_PINFUNC_VARIANT_LEGACY)
static void run(void)
{
    reset_all_mocks();
    int rc = hyp_esp32_hw_init();
    CHECK(rc == 0, "legacy: hyp_esp32_hw_init -> 0");

    /* Every GPIO pin in boards.yaml's pins.gpio list gets pinMode()'d. */
    const int gpio_pins[] = { 0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19,
                               21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39 };
    bool all_gpio_configured = true;
    for (int p : gpio_pins) if (!g_mock_pinmode_called[p]) all_gpio_configured = false;
    CHECK(all_gpio_configured, "legacy: every board GPIO pin got pinMode()");

    /* PWM pins all attached. */
    const int pwm_pins[] = { 25, 26, 27, 32, 33 };
    bool all_pwm_attached = true;
    for (int p : pwm_pins) if (!g_mock_ledc_attach_called[p]) all_pwm_attached = false;
    CHECK(all_pwm_attached, "legacy: every board PWM pin got ledcAttach()");

    /* UART1 (tx=GPIO10/rx=GPIO9) is unconditionally skipped by hyp_esp32_hw.cpp:
     * those pins are wired to the internal SPI flash in QIO mode on this
     * board, so Serial1.begin() must never be called, PINFUNC mode or not.
     * UART2 (tx=GPIO17/rx=GPIO16) has no such conflict and begins normally. */
    CHECK(!Serial1.began, "legacy: UART1 begin() still skipped (flash pin conflict, unrelated to PINFUNC)");
    CHECK(Serial.log_contains("UART1 skipped"), "legacy: UART1 flash-conflict warning logged");
    CHECK(Serial2.began, "legacy: UART2 begin() called");

    /* Both SPI buses and the I2C bus initialize (observable through the
     * runtime primitives, since the bus handles are file-local statics). */
    uint8_t buf[2] = { 0 };
    CHECK(hyp_spi_transfer("spi.hspi", buf, buf, 2) == HYP_RUNTIME_OK, "legacy: SPI HSPI initialized");
    CHECK(hyp_spi_transfer("spi.vspi", buf, buf, 2) == HYP_RUNTIME_OK, "legacy: SPI VSPI initialized");
    CHECK(g_mock_wire_began, "legacy: I2C I2C0 Wire.begin() called");

    /* No clock block in this header -> setCpuFrequencyMhz() never called. */
    CHECK(g_mock_cpu_freq_call_count == 0, "legacy: setCpuFrequencyMhz() not called (no clock block)");
}
#endif

/* ---- DESKTOP: PINFUNC + standard 160 MHz clock ---------------------------
 * Fixture assigns: gpio.GPIO4 (gpio), pwm.GPIO25 (pwm), adc.GPIO32 (adc),
 * uart.uart0 tx=GPIO1/rx=GPIO3, i2c.i2c0 sda=GPIO21/scl=GPIO22,
 * spi.hspi sck=GPIO14/mosi=GPIO13/miso=GPIO12/cs=GPIO15. Resolved CPU freq
 * 160 MHz (standard). Everything else (VSPI, UART1, UART2, all other GPIO/
 * PWM/ADC pins) must stay uninitialized. */
#if defined(HYP_PINFUNC_VARIANT_DESKTOP)
static void run(void)
{
    reset_all_mocks();
    int rc = hyp_esp32_hw_init();
    CHECK(rc == 0, "desktop: hyp_esp32_hw_init -> 0");

    /* Clock: applied, standard frequency, no warning, applied-frequency log. */
    CHECK(g_mock_cpu_freq_call_count == 1, "desktop: setCpuFrequencyMhz() called exactly once");
    CHECK(g_mock_cpu_freq_last_mhz == 160, "desktop: setCpuFrequencyMhz(160)");
    CHECK(!Serial.log_contains("[WARN]"), "desktop: no nonstandard-frequency warning for 160 MHz");
    CHECK(!Serial.log_contains("[ERROR]"), "desktop: no clock error (setCpuFrequencyMhz succeeded)");
    CHECK(Serial.log_contains("CPU frequency set to"), "desktop: applied frequency logged");

    /* GPIO: only the assigned pin (GPIO4) initializes as plain GPIO. */
    CHECK(g_mock_pinmode_called[4], "desktop: GPIO4 (assigned gpio) got pinMode()");
    CHECK(!g_mock_pinmode_called[0], "desktop: GPIO0 (unassigned) did NOT get pinMode()");
    CHECK(!g_mock_pinmode_called[2], "desktop: GPIO2 (unassigned) did NOT get pinMode()");
    CHECK(!g_mock_pinmode_called[23], "desktop: GPIO23 (unassigned) did NOT get pinMode()");
    /* Bus pins (GPIO12/13/14/15 = HSPI, GPIO21/22 = I2C0) must NOT be
     * configured as plain GPIO even though HYP_RESOURCE_GPIO_GPIOn exists
     * for them at the board level -- their pinfunc is spi/i2c, not gpio. */

    /* PWM: only GPIO25 attaches; GPIO26/27/32/33 do not. */
    CHECK(g_mock_ledc_attach_called[25], "desktop: PWM GPIO25 (assigned) got ledcAttach()");
    CHECK(!g_mock_ledc_attach_called[26], "desktop: PWM GPIO26 (unassigned) did NOT get ledcAttach()");
    CHECK(!g_mock_ledc_attach_called[27], "desktop: PWM GPIO27 (unassigned) did NOT get ledcAttach()");
    CHECK(!g_mock_ledc_attach_called[32], "desktop: PWM GPIO32 (unassigned; GPIO32 is the ADC pin here) did NOT get ledcAttach()");
    CHECK(!g_mock_ledc_attach_called[33], "desktop: PWM GPIO33 (unassigned) did NOT get ledcAttach()");

    /* ADC: only GPIO32 configured (pinMode(pin, ANALOG) inside the ADC block). */
    CHECK(g_mock_pinmode_called[32], "desktop: ADC GPIO32 (assigned) got pinMode(ANALOG)");
    CHECK(!g_mock_pinmode_called[33], "desktop: ADC GPIO33 (unassigned) did NOT get pinMode()");
    CHECK(!g_mock_pinmode_called[34], "desktop: ADC GPIO34 (unassigned) did NOT get pinMode()");

    /* UART0 (GPIO1 tx + GPIO3 rx both assigned) begins; UART1/UART2 do not. */
    CHECK(Serial.log_contains("UART0 already initialised"), "desktop: UART0 init path taken");
    CHECK(!Serial1.began, "desktop: UART1 (unassigned) begin() NOT called");
    CHECK(!Serial2.began, "desktop: UART2 (unassigned) begin() NOT called");

    /* SPI: HSPI (all 4 pins assigned) initializes; VSPI (unassigned) does not. */
    uint8_t buf[2] = { 0 };
    CHECK(hyp_spi_transfer("spi.hspi", buf, buf, 2) == HYP_RUNTIME_OK, "desktop: SPI HSPI (assigned) initialized");
    CHECK(hyp_spi_transfer("spi.vspi", buf, buf, 2) == HYP_RUNTIME_NOT_INITIALIZED, "desktop: SPI VSPI (unassigned) NOT initialized");

    /* I2C: I2C0 (both pins assigned) begins. */
    CHECK(g_mock_wire_began, "desktop: I2C I2C0 (assigned) Wire.begin() called");
}
#endif

/* ---- NONSTD: PINFUNC + nonstandard 100 MHz clock --------------------------
 * Same assignments as the desktop fixture, but resolved CPU freq is 100 MHz
 * (not 80/160/240) -> HYP_CLOCK_NONSTANDARD_CPU_FREQ is defined. Must warn,
 * not fail, and still apply the frequency (mock setCpuFrequencyMhz succeeds
 * by default). */
#if defined(HYP_PINFUNC_VARIANT_NONSTD)
static void run(void)
{
    reset_all_mocks();
    int rc = hyp_esp32_hw_init();
    CHECK(rc == 0, "nonstd: hyp_esp32_hw_init -> 0 (nonstandard freq must not fail init)");

    CHECK(g_mock_cpu_freq_call_count == 1, "nonstd: setCpuFrequencyMhz() called exactly once");
    CHECK(g_mock_cpu_freq_last_mhz == 100, "nonstd: setCpuFrequencyMhz(100)");
    CHECK(Serial.log_contains("[WARN]"), "nonstd: nonstandard-frequency warning logged");
    CHECK(!Serial.log_contains("[ERROR]"), "nonstd: no error (setCpuFrequencyMhz still succeeded)");
    CHECK(Serial.log_contains("CPU frequency set to"), "nonstd: applied frequency still logged");
}
#endif

/* ---- MINIMAL: PINFUNC, only gpio.GPIO4 assigned, no clock.json -----------
 * Strongest negative test: with only one pin function present, NOTHING else
 * (UART0/1/2, both SPI buses, I2C0, every PWM/ADC pin, every other GPIO)
 * may initialize, and setCpuFrequencyMhz() must never be called (no
 * HYP_CLOCK_CONFIG_PRESENT in this header). */
#if defined(HYP_PINFUNC_VARIANT_MINIMAL)
static void run(void)
{
    reset_all_mocks();
    int rc = hyp_esp32_hw_init();
    CHECK(rc == 0, "minimal: hyp_esp32_hw_init -> 0");

    CHECK(g_mock_cpu_freq_call_count == 0, "minimal: setCpuFrequencyMhz() not called (no clock block)");

    CHECK(g_mock_pinmode_called[4], "minimal: GPIO4 (assigned) got pinMode()");
    const int other_gpio[] = { 0, 2, 5, 12, 13, 14, 15, 16, 17, 18, 19,
                                21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39 };
    bool any_other_configured = false;
    for (int p : other_gpio) if (g_mock_pinmode_called[p]) any_other_configured = true;
    CHECK(!any_other_configured, "minimal: no other GPIO pin got pinMode()");

    bool any_pwm_attached = false;
    for (int p : { 25, 26, 27, 32, 33 }) if (g_mock_ledc_attach_called[p]) any_pwm_attached = true;
    CHECK(!any_pwm_attached, "minimal: no PWM pin got ledcAttach()");

    CHECK(!Serial.log_contains("UART0 already initialised"), "minimal: UART0 NOT initialized");
    CHECK(!Serial1.began, "minimal: UART1 NOT initialized");
    CHECK(!Serial2.began, "minimal: UART2 NOT initialized");

    uint8_t buf[2] = { 0 };
    CHECK(hyp_spi_transfer("spi.hspi", buf, buf, 2) == HYP_RUNTIME_NOT_INITIALIZED, "minimal: SPI HSPI NOT initialized");
    CHECK(hyp_spi_transfer("spi.vspi", buf, buf, 2) == HYP_RUNTIME_NOT_INITIALIZED, "minimal: SPI VSPI NOT initialized");
    CHECK(!g_mock_wire_began, "minimal: I2C I2C0 NOT initialized");
}
#endif

/* ---- CPU-frequency-rejected path -------------------------------------
 * Reuses the DESKTOP variant's clock block, but this run forces the mock
 * setCpuFrequencyMhz() to return false (as real ESP-IDF does when the
 * requested value is invalid for the chip's XTAL) and asserts the error is
 * logged, not silently swallowed, and init still completes. Compiled only
 * into the DESKTOP binary (any clock-bearing fixture would do). */
#if defined(HYP_PINFUNC_VARIANT_DESKTOP)
static void run_cpu_freq_rejected(void)
{
    reset_all_mocks();
    g_mock_cpu_freq_result = false; /* reset_all_mocks() runs first; override after */
    int rc = hyp_esp32_hw_init();
    CHECK(rc == 0, "desktop/rejected-freq: hyp_esp32_hw_init -> 0 (must not hard-fail)");
    CHECK(g_mock_cpu_freq_call_count == 1, "desktop/rejected-freq: setCpuFrequencyMhz() still called");
    CHECK(Serial.log_contains("[ERROR]"), "desktop/rejected-freq: rejection logged as [ERROR]");
    CHECK(!Serial.log_contains("CPU frequency set to"), "desktop/rejected-freq: no 'applied' log on rejection");
}
#endif

int main(void)
{
    printf("=== HyprAccel PINFUNC gating tests ===\n\n");
    run();
#if defined(HYP_PINFUNC_VARIANT_DESKTOP)
    run_cpu_freq_rejected();
#endif
    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
