#ifndef DIAGNOSTIC_EVENT_LOG_H
#define DIAGNOSTIC_EVENT_LOG_H

#include <Arduino.h>

enum class DiagnosticEventType : uint8_t {
    Boot = 1,
    Wake = 2,
    SleepEnter = 3,
    Bump = 10,
    Glitch = 11,
    LowBattery = 20,
    CriticalBattery = 21,
    BatteryInvalid = 22,
    UsbPowerChanged = 23,
    Hx711Missing = 30,
    DisplayMissing = 31,
    FuelGaugeMissing = 32,
    BleError = 40,
    WifiError = 41,
    BoardStatus = 50
};

struct DiagnosticEvent {
    uint32_t sequence;
    uint32_t millis;
    DiagnosticEventType type;
    float value;
    char detail[48];
};

class DiagnosticEventLog {
public:
    bool begin(size_t preferredCapacity = 256, size_t fallbackCapacity = 32);
    void record(DiagnosticEventType type, float value = 0.0f, const char* detail = nullptr, uint32_t sampleSequence = 0);
    void clear();
    void printTo(Stream& stream, size_t limit = 24) const;
    String toJson(size_t limit = 64) const;

    bool isReady() const { return events != nullptr && capacityValue > 0; }
    bool isPsramBacked() const { return psramBacked; }
    size_t capacity() const { return capacityValue; }
    size_t bytesAllocated() const { return capacityValue * sizeof(DiagnosticEvent); }
    size_t count() const { return countValue; }
    uint32_t totalRecorded() const { return totalRecordedValue; }
    uint32_t dropped() const { return droppedValue; }

    static const char* typeName(DiagnosticEventType type);

private:
    DiagnosticEvent* events = nullptr;
    size_t capacityValue = 0;
    size_t countValue = 0;
    size_t nextIndex = 0;
    bool psramBacked = false;
    uint32_t totalRecordedValue = 0;
    uint32_t droppedValue = 0;

    static String jsonEscape(const char* input);
};

#endif
