#ifndef HX711_IO_H
#define HX711_IO_H

#include <Arduino.h>
#include <stdint.h>

class Hx711Io {
public:
    enum class Status : uint8_t {
        Ok,
        NotReady,
        Timeout,
        Disconnected,
        ReadError,
        Railed
    };

    enum class Gain : uint8_t {
        ChannelA128 = 1,
        ChannelB32 = 2,
        ChannelA64 = 3
    };

    void begin(uint8_t dataPin, uint8_t clockPin, Gain gain = Gain::ChannelA128);
    bool isReady() const;
    Status readRaw(int32_t& rawValue, uint32_t* readDurationMicros = nullptr);
    Status readRawWithTimeout(uint32_t timeoutMicros, int32_t& rawValue, uint32_t* waitDurationMicros = nullptr, uint32_t* readDurationMicros = nullptr);
    void powerDown();
    void powerUp();
    void setGain(Gain gain);
    Gain getGain() const { return gain; }
    const char* statusName(Status status) const;

private:
    uint8_t dataPin = 0;
    uint8_t clockPin = 0;
    Gain gain = Gain::ChannelA128;
    bool begun = false;

    uint8_t gainPulses() const { return static_cast<uint8_t>(gain); }
};

#endif
