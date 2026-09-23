/*
 * mock/Arduino.h — minimal host-side stub of the Arduino core API.
 *
 * SDK-T6: lets sdk/src/hyp_esp32_hw.cpp compile and run on the host so the
 * SPI/I2C transaction primitives can be unit-tested without real hardware.
 * Only the symbols actually referenced by hyp_esp32_hw.cpp are provided.
 *
 * ESP_ARDUINO_VERSION_MAJOR is fixed to 3 so the modern ledc* PWM path is the
 * one compiled (the pre-3 channel path is #if'd out).
 *
 * All globals/functions are `inline` so the header can be included from multiple
 * translation units without violating the One Definition Rule (requires C++17).
 */
#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string>

#define ESP_ARDUINO_VERSION_MAJOR 3

#define HIGH 1
#define LOW  0
#define INPUT           0
#define OUTPUT          1
#define INPUT_PULLUP    2
#define INPUT_PULLDOWN  3
#define ANALOG          4
#define MSBFIRST        1
#define SERIAL_8N1      0x800001c

typedef enum {
    ADC_0db   = 0,
    ADC_2_5db = 1,
    ADC_6db   = 2,
    ADC_11db  = 3
} adc_attenuation_t;

/* HardwareSerial: swallow the actual bytes but record enough to test on:
 * whether begin() was ever called (so tests can tell UART1/UART2 were
 * actually initialized rather than just "configured" in the board macros),
 * and a rolling text log of everything print()/println()'d so tests can
 * assert on the [INFO]/[WARN]/[ERROR] lines hyp_esp32_hw.cpp emits. */
class HardwareSerial {
public:
    bool began = false;
    long began_baud = 0;
    std::string log;

    void begin(long baud) { began = true; began_baud = baud; }
    void begin(long baud, int, int, int) { began = true; began_baud = baud; }
    void print(const char *s) { log += s; }
    void print(int v) { log += std::to_string(v); }
    void print(unsigned int v) { log += std::to_string(v); }
    void print(double v) { log += std::to_string(v); }
    void println(const char *s) { log += s; log += "\n"; }
    void println(int v) { log += std::to_string(v); log += "\n"; }
    void println(unsigned int v) { log += std::to_string(v); log += "\n"; }
    void println(double v) { log += std::to_string(v); log += "\n"; }
    int available() { return 0; }
    size_t write(const uint8_t *, size_t n) { return n; }

    bool log_contains(const char *needle) const { return log.find(needle) != std::string::npos; }
    void reset() { began = false; began_baud = 0; log.clear(); }
};

inline HardwareSerial Serial;
inline HardwareSerial Serial1;
inline HardwareSerial Serial2;

/* Digital/analog IO — record the last digitalWrite so tests can assert CS use. */
inline int g_mock_last_cs_pin = -1;
inline int g_mock_last_cs_val = -1;

/* pinMode()/ledcAttach() call tracking, keyed by GPIO number (0..63), so
 * tests can assert PINFUNC gating: a pin whose function doesn't match must
 * never reach pinMode()/ledcAttach() during hyp_esp32_hw_init(). */
inline bool g_mock_pinmode_called[64] = { false };
inline bool g_mock_ledc_attach_called[64] = { false };

inline void mock_pin_tracking_reset(void) {
    for (int i = 0; i < 64; i++) { g_mock_pinmode_called[i] = false; g_mock_ledc_attach_called[i] = false; }
}

inline void pinMode(int pin, int) { if (pin >= 0 && pin < 64) g_mock_pinmode_called[pin] = true; }
inline void digitalWrite(int pin, int val) { g_mock_last_cs_pin = pin; g_mock_last_cs_val = val; }
inline int  digitalRead(int) { return 0; }
inline int  analogRead(int) { return 0; }
inline void analogSetPinAttenuation(int, adc_attenuation_t) {}
inline void ledcAttach(int pin, long, int) { if (pin >= 0 && pin < 64) g_mock_ledc_attach_called[pin] = true; }
inline void ledcWrite(int, int) {}
inline unsigned long micros() { return 0UL; }

/* setCpuFrequencyMhz() — real ESP32 Arduino core signature returns bool
 * (false when the requested frequency is not valid for the chip's XTAL).
 * g_mock_cpu_freq_result lets a test force the failure path. */
inline uint32_t g_mock_cpu_freq_last_mhz = 0;
inline int      g_mock_cpu_freq_call_count = 0;
inline bool     g_mock_cpu_freq_result = true;

inline bool setCpuFrequencyMhz(uint32_t mhz) {
    g_mock_cpu_freq_last_mhz = mhz;
    g_mock_cpu_freq_call_count++;
    return g_mock_cpu_freq_result;
}

inline void mock_cpu_freq_reset(void) {
    g_mock_cpu_freq_last_mhz = 0;
    g_mock_cpu_freq_call_count = 0;
    g_mock_cpu_freq_result = true;
}

#endif /* MOCK_ARDUINO_H */
