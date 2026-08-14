#ifndef BATTERY_DRAIN_SESSION_H
#define BATTERY_DRAIN_SESSION_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

class BatteryDrainSession {
public:
    void begin(uint32_t nowMs,
               float voltage,
               int percent,
               int rawPercent,
               bool valid,
               const char* label = "boot") {
        setLabel(label);
        startMillis = nowMs;
        lastMillis = nowMs;
        startVoltage = valid ? voltage : NAN;
        lastVoltage = valid ? voltage : NAN;
        startPercent = clampPercent(percent);
        lastPercent = clampPercent(percent);
        startRawPercent = clampPercent(rawPercent);
        lastRawPercent = clampPercent(rawPercent);
        validStart = valid;
        validLast = valid;
        samples = valid ? 1 : 0;
        invalidSamples = valid ? 0 : 1;
    }

    void reset(uint32_t nowMs,
               float voltage,
               int percent,
               int rawPercent,
               bool valid,
               const char* label = "manual") {
        begin(nowMs, voltage, percent, rawPercent, valid, label);
    }

    void update(uint32_t nowMs,
                float voltage,
                int percent,
                int rawPercent,
                bool valid) {
        lastMillis = nowMs;
        if (!valid || !isfinite(voltage)) {
            validLast = false;
            invalidSamples++;
            return;
        }

        if (!validStart) {
            startMillis = nowMs;
            startVoltage = voltage;
            startPercent = clampPercent(percent);
            startRawPercent = clampPercent(rawPercent);
            validStart = true;
        }

        lastVoltage = voltage;
        lastPercent = clampPercent(percent);
        lastRawPercent = clampPercent(rawPercent);
        validLast = true;
        samples++;
    }

    const char* getLabel() const { return label; }
    uint32_t getStartMillis() const { return startMillis; }
    uint32_t getLastMillis() const { return lastMillis; }
    uint32_t getSamples() const { return samples; }
    uint32_t getInvalidSamples() const { return invalidSamples; }
    bool hasValidStart() const { return validStart; }
    bool hasValidLast() const { return validLast; }

    float getStartVoltage() const { return validStart ? startVoltage : NAN; }
    float getLastVoltage() const { return validLast ? lastVoltage : NAN; }
    int getStartPercent() const { return startPercent; }
    int getLastPercent() const { return lastPercent; }
    int getStartRawPercent() const { return startRawPercent; }
    int getLastRawPercent() const { return lastRawPercent; }

    float getElapsedMinutes() const {
        return static_cast<float>(lastMillis - startMillis) / 60000.0f;
    }

    float getDeltaVoltage() const {
        if (!validStart || !validLast) {
            return NAN;
        }
        return lastVoltage - startVoltage;
    }

    int getDeltaPercent() const {
        return lastPercent - startPercent;
    }

    int getDeltaRawPercent() const {
        return lastRawPercent - startRawPercent;
    }

    float getVoltageMillivoltsPerHour() const {
        if (!validStart || !validLast) {
            return NAN;
        }
        const float hours = elapsedHours();
        if (hours <= 0.0f) {
            return 0.0f;
        }
        return ((lastVoltage - startVoltage) * 1000.0f) / hours;
    }

    float getRawPercentPerHour() const {
        const float hours = elapsedHours();
        if (hours <= 0.0f) {
            return 0.0f;
        }
        return static_cast<float>(lastRawPercent - startRawPercent) / hours;
    }

    const char* getTrend() const {
        if (!validStart || !validLast || getElapsedMinutes() < 2.0f) {
            return "learning";
        }

        const float mvPerHour = getVoltageMillivoltsPerHour();
        if (!isfinite(mvPerHour)) {
            return "unknown";
        }
        if (mvPerHour >= 12.0f || getRawPercentPerHour() >= 2.0f) {
            return "charging";
        }
        if (mvPerHour <= -12.0f || getRawPercentPerHour() <= -2.0f) {
            return "draining";
        }
        return "flat";
    }

    const char* getConfidence() const {
        const float minutes = getElapsedMinutes();
        const float absDeltaMv = fabsf(getDeltaVoltage() * 1000.0f);
        const int absDeltaRawPercent = absInt(getDeltaRawPercent());

        if (!validStart || !validLast || samples < 2) {
            return "none";
        }
        if (minutes >= 90.0f && (absDeltaMv >= 25.0f || absDeltaRawPercent >= 5)) {
            return "high";
        }
        if (minutes >= 30.0f && (absDeltaMv >= 10.0f || absDeltaRawPercent >= 2)) {
            return "medium";
        }
        if (minutes >= 5.0f) {
            return "low";
        }
        return "learning";
    }

private:
    char label[32] = "boot";
    uint32_t startMillis = 0;
    uint32_t lastMillis = 0;
    float startVoltage = NAN;
    float lastVoltage = NAN;
    int startPercent = 0;
    int lastPercent = 0;
    int startRawPercent = 0;
    int lastRawPercent = 0;
    bool validStart = false;
    bool validLast = false;
    uint32_t samples = 0;
    uint32_t invalidSamples = 0;

    void setLabel(const char* newLabel) {
        if (newLabel == nullptr || newLabel[0] == '\0') {
            newLabel = "unnamed";
        }
        snprintf(label, sizeof(label), "%s", newLabel);
    }

    static int clampPercent(int value) {
        if (value < 0) {
            return 0;
        }
        if (value > 100) {
            return 100;
        }
        return value;
    }

    static int absInt(int value) {
        return value < 0 ? -value : value;
    }

    float elapsedHours() const {
        return static_cast<float>(lastMillis - startMillis) / 3600000.0f;
    }
};

#endif
