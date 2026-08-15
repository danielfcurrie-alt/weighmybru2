#ifndef SCALE_COMMAND_QUEUE_H
#define SCALE_COMMAND_QUEUE_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

class Scale;

class ScaleCommandQueue {
public:
    bool requestSetCalibrationFactor(float factor, const char* source);
    bool requestFilterSettings(bool updateBrewingThreshold,
                               float brewingThreshold,
                               bool updateStabilityTimeout,
                               unsigned long stabilityTimeout,
                               bool updateMedianSamples,
                               int medianSamples,
                               bool updateAverageSamples,
                               int averageSamples,
                               const char* source);
    void process(Scale& scale);

    bool hasPendingCalibrationFactor() const;
    float getPendingCalibrationFactor() const;

private:
    mutable portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    bool calibrationFactorPending = false;
    float pendingCalibrationFactor = 0.0f;
    char pendingCalibrationSource[24] = {};

    bool filterSettingsPending = false;
    bool pendingBrewingThresholdSet = false;
    float pendingBrewingThreshold = 0.0f;
    bool pendingStabilityTimeoutSet = false;
    unsigned long pendingStabilityTimeout = 0;
    bool pendingMedianSamplesSet = false;
    int pendingMedianSamples = 0;
    bool pendingAverageSamplesSet = false;
    int pendingAverageSamples = 0;
    char pendingFilterSource[24] = {};
};

#endif
