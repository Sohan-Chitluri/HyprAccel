/*
 * mock/Wire.h — minimal host-side stub of Arduino Wire.h for SDK-T6 tests.
 *
 * Records the addressed slave and every written byte; endTransmission() returns
 * a test-controlled code so I2C NACK/timeout paths can be exercised. read()
 * yields bytes preloaded via mock_wire_set_read_data(); requestFrom() reports
 * how many of those are available so short-read/timeout handling is testable.
 */
#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include <stdint.h>
#include <stddef.h>

/* --- Mock capture / control state (shared across translation units) --- */
inline uint8_t g_mock_wire_tx[256];       /* bytes written after beginTransmission */
inline int     g_mock_wire_tx_count = 0;
inline uint8_t g_mock_wire_addr     = 0;  /* last addressed 7-bit slave            */
inline uint8_t g_mock_wire_end_rc   = 0;  /* value endTransmission() returns       */
inline uint8_t g_mock_wire_read_buf[256]; /* bytes read() will hand back           */
inline int     g_mock_wire_read_len = 0;  /* bytes made available by the test      */
inline int     g_mock_wire_read_idx = 0;
inline int     g_mock_wire_requested = 0; /* len passed to last requestFrom()      */

inline bool g_mock_wire_began = false; /* set by TwoWire::begin(); lets tests tell
                                          * "I2C0 configured in board macros" apart
                                          * from "I2C0 actually initialized". */

inline void mock_wire_reset(void) {
    g_mock_wire_tx_count = 0;
    g_mock_wire_addr = 0;
    g_mock_wire_end_rc = 0;
    g_mock_wire_read_len = 0;
    g_mock_wire_read_idx = 0;
    g_mock_wire_requested = 0;
    g_mock_wire_began = false;
}

/* Simulate a bus error: 2 = NACK on address, 3 = NACK on data, 5 = timeout. */
inline void mock_wire_set_nack(uint8_t rc) { g_mock_wire_end_rc = rc; }

inline void mock_wire_set_read_data(const uint8_t *d, int n) {
    for (int i = 0; i < n && i < 256; i++) g_mock_wire_read_buf[i] = d[i];
    g_mock_wire_read_len = n;
    g_mock_wire_read_idx = 0;
}

class TwoWire {
public:
    void begin(int = -1, int = -1, uint32_t = 0) { g_mock_wire_began = true; }
    void beginTransmission(uint8_t addr) { g_mock_wire_addr = addr; g_mock_wire_tx_count = 0; }
    size_t write(uint8_t b) {
        if (g_mock_wire_tx_count < 256) g_mock_wire_tx[g_mock_wire_tx_count++] = b;
        return 1;
    }
    size_t write(const uint8_t *d, size_t n) {
        for (size_t i = 0; i < n; i++) write(d[i]);
        return n;
    }
    uint8_t endTransmission() { return g_mock_wire_end_rc; }
    uint8_t endTransmission(bool) { return g_mock_wire_end_rc; }
    size_t requestFrom(int addr, int n) {
        g_mock_wire_addr = (uint8_t)addr;
        g_mock_wire_requested = n;
        g_mock_wire_read_idx = 0;
        /* Only as many bytes as the test preloaded are actually available. */
        return (size_t)(g_mock_wire_read_len < n ? g_mock_wire_read_len : n);
    }
    int read() {
        if (g_mock_wire_read_idx < g_mock_wire_read_len)
            return g_mock_wire_read_buf[g_mock_wire_read_idx++];
        return -1;
    }
    int available() { return g_mock_wire_read_len - g_mock_wire_read_idx; }
};

inline TwoWire Wire;

#endif /* MOCK_WIRE_H */
