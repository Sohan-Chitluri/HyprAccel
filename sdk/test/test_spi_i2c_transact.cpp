/*
 * test_spi_i2c_transact.cpp — HyprAccel SDK-T6 test harness
 *
 * Exercises the real hyp_spi_transfer() / hyp_i2c_transact() implementations in
 * sdk/src/hyp_esp32_hw.cpp on the host, using the Arduino/SPI/Wire stubs under
 * sdk/test/mock/. Covers full-duplex SPI, register-addressed I2C read/write,
 * the generic sensor/actuator dispatch routing, and the mandated error paths:
 * I2C NACK/timeout, SPI-bus-not-initialized, and out-of-range transfer length.
 *
 * Build: see the `spi-i2c-test` target in the Makefile (compiled as C++ so the
 * mock control helpers are visible).
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

/* ---- SPI: bus-not-initialized must be reported, not silent ---------------- */
static void test_spi_not_initialized(void)
{
    /* Runs BEFORE hyp_esp32_hw_init(): the bus handle is still NULL. */
    uint8_t tx[2] = { 0x01, 0x02 };
    uint8_t rx[2] = { 0 };
    int rc = hyp_spi_transfer("spi.hspi", tx, rx, sizeof(tx));
    CHECK(rc == HYP_RUNTIME_NOT_INITIALIZED, "spi_transfer before init -> NOT_INITIALIZED");
}

/* ---- SPI: argument / resource validation ---------------------------------- */
static void test_spi_arg_validation(void)
{
    uint8_t buf[2] = { 0 };
    CHECK(hyp_spi_transfer(NULL, buf, buf, 2) == HYP_RUNTIME_INVALID_ARGUMENT,
          "spi_transfer null id -> INVALID_ARGUMENT");
    CHECK(hyp_spi_transfer("spi.hspi", buf, buf, 0) == HYP_RUNTIME_INVALID_ARGUMENT,
          "spi_transfer len==0 -> INVALID_ARGUMENT");
    CHECK(hyp_spi_transfer("spi.hspi", NULL, NULL, 2) == HYP_RUNTIME_INVALID_ARGUMENT,
          "spi_transfer no tx and no rx -> INVALID_ARGUMENT");
    CHECK(hyp_spi_transfer("nodots", buf, buf, 2) == HYP_RUNTIME_INVALID_RESOURCE_ID,
          "spi_transfer malformed id -> INVALID_RESOURCE_ID");
    CHECK(hyp_spi_transfer("gpio.GPIO4", buf, buf, 2) == HYP_RUNTIME_UNSUPPORTED_RESOURCE,
          "spi_transfer non-spi type -> UNSUPPORTED_RESOURCE");
}

/* ---- SPI: full-duplex exchange -------------------------------------------- */
static void test_spi_full_duplex(void)
{
    mock_spi_reset();
    const uint8_t resp[3] = { 0x11, 0x22, 0x33 };
    mock_spi_set_response(resp, 3);

    uint8_t tx[3] = { 0xAA, 0xBB, 0xCC };
    uint8_t rx[3] = { 0 };
    int rc = hyp_spi_transfer("spi.hspi", tx, rx, 3);

    CHECK(rc == HYP_RUNTIME_OK, "spi_transfer full-duplex -> OK");
    CHECK(g_mock_spi_tx_count == 3 && g_mock_spi_tx[0] == 0xAA &&
          g_mock_spi_tx[1] == 0xBB && g_mock_spi_tx[2] == 0xCC,
          "spi_transfer clocked out tx bytes");
    CHECK(rx[0] == 0x11 && rx[1] == 0x22 && rx[2] == 0x33,
          "spi_transfer captured rx bytes");
    CHECK(g_mock_spi_begin_count == 1 && g_mock_spi_end_count == 1,
          "spi_transfer wrapped in begin/endTransaction");
    CHECK(g_mock_last_cs_val == HIGH, "spi_transfer released CS high after xfer");
}

/* ---- SPI: read-only clocks zeros ------------------------------------------ */
static void test_spi_read_only(void)
{
    mock_spi_reset();
    const uint8_t resp[2] = { 0x5A, 0xA5 };
    mock_spi_set_response(resp, 2);

    uint8_t rx[2] = { 0 };
    int rc = hyp_spi_transfer("spi.hspi", NULL, rx, 2);

    CHECK(rc == HYP_RUNTIME_OK, "spi_transfer read-only -> OK");
    CHECK(g_mock_spi_tx[0] == 0x00 && g_mock_spi_tx[1] == 0x00,
          "spi_transfer read-only clocks zeros");
    CHECK(rx[0] == 0x5A && rx[1] == 0xA5, "spi_transfer read-only captured rx");
}

/* ---- I2C: argument / range validation ------------------------------------- */
static void test_i2c_arg_validation(void)
{
    uint8_t buf[4] = { 0 };
    CHECK(hyp_i2c_transact(NULL, 0x00, buf, 4, true) == HYP_RUNTIME_INVALID_ARGUMENT,
          "i2c_transact null id -> INVALID_ARGUMENT");
    CHECK(hyp_i2c_transact("i2c.imu", 0x00, NULL, 4, true) == HYP_RUNTIME_INVALID_ARGUMENT,
          "i2c_transact null data -> INVALID_ARGUMENT");
    CHECK(hyp_i2c_transact("i2c.imu", 0x00, buf, 0, true) == HYP_RUNTIME_INVALID_ARGUMENT,
          "i2c_transact len==0 -> INVALID_ARGUMENT");
    uint8_t big[200] = { 0 };
    CHECK(hyp_i2c_transact("i2c.imu", 0x00, big, 200, true) == HYP_RUNTIME_BUFFER_TOO_SMALL,
          "i2c_transact oversized len -> BUFFER_TOO_SMALL");
    CHECK(hyp_i2c_transact("i2c.unknown", 0x00, buf, 4, true) == HYP_RUNTIME_RESOURCE_NOT_CONFIGURED,
          "i2c_transact unknown device -> RESOURCE_NOT_CONFIGURED");
    CHECK(hyp_i2c_transact("spi.hspi", 0x00, buf, 4, true) == HYP_RUNTIME_UNSUPPORTED_RESOURCE,
          "i2c_transact non-i2c type -> UNSUPPORTED_RESOURCE");
}

