#include "Hx711Io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace {
portMUX_TYPE hx711IoMux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint8_t HX711_BITS = 24;
constexpr uint8_t HX711_POWER_DOWN_US = 80;
constexpr int32_t HX711_POSITIVE_RAIL = 0x7FFFFF;
constexpr int32_t HX711_NEGATIVE_RAIL = -0x800000;
}

#ifndef WMBP_HX711_PULSE_SETTLE_US
#define WMBP_HX711_PULSE_SETTLE_US 1
#endif

static_assert(WMBP_HX711_PULSE_SETTLE_US > 0, "HX711 pulse settle must be positive");

void Hx711Io::begin(uint8_t dataPinArg, uint8_t clockPinArg, Gain gainArg) {
    dataPin = dataPinArg;
    clockPin = clockPinArg;
    gain = gainArg;
    pinMode(dataPin, INPUT);
    pinMode(clockPin, OUTPUT);
    digitalWrite(clockPin, LOW);
    begun = true;
}

bool Hx711Io::isReady() const {
    return begun && digitalRead(dataPin) == LOW;
}

Hx711Io::Status Hx711Io::readRaw(int32_t& rawValue, uint32_t* readDurationMicros) {
    rawValue = 0;
    if (readDurationMicros != nullptr) {
        *readDurationMicros = 0;
    }
    if (!begun) {
        return Status::Disconnected;
    }
    if (digitalRead(dataPin) != LOW) {
        return Status::NotReady;
    }

    uint32_t value = 0;
    const uint32_t startMicros = micros();

    portENTER_CRITICAL(&hx711IoMux);
    for (uint8_t i = 0; i < HX711_BITS; i++) {
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(WMBP_HX711_PULSE_SETTLE_US);
        value = (value << 1) | (digitalRead(dataPin) ? 1UL : 0UL);
        digitalWrite(clockPin, LOW);
        delayMicroseconds(WMBP_HX711_PULSE_SETTLE_US);
    }

    const uint8_t pulses = gainPulses();
    for (uint8_t i = 0; i < pulses; i++) {
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(WMBP_HX711_PULSE_SETTLE_US);
        digitalWrite(clockPin, LOW);
        delayMicroseconds(WMBP_HX711_PULSE_SETTLE_US);
    }
    portEXIT_CRITICAL(&hx711IoMux);

    const uint32_t endMicros = micros();
    if (readDurationMicros != nullptr) {
        *readDurationMicros = endMicros - startMicros;
    }

    if (value & 0x800000UL) {
        value |= 0xFF000000UL;
    }
    rawValue = static_cast<int32_t>(value);

    if (rawValue == HX711_POSITIVE_RAIL || rawValue == HX711_NEGATIVE_RAIL) {
        return Status::Railed;
    }
    return Status::Ok;
}

Hx711Io::Status Hx711Io::readRawWithTimeout(uint32_t timeoutMicros, int32_t& rawValue, uint32_t* waitDurationMicros, uint32_t* readDurationMicros) {
    rawValue = 0;
    if (waitDurationMicros != nullptr) {
        *waitDurationMicros = 0;
    }
    const uint32_t startMicros = micros();
    while (!isReady()) {
        const uint32_t elapsed = micros() - startMicros;
        if (elapsed >= timeoutMicros) {
            if (waitDurationMicros != nullptr) {
                *waitDurationMicros = elapsed;
            }
            return begun ? Status::Timeout : Status::Disconnected;
        }
        delayMicroseconds(100);
    }
    if (waitDurationMicros != nullptr) {
        *waitDurationMicros = micros() - startMicros;
    }
    return readRaw(rawValue, readDurationMicros);
}

void Hx711Io::powerDown() {
    if (!begun) {
        return;
    }
    pinMode(clockPin, OUTPUT);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(HX711_POWER_DOWN_US);
}

void Hx711Io::powerUp() {
    if (!begun) {
        return;
    }
    digitalWrite(clockPin, LOW);
}

void Hx711Io::setGain(Gain gainArg) {
    gain = gainArg;
}

const char* Hx711Io::statusName(Status status) const {
    switch (status) {
        case Status::Ok: return "Ok";
        case Status::NotReady: return "NotReady";
        case Status::Timeout: return "Timeout";
        case Status::Disconnected: return "Disconnected";
        case Status::ReadError: return "ReadError";
        case Status::Railed: return "Railed";
    }
    return "Unknown";
}
