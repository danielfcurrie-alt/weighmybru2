#include "ScaleCommandQueue.h"
#include "Scale.h"
#include <math.h>
#include <string.h>

bool ScaleCommandQueue::requestSetCalibrationFactor(float factor, const char* source) {
    if (!isfinite(factor) || factor < 10.0f || factor > 100000.0f) {
        return false;
    }

    portENTER_CRITICAL(&mux);
    calibrationFactorPending = true;
    pendingCalibrationFactor = factor;
    const char* safeSource = source != nullptr ? source : "unknown";
    strlcpy(pendingCalibrationSource, safeSource, sizeof(pendingCalibrationSource));
    portEXIT_CRITICAL(&mux);
    return true;
}

bool ScaleCommandQueue::requestFilterSettings(bool updateBrewingThreshold,
                                              float brewingThreshold,
                                              bool updateStabilityTimeout,
                                              unsigned long stabilityTimeout,
                                              bool updateMedianSamples,
                                              int medianSamples,
                                              bool updateAverageSamples,
                                              int averageSamples,
                                              const char* source) {
    if (!updateBrewingThreshold && !updateStabilityTimeout &&
        !updateMedianSamples && !updateAverageSamples) {
        return false;
    }

    if (updateBrewingThreshold &&
        (!isfinite(brewingThreshold) || brewingThreshold < 0.05f || brewingThreshold > 1.0f)) {
        return false;
    }
    if (updateStabilityTimeout && (stabilityTimeout < 500UL || stabilityTimeout > 10000UL)) {
        return false;
    }
    if (updateMedianSamples && (medianSamples < 1 || medianSamples > 10)) {
        return false;
    }
    if (updateAverageSamples && (averageSamples < 1 || averageSamples > 10)) {
        return false;
    }

    portENTER_CRITICAL(&mux);
    filterSettingsPending = true;
    pendingBrewingThresholdSet = updateBrewingThreshold;
    pendingBrewingThreshold = brewingThreshold;
    pendingStabilityTimeoutSet = updateStabilityTimeout;
    pendingStabilityTimeout = stabilityTimeout;
    pendingMedianSamplesSet = updateMedianSamples;
    pendingMedianSamples = medianSamples;
    pendingAverageSamplesSet = updateAverageSamples;
    pendingAverageSamples = averageSamples;
    const char* safeSource = source != nullptr ? source : "unknown";
    strlcpy(pendingFilterSource, safeSource, sizeof(pendingFilterSource));
    portEXIT_CRITICAL(&mux);
    return true;
}

void ScaleCommandQueue::process(Scale& scale) {
    bool shouldApplyCalibration = false;
    float factor = 0.0f;
    char source[sizeof(pendingCalibrationSource)] = {};
    bool shouldApplyFilterSettings = false;
    bool applyBrewingThreshold = false;
    float brewingThreshold = 0.0f;
    bool applyStabilityTimeout = false;
    unsigned long stabilityTimeout = 0;
    bool applyMedianSamples = false;
    int medianSamples = 0;
    bool applyAverageSamples = false;
    int averageSamples = 0;
    char filterSource[sizeof(pendingFilterSource)] = {};

    portENTER_CRITICAL(&mux);
    if (calibrationFactorPending) {
        shouldApplyCalibration = true;
        factor = pendingCalibrationFactor;
        strlcpy(source, pendingCalibrationSource, sizeof(source));
        calibrationFactorPending = false;
        pendingCalibrationFactor = 0.0f;
        pendingCalibrationSource[0] = '\0';
    }
    if (filterSettingsPending) {
        shouldApplyFilterSettings = true;
        applyBrewingThreshold = pendingBrewingThresholdSet;
        brewingThreshold = pendingBrewingThreshold;
        applyStabilityTimeout = pendingStabilityTimeoutSet;
        stabilityTimeout = pendingStabilityTimeout;
        applyMedianSamples = pendingMedianSamplesSet;
        medianSamples = pendingMedianSamples;
        applyAverageSamples = pendingAverageSamplesSet;
        averageSamples = pendingAverageSamples;
        strlcpy(filterSource, pendingFilterSource, sizeof(filterSource));

        filterSettingsPending = false;
        pendingBrewingThresholdSet = false;
        pendingStabilityTimeoutSet = false;
        pendingMedianSamplesSet = false;
        pendingAverageSamplesSet = false;
        pendingFilterSource[0] = '\0';
    }
    portEXIT_CRITICAL(&mux);

    if (shouldApplyCalibration) {
        Serial.printf("Scale command queue: applying calibration factor %.6f from %s\n",
                      factor,
                      source[0] != '\0' ? source : "unknown");
        scale.set_scale(factor);
    }

    if (shouldApplyFilterSettings) {
        Serial.printf("Scale command queue: applying filter settings from %s\n",
                      filterSource[0] != '\0' ? filterSource : "unknown");
        if (applyBrewingThreshold) {
            scale.setBrewingThreshold(brewingThreshold);
        }
        if (applyStabilityTimeout) {
            scale.setStabilityTimeout(stabilityTimeout);
        }
        if (applyMedianSamples) {
            scale.setMedianSamples(medianSamples);
        }
        if (applyAverageSamples) {
            scale.setAverageSamples(averageSamples);
        }
    }
}

bool ScaleCommandQueue::hasPendingCalibrationFactor() const {
    portENTER_CRITICAL(&mux);
    const bool pending = calibrationFactorPending;
    portEXIT_CRITICAL(&mux);
    return pending;
}

float ScaleCommandQueue::getPendingCalibrationFactor() const {
    portENTER_CRITICAL(&mux);
    const float factor = pendingCalibrationFactor;
    portEXIT_CRITICAL(&mux);
    return factor;
}
