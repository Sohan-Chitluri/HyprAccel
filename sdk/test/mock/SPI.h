/*
 * mock/SPI.h — minimal host-side stub of Arduino SPI.h for SDK-T6 tests.
 *
 * SPIClass::transfer() records each transmitted byte in g_mock_spi_tx[] and
 * returns the next byte from a test-supplied response buffer, so a full-duplex
 * exchange can be verified on the host.
 */
#ifndef MOCK_SPI_H
#define MOCK_SPI_H

#include <stdint.h>
#include <stddef.h>

#define HSPI 2
#define VSPI 3
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPISettings {
public:
    uint32_t freq; uint8_t order; uint8_t mode;
    SPISettings(uint32_t f, uint8_t o, uint8_t m) : freq(f), order(o), mode(m) {}
    SPISettings() : freq(1000000), order(1), mode(0) {}
};

/* --- Mock capture / control state (shared across translation units) --- */
inline uint8_t g_mock_spi_tx[256];        /* bytes clocked out via transfer()   */
inline uint8_t g_mock_spi_rx_resp[256];   /* bytes transfer() returns (MISO)     */
inline int     g_mock_spi_tx_count   = 0;
inline int     g_mock_spi_resp_idx   = 0;
inline int     g_mock_spi_begin_count = 0;
inline int     g_mock_spi_end_count   = 0;

inline void mock_spi_reset(void) {
    g_mock_spi_tx_count = 0;
    g_mock_spi_resp_idx = 0;
    g_mock_spi_begin_count = 0;
    g_mock_spi_end_count = 0;
    for (int i = 0; i < 256; i++) { g_mock_spi_tx[i] = 0; g_mock_spi_rx_resp[i] = 0; }
}

inline void mock_spi_set_response(const uint8_t *resp, int n) {
    for (int i = 0; i < n && i < 256; i++) g_mock_spi_rx_resp[i] = resp[i];
}

class SPIClass {
public:
    int bus_id;
    SPIClass(int id = VSPI) : bus_id(id) {}
    void begin(int = -1, int = -1, int = -1, int = -1) {}
    void beginTransaction(SPISettings) { g_mock_spi_begin_count++; }
    void endTransaction() { g_mock_spi_end_count++; }
    uint8_t transfer(uint8_t b) {
        if (g_mock_spi_tx_count < 256) g_mock_spi_tx[g_mock_spi_tx_count++] = b;
        uint8_t r = g_mock_spi_rx_resp[g_mock_spi_resp_idx];
        if (g_mock_spi_resp_idx < 255) g_mock_spi_resp_idx++;
        return r;
    }
};

#endif /* MOCK_SPI_H */
