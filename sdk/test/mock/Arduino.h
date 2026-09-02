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

/* HardwareSerial: swallow all output; report no bytes available. */
class HardwareSerial {
public:
    void begin(long) {}
    void begin(long, int, int, int) {}
    void print(const char *) {}
    void print(int) {}
    void print(unsigned int) {}
    void println(const char *) {}
    void println(int) {}
    void println(unsigned int) {}
    int available() { return 0; }
    size_t write(const uint8_t *, size_t n) { return n; }
};

inline HardwareSerial Serial;
inline HardwareSerial Serial1;
inline HardwareSerial Serial2;

/* Digital/analog IO — record the last digitalWrite so tests can assert CS use. */
inline int g_mock_last_cs_pin = -1;
inline int g_mock_last_cs_val = -1;

inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int val) { g_mock_last_cs_pin = pin; g_mock_last_cs_val = val; }
inline int  digitalRead(int) { return 0; }
inline int  analogRead(int) { return 0; }
inline void analogSetPinAttenuation(int, adc_attenuation_t) {}
inline void ledcAttach(int, long, int) {}
inline void ledcWrite(int, int) {}
inline unsigned long micros() { return 0UL; }

#endif /* MOCK_ARDUINO_H */
