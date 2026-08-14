#include "DiagnosticEventLog.h"
#include <esp_heap_caps.h>
#include <string.h>

bool DiagnosticEventLog::begin(size_t preferredCapacity, size_t fallbackCapacity) {
    if (events != nullptr) {
        return true;
    }

    if (preferredCapacity == 0) {
        preferredCapacity = 1;
    }
    if (fallbackCapacity == 0) {
        fallbackCapacity = 1;
    }

    size_t attemptedPsramCapacity = preferredCapacity;
    while (attemptedPsramCapacity >= fallbackCapacity && ESP.getPsramSize() > 0) {
        const size_t preferredBytes = attemptedPsramCapacity * sizeof(DiagnosticEvent);
        if (ESP.getFreePsram() >= preferredBytes + 4096) {
            events = static_cast<DiagnosticEvent*>(heap_caps_malloc(preferredBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (events != nullptr) {
                capacityValue = attemptedPsramCapacity;
                psramBacked = true;
                break;
            }
        }

        if (attemptedPsramCapacity == fallbackCapacity) {
            break;
        }
        attemptedPsramCapacity = max(fallbackCapacity, attemptedPsramCapacity / 2);
    }

    if (events == nullptr) {
        size_t attemptedHeapCapacity = fallbackCapacity;
        while (attemptedHeapCapacity > 0) {
            const size_t fallbackBytes = attemptedHeapCapacity * sizeof(DiagnosticEvent);
            events = static_cast<DiagnosticEvent*>(heap_caps_malloc(fallbackBytes, MALLOC_CAP_8BIT));
            if (events != nullptr) {
                capacityValue = attemptedHeapCapacity;
                psramBacked = false;
                break;
            }
            attemptedHeapCapacity /= 2;
        }
    }

    if (events == nullptr) {
        capacityValue = 0;
        psramBacked = false;
        Serial.println("DiagnosticEventLog: allocation failed");
        return false;
    }

    memset(events, 0, capacityValue * sizeof(DiagnosticEvent));
    countValue = 0;
    nextIndex = 0;
    totalRecordedValue = 0;
    droppedValue = 0;

    Serial.printf("DiagnosticEventLog: capacity=%u allocatedBytes=%u eventBytes=%u backend=%s psramSize=%u freePsram=%u\n",
                  static_cast<unsigned>(capacityValue),
                  static_cast<unsigned>(bytesAllocated()),
                  static_cast<unsigned>(sizeof(DiagnosticEvent)),
                  psramBacked ? "psram" : "heap",
                  static_cast<unsigned>(ESP.getPsramSize()),
                  static_cast<unsigned>(ESP.getFreePsram()));
    return true;
}

void DiagnosticEventLog::record(DiagnosticEventType type, float value, const char* detail, uint32_t sampleSequence) {
    if (events == nullptr || capacityValue == 0) {
        droppedValue++;
        return;
    }

    DiagnosticEvent& event = events[nextIndex];
    event.sequence = sampleSequence;
    event.millis = millis();
    event.type = type;
    event.value = value;
    if (detail != nullptr) {
        strncpy(event.detail, detail, sizeof(event.detail) - 1);
        event.detail[sizeof(event.detail) - 1] = '\0';
    } else {
        event.detail[0] = '\0';
    }

    nextIndex = (nextIndex + 1) % capacityValue;
    if (countValue < capacityValue) {
        countValue++;
    }
    totalRecordedValue++;
}

void DiagnosticEventLog::clear() {
    if (events == nullptr || capacityValue == 0) {
        return;
    }

    memset(events, 0, capacityValue * sizeof(DiagnosticEvent));
    countValue = 0;
    nextIndex = 0;
    droppedValue = 0;
}

void DiagnosticEventLog::printTo(Stream& stream, size_t limit) const {
    stream.printf("Diagnostic events: count=%u capacity=%u backend=%s total=%lu dropped=%lu\n",
                  static_cast<unsigned>(countValue),
                  static_cast<unsigned>(capacityValue),
                  psramBacked ? "psram" : "heap",
                  static_cast<unsigned long>(totalRecordedValue),
                  static_cast<unsigned long>(droppedValue));

    if (events == nullptr || capacityValue == 0 || countValue == 0) {
        return;
    }

    const size_t shown = min(limit, countValue);
    for (size_t i = 0; i < shown; ++i) {
        const size_t newestOffset = countValue - 1 - i;
        const size_t index = (nextIndex + capacityValue - 1 - i) % capacityValue;
        const DiagnosticEvent& event = events[index];
        stream.printf("  #%u ms=%lu seq=%lu type=%s value=%.3f detail=%s\n",
                      static_cast<unsigned>(newestOffset),
                      static_cast<unsigned long>(event.millis),
                      static_cast<unsigned long>(event.sequence),
                      typeName(event.type),
                      event.value,
                      event.detail);
    }
}

String DiagnosticEventLog::toJson(size_t limit) const {
    String json = "{";
    json += "\"ready\":" + String(isReady() ? "true" : "false");
    json += ",\"backend\":\"" + String(psramBacked ? "psram" : (events != nullptr ? "heap" : "none")) + "\"";
    json += ",\"psram_backed\":" + String(psramBacked ? "true" : "false");
    json += ",\"capacity\":" + String(capacityValue);
    json += ",\"allocated_bytes\":" + String(bytesAllocated());
    json += ",\"psram_size\":" + String(ESP.getPsramSize());
    json += ",\"free_psram\":" + String(ESP.getFreePsram());
    json += ",\"count\":" + String(countValue);
    json += ",\"total_recorded\":" + String(totalRecordedValue);
    json += ",\"dropped\":" + String(droppedValue);
    json += ",\"events\":[";

    if (events != nullptr && capacityValue > 0 && countValue > 0) {
        const size_t shown = min(limit, countValue);
        for (size_t i = 0; i < shown; ++i) {
            if (i > 0) {
                json += ",";
            }
            const size_t index = (nextIndex + capacityValue - 1 - i) % capacityValue;
            const DiagnosticEvent& event = events[index];
            json += "{";
            json += "\"ms\":" + String(event.millis);
            json += ",\"sequence\":" + String(event.sequence);
            json += ",\"type\":\"" + String(typeName(event.type)) + "\"";
            json += ",\"type_code\":" + String(static_cast<uint8_t>(event.type));
            json += ",\"value\":" + String(event.value, 3);
            json += ",\"detail\":\"" + jsonEscape(event.detail) + "\"";
            json += "}";
        }
    }

    json += "]}";
    return json;
}

const char* DiagnosticEventLog::typeName(DiagnosticEventType type) {
    switch (type) {
        case DiagnosticEventType::Boot: return "boot";
        case DiagnosticEventType::Wake: return "wake";
        case DiagnosticEventType::SleepEnter: return "sleep_enter";
        case DiagnosticEventType::Bump: return "bump";
        case DiagnosticEventType::Glitch: return "glitch";
        case DiagnosticEventType::LowBattery: return "low_battery";
        case DiagnosticEventType::CriticalBattery: return "critical_battery";
        case DiagnosticEventType::BatteryInvalid: return "battery_invalid";
        case DiagnosticEventType::UsbPowerChanged: return "usb_power_changed";
        case DiagnosticEventType::Hx711Missing: return "hx711_missing";
        case DiagnosticEventType::DisplayMissing: return "display_missing";
        case DiagnosticEventType::FuelGaugeMissing: return "fuel_gauge_missing";
        case DiagnosticEventType::BleError: return "ble_error";
        case DiagnosticEventType::WifiError: return "wifi_error";
        case DiagnosticEventType::BoardStatus: return "board_status";
        default: return "unknown";
    }
}

String DiagnosticEventLog::jsonEscape(const char* input) {
    String escaped;
    if (input == nullptr) {
        return escaped;
    }

    for (const char* p = input; *p != '\0'; ++p) {
        switch (*p) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += *p; break;
        }
    }
    return escaped;
}
