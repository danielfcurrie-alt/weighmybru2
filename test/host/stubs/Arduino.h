#ifndef HOST_TEST_ARDUINO_H
#define HOST_TEST_ARDUINO_H

#include <stdint.h>

#define LOW 0
#define HIGH 1
#define INPUT 0x01

namespace ArduinoFake {
inline uint32_t nowMs = 0;
inline int pinValues[256] = {};
inline int pinModes[256] = {};

inline void reset() {
    nowMs = 0;
    for (int index = 0; index < 256; ++index) {
        pinValues[index] = LOW;
        pinModes[index] = 0;
    }
}
}

inline uint32_t millis() {
    return ArduinoFake::nowMs;
}

inline void delay(uint32_t milliseconds) {
    ArduinoFake::nowMs += milliseconds;
}

inline void pinMode(int pin, int mode) {
    if (pin >= 0 && pin < 256) {
        ArduinoFake::pinModes[pin] = mode;
    }
}

inline int digitalRead(int pin) {
    return pin >= 0 && pin < 256 ? ArduinoFake::pinValues[pin] : LOW;
}

#endif
