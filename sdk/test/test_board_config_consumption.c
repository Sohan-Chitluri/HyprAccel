/*
 * test_board_config_consumption.c
 *
 * FW-P1..P6: Host-side compilation + runtime verification that:
 *   1. gen_board_config.js emits the expected HYP_RESOURCE_*_PIN macros.
 *   2. The generated values match boards.yaml pin definitions.
 *   3. No hardcoded demo pin assignments exist (verified by absence of legacy
 *      channel0/button0/motor0 keys in get_pin_for_resource).
 *
 * This file compiles and runs on the host (no Arduino environment required).
 * It includes the generated board config header directly.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include the generated board config header — the primary subject under test */
#include "../../boards/codegen/hyp_board_config.h"

/* -------------------------------------------------------------------------
 * Test helpers
 * ---------------------------------------------------------------------- */
static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_EQ(label, actual, expected) do { \
    if ((actual) == (expected)) { \
        printf("[PASS] %-50s  val=%d\n", label, (int)(actual)); \
        g_passed++; \
    } else { \
        printf("[FAIL] %-50s  expected=%d  got=%d\n", label, (int)(expected), (int)(actual)); \
        g_failed++; \
    } \
} while (0)

#define ASSERT_DEFINED(label, macro_val) do { \
    printf("[PASS] %-50s  defined=%d\n", label, (int)(macro_val)); \
    g_passed++; \
} while (0)

/* -------------------------------------------------------------------------
 * FW-P2 UART pin verification
 * Expected from boards.yaml:
 *   uart0: tx=GPIO1  rx=GPIO3
 *   uart1: tx=GPIO10 rx=GPIO9
 *   uart2: tx=GPIO17 rx=GPIO16
 * ---------------------------------------------------------------------- */