/* ---- I2C: register write success ------------------------------------------ */
static void test_i2c_write_ok(void)
{
    mock_wire_reset();
    uint8_t data[2] = { 0xDE, 0xAD };
    int rc = hyp_i2c_transact("i2c.imu", 0x6B, data, 2, true);

    CHECK(rc == HYP_RUNTIME_OK, "i2c_transact write -> OK");
    CHECK(g_mock_wire_addr == 0x68, "i2c_transact resolved imu -> addr 0x68");
    CHECK(g_mock_wire_tx_count == 3 && g_mock_wire_tx[0] == 0x6B &&
          g_mock_wire_tx[1] == 0xDE && g_mock_wire_tx[2] == 0xAD,
          "i2c_transact write sent reg then data");
}

/* ---- I2C: NACK / timeout must surface as IO_ERROR ------------------------- */
static void test_i2c_write_nack(void)
{
    mock_wire_reset();
    mock_wire_set_nack(2); /* NACK on address */
    uint8_t data[1] = { 0x00 };
    int rc = hyp_i2c_transact("i2c.imu", 0x6B, data, 1, true);
    CHECK(rc == HYP_RUNTIME_IO_ERROR, "i2c_transact write NACK -> IO_ERROR");
}

/* ---- I2C: register read success ------------------------------------------- */
static void test_i2c_read_ok(void)
{
    mock_wire_reset();
    const uint8_t src[2] = { 0x19, 0x82 };
    mock_wire_set_read_data(src, 2);

    uint8_t data[2] = { 0 };
    int rc = hyp_i2c_transact("i2c.imu", 0x3B, data, 2, false);

    CHECK(rc == HYP_RUNTIME_OK, "i2c_transact read -> OK");
    CHECK(g_mock_wire_tx_count == 1 && g_mock_wire_tx[0] == 0x3B,
          "i2c_transact read addressed the register");
    CHECK(data[0] == 0x19 && data[1] == 0x82, "i2c_transact read returned bytes");
}

/* ---- I2C: short read / timeout -------------------------------------------- */
static void test_i2c_read_short(void)
{
    mock_wire_reset();
    const uint8_t src[1] = { 0x42 };
    mock_wire_set_read_data(src, 1); /* only 1 byte available, ask for 4 */

    uint8_t data[4] = { 0 };
    int rc = hyp_i2c_transact("i2c.imu", 0x3B, data, 4, false);
    CHECK(rc == HYP_RUNTIME_IO_ERROR, "i2c_transact short read -> IO_ERROR");
}

/* ---- Dispatch: sensor/actuator route SPI/I2C to the primitives ------------ */
static void test_dispatch_routing(void)
{
    /* SPI actuator write */
    mock_spi_reset();
    uint8_t out[2] = { 0x0F, 0xF0 };
    CHECK(hyp_esp32_actuator_write("spi.hspi", out, 2) == HYP_RUNTIME_OK,
          "actuator_write spi.hspi -> OK (routed)");
    CHECK(g_mock_spi_tx[0] == 0x0F && g_mock_spi_tx[1] == 0xF0,
          "actuator_write spi.hspi clocked bytes");

    /* SPI sensor read */
    mock_spi_reset();
    const uint8_t resp[2] = { 0x77, 0x88 };
    mock_spi_set_response(resp, 2);
    uint8_t in[2] = { 0 };
    CHECK(hyp_esp32_sensor_read("spi.hspi", in, 2) == HYP_RUNTIME_OK,
          "sensor_read spi.hspi -> OK (routed)");
    CHECK(in[0] == 0x77 && in[1] == 0x88, "sensor_read spi.hspi captured bytes");

    /* I2C register-less sensor read on the named device */
    mock_wire_reset();
    const uint8_t src[1] = { 0x2A };
    mock_wire_set_read_data(src, 1);
    uint8_t b = 0;
    CHECK(hyp_esp32_sensor_read("i2c.imu", &b, 1) == HYP_RUNTIME_OK,
          "sensor_read i2c.imu -> OK (routed)");
    CHECK(b == 0x2A && g_mock_wire_addr == 0x68, "sensor_read i2c.imu read from 0x68");

    /* Unknown I2C device is reported, never a silent UNSUPPORTED_RESOURCE */
    uint8_t junk = 0;
    CHECK(hyp_esp32_sensor_read("i2c.ghost", &junk, 1) == HYP_RUNTIME_RESOURCE_NOT_CONFIGURED,
          "sensor_read unknown i2c device -> RESOURCE_NOT_CONFIGURED");
}

int main(void)
{
    printf("=== HyprAccel SDK-T6 SPI/I2C Transaction Tests ===\n\n");

    /* Must run while the SPI bus handle is still NULL. */
    test_spi_not_initialized();
    test_spi_arg_validation();

    int init_rc = hyp_esp32_hw_init();
    CHECK(init_rc == 0, "hyp_esp32_hw_init -> 0");

    test_spi_full_duplex();
    test_spi_read_only();
    test_i2c_arg_validation();
    test_i2c_write_ok();
    test_i2c_write_nack();
    test_i2c_read_ok();
    test_i2c_read_short();
    test_dispatch_routing();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
