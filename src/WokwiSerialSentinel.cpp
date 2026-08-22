#ifdef WMBP_WOKWI_SERIAL_SENTINEL

#include <Arduino.h>

namespace {
constexpr uint32_t SAMPLE_INTERVAL_MS = 10;
uint32_t sequence = 0;
uint32_t nextSampleMs = 0;
}

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("WMBP_SERIAL_SENTINEL_BOOT");
    Serial.println("WMBP_WEIGHT_V1_HEADER,ms,seq,weight_g,flow_gps,status,quality,battery_pct,hx711_hz,dropped");
    nextSampleMs = millis();
}

void loop() {
    const uint32_t nowMs = millis();
    if (static_cast<int32_t>(nowMs - nextSampleMs) < 0) {
        return;
    }

    nextSampleMs += SAMPLE_INTERVAL_MS;
    sequence++;
    Serial.printf("WMBP_WEIGHT_V1,%lu,%lu,0.000,0.000,0x0001,100,100,100.00,0\n",
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(sequence));
}

#endif