static void test_uart_pins(void)
{
    printf("\n--- FW-P2: UART Pin Macros ---\n");

#ifdef HYP_RESOURCE_UART_UART0_TX_PIN
    ASSERT_EQ("UART0 TX pin", HYP_RESOURCE_UART_UART0_TX_PIN,  1);
    ASSERT_EQ("UART0 RX pin", HYP_RESOURCE_UART_UART0_RX_PIN,  3);
#else
    printf("[FAIL] HYP_RESOURCE_UART_UART0_TX_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_UART_UART1_TX_PIN
    ASSERT_EQ("UART1 TX pin", HYP_RESOURCE_UART_UART1_TX_PIN, 10);
    ASSERT_EQ("UART1 RX pin", HYP_RESOURCE_UART_UART1_RX_PIN,  9);
#else
    printf("[FAIL] HYP_RESOURCE_UART_UART1_TX_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_UART_UART2_TX_PIN
    ASSERT_EQ("UART2 TX pin", HYP_RESOURCE_UART_UART2_TX_PIN, 17);
    ASSERT_EQ("UART2 RX pin", HYP_RESOURCE_UART_UART2_RX_PIN, 16);
#else
    printf("[FAIL] HYP_RESOURCE_UART_UART2_TX_PIN not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * FW-P3 SPI pin verification
 * Expected from boards.yaml:
 *   hspi: sck=GPIO14  mosi=GPIO13  miso=GPIO12  cs=GPIO15
 *   vspi: sck=GPIO18  mosi=GPIO23  miso=GPIO19  cs=GPIO5
 * ---------------------------------------------------------------------- */
static void test_spi_pins(void)
{
    printf("\n--- FW-P3: SPI Pin Macros ---\n");

#ifdef HYP_RESOURCE_SPI_HSPI_SCK_PIN
    ASSERT_EQ("SPI HSPI SCK  pin", HYP_RESOURCE_SPI_HSPI_SCK_PIN,  14);
    ASSERT_EQ("SPI HSPI MOSI pin", HYP_RESOURCE_SPI_HSPI_MOSI_PIN, 13);
    ASSERT_EQ("SPI HSPI MISO pin", HYP_RESOURCE_SPI_HSPI_MISO_PIN, 12);
    ASSERT_EQ("SPI HSPI CS   pin", HYP_RESOURCE_SPI_HSPI_CS_PIN,   15);
#else
    printf("[FAIL] HYP_RESOURCE_SPI_HSPI_SCK_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_SPI_VSPI_SCK_PIN
    ASSERT_EQ("SPI VSPI SCK  pin", HYP_RESOURCE_SPI_VSPI_SCK_PIN,  18);
    ASSERT_EQ("SPI VSPI MOSI pin", HYP_RESOURCE_SPI_VSPI_MOSI_PIN, 23);
    ASSERT_EQ("SPI VSPI MISO pin", HYP_RESOURCE_SPI_VSPI_MISO_PIN, 19);
    ASSERT_EQ("SPI VSPI CS   pin", HYP_RESOURCE_SPI_VSPI_CS_PIN,    5);
#else
    printf("[FAIL] HYP_RESOURCE_SPI_VSPI_SCK_PIN not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * FW-P4 I2C pin verification
 * Expected from boards.yaml:
 *   i2c0: scl=GPIO22  sda=GPIO21
 * ---------------------------------------------------------------------- */
static void test_i2c_pins(void)
{
    printf("\n--- FW-P4: I2C Pin Macros ---\n");

#ifdef HYP_RESOURCE_I2C_I2C0_SDA_PIN
    ASSERT_EQ("I2C0 SDA pin", HYP_RESOURCE_I2C_I2C0_SDA_PIN, 21);
    ASSERT_EQ("I2C0 SCL pin", HYP_RESOURCE_I2C_I2C0_SCL_PIN, 22);
#else
    printf("[FAIL] HYP_RESOURCE_I2C_I2C0_SDA_PIN not defined\n"); g_failed++;
#endif

    /* SDK-T6: named I2C device slave-address macro emitted from boards.yaml */
#ifdef HYP_RESOURCE_I2C_IMU_ADDRESS
    ASSERT_EQ("I2C imu device address", HYP_RESOURCE_I2C_IMU_ADDRESS, 0x68);
#else
    printf("[FAIL] HYP_RESOURCE_I2C_IMU_ADDRESS not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * FW-P5 PWM pin verification
 * Expected: GPIO25=25, GPIO26=26, GPIO27=27, GPIO32=32, GPIO33=33
 * ---------------------------------------------------------------------- */
static void test_pwm_pins(void)
{
    printf("\n--- FW-P5: PWM Pin Macros ---\n");

#ifdef HYP_RESOURCE_PWM_GPIO25_PIN
    ASSERT_EQ("PWM GPIO25 pin", HYP_RESOURCE_PWM_GPIO25_PIN, 25);
#else
    printf("[FAIL] HYP_RESOURCE_PWM_GPIO25_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_PWM_GPIO26_PIN
    ASSERT_EQ("PWM GPIO26 pin", HYP_RESOURCE_PWM_GPIO26_PIN, 26);
#else
    printf("[FAIL] HYP_RESOURCE_PWM_GPIO26_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_PWM_GPIO27_PIN
    ASSERT_EQ("PWM GPIO27 pin", HYP_RESOURCE_PWM_GPIO27_PIN, 27);
#else
    printf("[FAIL] HYP_RESOURCE_PWM_GPIO27_PIN not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * FW-P6 ADC pin verification
 * Expected: GPIO32..GPIO39 pin numbers match their GPIO indices
 * ---------------------------------------------------------------------- */
static void test_adc_pins(void)
{
    printf("\n--- FW-P6: ADC Pin Macros ---\n");

#ifdef HYP_RESOURCE_ADC_GPIO32_PIN
    ASSERT_EQ("ADC GPIO32 pin", HYP_RESOURCE_ADC_GPIO32_PIN, 32);
#else
    printf("[FAIL] HYP_RESOURCE_ADC_GPIO32_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_ADC_GPIO34_PIN
    ASSERT_EQ("ADC GPIO34 pin", HYP_RESOURCE_ADC_GPIO34_PIN, 34);
#else
    printf("[FAIL] HYP_RESOURCE_ADC_GPIO34_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_ADC_GPIO39_PIN
    ASSERT_EQ("ADC GPIO39 pin", HYP_RESOURCE_ADC_GPIO39_PIN, 39);
#else
    printf("[FAIL] HYP_RESOURCE_ADC_GPIO39_PIN not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * FW-P1 GPIO pin verification
 * ---------------------------------------------------------------------- */
static void test_gpio_pins(void)
{
    printf("\n--- FW-P1: GPIO Pin Macros ---\n");

#ifdef HYP_RESOURCE_GPIO_GPIO4_PIN
    ASSERT_EQ("GPIO4 pin",  HYP_RESOURCE_GPIO_GPIO4_PIN,  4);
#else
    printf("[FAIL] HYP_RESOURCE_GPIO_GPIO4_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO21_PIN
    ASSERT_EQ("GPIO21 pin", HYP_RESOURCE_GPIO_GPIO21_PIN, 21);
#else
    printf("[FAIL] HYP_RESOURCE_GPIO_GPIO21_PIN not defined\n"); g_failed++;
#endif

#ifdef HYP_RESOURCE_GPIO_GPIO22_PIN
    ASSERT_EQ("GPIO22 pin", HYP_RESOURCE_GPIO_GPIO22_PIN, 22);
#else
    printf("[FAIL] HYP_RESOURCE_GPIO_GPIO22_PIN not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * Verify no hardcoded legacy channel-name constants remain
 * The old get_pin_for_resource() used "channel0", "button0", "motor0" etc.
 * These must not appear as defined C constants (they were hardcoded ints).
 * We check by searching the header for telltale legacy patterns.
 * ---------------------------------------------------------------------- */
static void test_no_hardcoded_legacy_pins(void)
{
    printf("\n--- No hardcoded legacy demo pin assignments ---\n");

    /* Read the generated header at runtime and scan for banned patterns */
    const char *header_path = "../../boards/codegen/hyp_board_config.h";
    FILE *f = fopen(header_path, "r");
    if (!f) {
        /* fallback: try current directory */
        f = fopen("hyp_board_config.h", "r");
    }
    if (!f) {
        printf("[SKIP] Cannot open hyp_board_config.h for text scan (path unavailable)\n");
        return;
    }

    const char *banned[] = {
        "channel0", "button0", "motor0", "servo0",
        /* Old standalone defines (no _PIN suffix): ban the exact form */
        "#define UART1_TX ", "#define UART2_TX ",
        "#define UART1_RX ", "#define UART2_RX ",
        NULL
    };

    char line[512];
    int found_banned = 0;
    while (fgets(line, sizeof(line), f)) {
        for (int i = 0; banned[i]; i++) {
            if (strstr(line, banned[i])) {
                printf("[FAIL] Banned legacy string '%s' found in header: %s",
                       banned[i], line);
                g_failed++;
                found_banned = 1;
            }
        }
    }
    fclose(f);

    if (!found_banned) {
        printf("[PASS] %-50s\n", "No hardcoded legacy pin names in generated header");
        g_passed++;
    }
}

/* -------------------------------------------------------------------------
 * Board identity checks
 * ---------------------------------------------------------------------- */
static void test_board_identity(void)
{
    printf("\n--- Board Identity ---\n");

#ifdef HYP_BOARD_ARCH_XTENSA_LX6
    printf("[PASS] %-50s\n", "HYP_BOARD_ARCH_XTENSA_LX6 defined"); g_passed++;
#else
    printf("[FAIL] HYP_BOARD_ARCH_XTENSA_LX6 not defined\n"); g_failed++;
#endif

#ifdef HYP_SYSCLK_MHZ
    ASSERT_EQ("HYP_SYSCLK_MHZ", HYP_SYSCLK_MHZ, 240);
#else
    printf("[FAIL] HYP_SYSCLK_MHZ not defined\n"); g_failed++;
#endif

#ifdef HYP_HAS_HW_CORDIC
    printf("[PASS] %-50s\n", "HYP_HAS_HW_CORDIC defined"); g_passed++;
#else
    printf("[FAIL] HYP_HAS_HW_CORDIC not defined\n"); g_failed++;
#endif
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(void)
{
    printf("=== HyprAccel FW-P1..P6 Board Config Consumption Tests ===\n");
    printf("    Header: boards/codegen/hyp_board_config.h\n");

    test_board_identity();
    test_gpio_pins();
    test_uart_pins();
    test_spi_pins();
    test_i2c_pins();
    test_pwm_pins();
    test_adc_pins();
    test_no_hardcoded_legacy_pins();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
