#include "Scale.h"
#include "WebServer.h"
#include "Calibration.h"
#include "FlowRate.h"
#include "DiagnosticEventLog.h"
#include <math.h>

namespace {
constexpr float ZERO_CLAMP_ENTER_GRAMS = 0.08f;
constexpr float ZERO_CLAMP_EXIT_GRAMS = 0.18f;
constexpr unsigned long ZERO_CLAMP_ENTER_MS = 300;

constexpr float AUTO_ZERO_RANGE_GRAMS = 0.45f;
constexpr float AUTO_ZERO_MAX_CORRECTION_GRAMS = 1.0f;
constexpr float AUTO_ZERO_MAX_STEP_GRAMS = 0.02f;
constexpr float AUTO_ZERO_STEP_FRACTION = 0.05f;
constexpr float AUTO_ZERO_RAW_STEP_GRAMS = 0.25f;
constexpr float AUTO_ZERO_WINDOW_P2P_GRAMS = 0.18f;
constexpr unsigned long AUTO_ZERO_STABLE_MS = 7000;
constexpr unsigned long AUTO_ZERO_ADJUST_INTERVAL_MS = 1000;
constexpr unsigned long AUTO_ZERO_ACTIVE_WINDOW_MS = 2000;
constexpr unsigned long ZERO_SUPPRESS_AFTER_TARE_MS = 2000;

constexpr float PLAUSIBILITY_JUMP_GRAMS = 5.0f;
constexpr float PLAUSIBILITY_CONFIRM_TOLERANCE_GRAMS = 2.0f;
constexpr float PLAUSIBILITY_CONFIRM_TOLERANCE_FRACTION = 0.25f;
constexpr uint8_t PLAUSIBILITY_CONFIRM_SAMPLE_COUNT = 2;
constexpr unsigned long PLAUSIBILITY_CANDIDATE_TIMEOUT_MS = 250;
constexpr unsigned long PLAUSIBILITY_SUPPRESS_AFTER_TARE_MS = 500;

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}
}

#if WMBP_SIMULATION_MODE
float Scale::simulatedRawWeight(unsigned long sampleMillis) const {
    return SimulationProfiles::weightForScenario(sampleMillis - simulationStartMillis,
                                                 WMBP_SIM_SCENARIO,
                                                 WMBP_SIM_HX711_HZ);
}
#endif

Scale::Scale(uint8_t dataPin, uint8_t clockPin, float calibrationFactor)
    : dataPin(dataPin), clockPin(clockPin), calibrationFactor(calibrationFactor), currentWeight(0.0f),
      readingIndex(0), samplesInitialized(false), previousFilteredWeight(0), medianSamples(3), averageSamples(2),
      currentFilterState(STABLE), lastBrewingActivity(0), lastStableWeight(0.0f) {
    // Initialize readings array
    for (int i = 0; i < MAX_SAMPLES; i++) {
        readings[i] = 0.0f;
    }
}

bool Scale::begin() {
    Serial.println("Starting scale initialization...");
    
    preferences.begin("scale", false);
    calibrationFactor = preferences.getFloat("calib", calibrationFactor);
    
    // Load filtering parameters with load cell-specific defaults
    loadFilterSettings();
    loadQualityStats();
    
    // Auto-adjust brewing threshold based on calibration factor and load cell characteristics
    // Only if not previously saved by user (check if key exists)
    if (!preferences.isKey("brew_thresh")) {
        // For 3kg load cells (1mV/V): calibration factors typically 400-800
        // For 500g load cells (2mV/V): calibration factors typically 2000-5000+
        if (calibrationFactor < 1000) {
            brewingThreshold = 0.25f;  // 3kg load cell with 1mV/V - needs higher threshold due to lower sensitivity
            Serial.println("Auto-detected 3kg load cell (low calibration factor)");
        } else if (calibrationFactor < 2500) {
            brewingThreshold = 0.15f; // Medium sensitivity load cell
            Serial.println("Auto-detected medium sensitivity load cell");
        } else {
            brewingThreshold = 0.1f;  // 500g load cell with 2mV/V - more sensitive, can use lower threshold
            Serial.println("Auto-detected high sensitivity load cell (500g/2mV/V type)");
        }
        saveFilterSettings(); // Save auto-detected values
    }
    
    preferences.end();

#if WMBP_SIMULATION_MODE
    Serial.printf("WMB+ SIMULATION MODE: HX711 hardware is bypassed; scenario=%s targetHz=%d batteryProfile=%s\n",
                  SimulationProfiles::scenarioName(WMBP_SIM_SCENARIO),
                  WMBP_SIM_HX711_HZ,
                  SimulationProfiles::batteryProfileName(WMBP_SIM_BATTERY_PROFILE));
    simulationStartMillis = millis();
    simulationLastSampleMillis = 0;
    simulationLastSampleMicros = 0;
    simulationTareOffset = simulatedRawWeight(simulationStartMillis);
    isConnected = true;
    currentWeight = 0.0f;
    lastTareMillis = millis();
    resetPlausibilityGate();
    resetZeroQualification();
    initializeSamples(0.0f);
    return true;
#endif
    
    // Initialize HX711 with error handling
    Serial.println("Initializing HX711...");
    hx711.begin(dataPin, clockPin);
    hx711.set_scale(calibrationFactor);
    
    // Test if HX711 is responding with a timeout
    Serial.println("Testing HX711 connection...");
    unsigned long startTime = millis();
    bool testPassed = false;
    
    // Try to get a reading with 3 second timeout
    while (millis() - startTime < 3000) {
        if (hx711.is_ready()) {
            long testReading = hx711.read();
            if (testReading != 0) {  // HX711 returns 0 when not connected
                testPassed = true;
                Serial.println("HX711 test reading: " + String(testReading));
                break;
            }
        }
        delay(100);  // Small delay between attempts
    }
    
    if (testPassed) {
        Serial.println("HX711 connected successfully");
        isConnected = true;
        
        // Only tare if connection is confirmed
        Serial.println("Performing initial tare...");
        hx711.tare();
        lastTareMillis = millis();
        resetPlausibilityGate();
        resetZeroQualification();
        
        Serial.println("Smart Scale filtering configured:");
        Serial.println("Brewing threshold: " + String(brewingThreshold) + "g");
        Serial.println("Stability timeout: " + String(stabilityTimeout) + "ms");
        Serial.println("Median samples (brewing): " + String(medianSamples));
        Serial.println("Average samples (stable): " + String(averageSamples));
        Serial.println("Smart filtering: ENABLED - Dynamic filter switching based on brewing activity");
        
        return true;
    } else {
        Serial.println("ERROR: HX711 not responding!");
        Serial.println("Check connections:");
        Serial.println("- VCC to 3.3V or 5V");
        Serial.println("- GND to GND");
        Serial.println("- DT to GPIO " + String(dataPin));
        Serial.println("- SCK to GPIO " + String(clockPin));
        Serial.println("- Load cell connections");
        
        isConnected = false;
        return false;
    }
}

void Scale::tare(uint8_t times) {
    if (!isConnected) {
        Serial.println("Cannot tare: HX711 not connected");
        return;
    }

#if WMBP_SIMULATION_MODE
    (void)times;
    if (flowRatePtr != nullptr) {
        flowRatePtr->pauseCalculation();
    }

    Serial.println("Taring simulated scale...");
    simulationTareOffset = simulatedRawWeight(millis());
    simulationLastSampleMicros = 0;
    Serial.println("Simulated tare complete");

    currentFilterState = STABLE;
    lastBrewingActivity = 0;
    currentWeight = 0.0f;
    lastStableWeight = 0.0f;
    sampleSequence++;
    lastSampleMillis = millis();
    lastSampleMicros = 0;
    hasLastRawSampleWeight = false;
    lastTareMillis = lastSampleMillis;
    resetPlausibilityGate();
    resetZeroQualification();
    persistQualityStatsIfNeeded(true);
    samplesInitialized = false;

    if (flowRatePtr != nullptr) {
        flowRatePtr->resumeCalculation();
    }
    return;
#endif
    
    // Pause flow rate calculation to prevent tare operation from affecting flow rate
    if (flowRatePtr != nullptr) {
        flowRatePtr->pauseCalculation();
    }
    
    Serial.println("Taring scale...");
    hx711.tare(times);
    Serial.println("Tare complete");
    
    // Reset smart filter state after taring - return to stable mode
    currentFilterState = STABLE;
    lastBrewingActivity = 0;
    currentWeight = 0.0f;
    lastStableWeight = 0.0f;
    sampleSequence++;
    lastSampleMillis = millis();
    lastSampleMicros = 0;
    hasLastRawSampleWeight = false;
    lastTareMillis = lastSampleMillis;
    resetPlausibilityGate();
    resetZeroQualification();
    persistQualityStatsIfNeeded(true);
    
    // Reinitialize sample buffer
    samplesInitialized = false;
    Serial.println("Smart filter reset to STABLE state");
    
    // Resume flow rate calculation from the new zero reference.
    if (flowRatePtr != nullptr) {
        flowRatePtr->resumeCalculation();
    }
}

void Scale::set_scale(float factor) {
    if (!isfinite(factor) || factor < 10.0f || factor > 100000.0f) {
        Serial.printf("Rejected invalid scale calibration factor: %.6f\n", factor);
        return;
    }

    // Only save if the calibration factor actually changed
    if (calibrationFactor != factor) {
        calibrationFactor = factor;
        hx711.set_scale(calibrationFactor);
        saveCalibration();
    }
}

void Scale::saveCalibration() {
    preferences.begin("scale", false);
    preferences.putFloat("calib", calibrationFactor);
    preferences.end();
}

void Scale::loadCalibration() {
    preferences.begin("scale", true);
    calibrationFactor = preferences.getFloat("calib", calibrationFactor);
    preferences.end();
}

float Scale::getWeight() {
    // Return 0 if HX711 is not connected
    if (!isConnected) {
        return 0.0f;
    }
    
    unsigned long currentTime = millis();

#if WMBP_SIMULATION_MODE
    const uint32_t currentMicros = micros();
    const uint32_t simIntervalMicros = SimulationProfiles::sampleIntervalMicros(WMBP_SIM_HX711_HZ);
    if (simulationLastSampleMicros != 0 &&
        currentMicros - simulationLastSampleMicros < simIntervalMicros) {
        return currentWeight;
    }
    simulationLastSampleMicros = currentMicros;
    simulationLastSampleMillis = currentTime;
    float rawReading = simulatedRawWeight(currentTime) - simulationTareOffset;
#else
    // Check if HX711 is ready before attempting to read
    if (!hx711.is_ready()) {
        return currentWeight;  // Return last known value if not ready
    }
    
    float rawReading = hx711.get_units(1);
#endif
    
    // Handle NaN or invalid readings
    if (isnan(rawReading)) {
        return currentWeight;
    }

    float qualifiedRawReading = rawReading;
    if (!qualifyRawReading(currentTime, rawReading, qualifiedRawReading)) {
        return currentWeight;
    }
    rawReading = qualifiedRawReading;
    
    // Initialize sample buffer on first valid reading
    if (!samplesInitialized) {
        initializeSamples(rawReading);
        currentWeight = applyZeroQualification(currentTime, rawReading, rawReading);
        lastStableWeight = currentWeight;
        currentFilterState = STABLE;
        recordAcceptedSample(currentTime, rawReading, currentWeight);
        return currentWeight;
    }
    
    // Store reading in circular buffer
    readings[readingIndex] = rawReading;
    readingIndex = (readingIndex + 1) % MAX_SAMPLES;
    
    // Smart filtering based on brewing activity detection
    float weightChange = abs(rawReading - currentWeight);
    bool brewingDetected = false;
    
    // Detect brewing activity using configurable threshold
    if (currentFilterState == STABLE) {
        // Check if weight change exceeds brewing threshold
        if (weightChange > brewingThreshold) {
            brewingDetected = true;
            currentFilterState = BREWING;
            lastBrewingActivity = currentTime;
        }
    } else if (currentFilterState == BREWING) {
        // Continue monitoring for brewing activity
        if (weightChange > brewingThreshold) {
            brewingDetected = true;
            lastBrewingActivity = currentTime;
        } else {
            // Check if we should transition to stable
            if (currentTime - lastBrewingActivity > stabilityTimeout) {
                currentFilterState = TRANSITIONING;
            }
        }
    } else if (currentFilterState == TRANSITIONING) {
        // In transition phase - verify stability
        if (weightChange > brewingThreshold) {
            // Activity detected again - back to brewing
            brewingDetected = true;
            currentFilterState = BREWING;
            lastBrewingActivity = currentTime;
        } else if (currentTime - lastBrewingActivity > stabilityTimeout * 2) {
            // Extended stability confirmed - switch to stable mode
            currentFilterState = STABLE;
            lastStableWeight = currentWeight;
        }
    }
    
    // Apply appropriate filter based on current state
    float filteredWeight;
    switch (currentFilterState) {
        case BREWING:
            // Use median filter during brewing for noise rejection
            filteredWeight = medianFilter(medianSamples);
            break;
        case STABLE:
        case TRANSITIONING:
            // Use average filter for stable readings - smoother and faster
            filteredWeight = averageFilter(averageSamples);
            break;
    }
    
    // Handle rapid changes (>5g) with immediate response regardless of filter state
    if (weightChange > 5.0f) {
        filteredWeight = rawReading;
        // Reset sample buffer for immediate response
        initializeSamples(rawReading);
        // Update state appropriately
        if (currentFilterState == STABLE) {
            currentFilterState = BREWING;
            lastBrewingActivity = currentTime;
        }
    }
    
    currentWeight = applyZeroQualification(currentTime, rawReading, filteredWeight);
    recordAcceptedSample(currentTime, rawReading, currentWeight);
    return currentWeight;
}

float Scale::getCurrentWeight() {
    return currentWeight;
}

void Scale::recordSampleCadence(unsigned long sampleMillis) {
    (void)sampleMillis;
    const bool longGap = cadenceTracker.recordSampleMicros(micros());

    lastSampleIntervalMicros = cadenceTracker.getLastIntervalMicros();
    sampleIntervalTotalMicros = cadenceTracker.getTotalMicros();
    sampleIntervalStatsCount = cadenceTracker.getStatsCount();
    sampleIntervalMinMicros = cadenceTracker.getMinMicros();
    sampleIntervalMaxMicros = cadenceTracker.getMaxMicros();
    sampleIntervalLongGapCount = cadenceTracker.getLongGapCount();

    if (longGap) {
        lifetimeLongGapCount++;
    }
}

void Scale::recordAcceptedSample(unsigned long sampleMillis, float rawReading, float publicWeight) {
    sampleSequence++;
    lastSampleMillis = sampleMillis;
    lifetimeSampleCount++;
    samplesSinceQualityPersist++;
    recordSampleCadence(sampleMillis);
    recordMeasurementQuality(sampleMillis, rawReading, publicWeight);
    lastAcceptedRawReading = rawReading;
    hasLastAcceptedRawReading = true;
    lastRawValueCounts = lroundf(rawReading * calibrationFactor);
    hasLastRawValueCounts = true;
    persistQualityStatsIfNeeded(false);
}

void Scale::recordMeasurementQuality(unsigned long sampleMillis, float rawReading, float publicWeight) {
    static constexpr float BUMP_THRESHOLD_GRAMS = 5.0f;

    if (hasLastRawSampleWeight && sampleMillis - lastTareMillis > ZERO_SUPPRESS_AFTER_TARE_MS) {
        const float rawDelta = fabsf(rawReading - lastRawSampleWeight);
        (void)publicWeight;

        // This is intentionally a coarse no-accelerometer disturbance detector:
        // a multi-gram instantaneous raw/load change is extremely unlikely during
        // espresso flow but common with cup knocks, scale bumps, or load shifts.
        if (rawDelta >= BUMP_THRESHOLD_GRAMS) {
            bumpCount++;
            lifetimeBumpCount++;
            lastBumpMillis = sampleMillis;
            lastBumpMagnitudeGrams = rawDelta;
            if (diagnosticEventLog != nullptr) {
                diagnosticEventLog->record(DiagnosticEventType::Bump, rawDelta, "accepted raw step", sampleSequence);
            }
        }
    }

    lastRawSampleWeight = rawReading;
    hasLastRawSampleWeight = true;
}

bool Scale::qualifyRawReading(unsigned long sampleMillis, float rawReading, float& qualifiedRawReading) {
    qualifiedRawReading = rawReading;

    if (!hasLastAcceptedRawReading || sampleMillis - lastTareMillis <= PLAUSIBILITY_SUPPRESS_AFTER_TARE_MS) {
        resetPlausibilityGate();
        return true;
    }

    const float baselineDelta = fabsf(rawReading - lastAcceptedRawReading);

    if (plausibilityCandidateActive) {
        const float candidateMovement = fabsf(plausibilityCandidateRaw - plausibilityBaselineRaw);
        const float confirmTolerance = fmaxf(PLAUSIBILITY_CONFIRM_TOLERANCE_GRAMS,
                                             candidateMovement * PLAUSIBILITY_CONFIRM_TOLERANCE_FRACTION);
        const float candidateDelta = fabsf(rawReading - plausibilityCandidateRaw);
        const bool stillAwayFromBaseline = baselineDelta >= PLAUSIBILITY_JUMP_GRAMS;
        const bool confirmsCandidate = stillAwayFromBaseline && candidateDelta <= confirmTolerance;
        const bool returnedToBaseline = baselineDelta < PLAUSIBILITY_JUMP_GRAMS;
        const bool timedOut = sampleMillis - plausibilityCandidateMillis > PLAUSIBILITY_CANDIDATE_TIMEOUT_MS;

        if (confirmsCandidate) {
            plausibilityCandidateCount++;
            if (plausibilityCandidateCount >= PLAUSIBILITY_CONFIRM_SAMPLE_COUNT) {
                resetPlausibilityGate();
                return true;
            }
            return false;
        }

        if (returnedToBaseline || timedOut) {
            recordRejectedGlitch(sampleMillis, candidateMovement);
            resetPlausibilityGate();
            return true;
        }

        // Still implausible, but not consistent with the first candidate. Treat
        // the prior candidate as a glitch and start over from this raw sample.
        recordRejectedGlitch(sampleMillis, candidateMovement);
        plausibilityCandidateActive = true;
        plausibilityCandidateCount = 1;
        plausibilityCandidateMillis = sampleMillis;
        plausibilityCandidateRaw = rawReading;
        plausibilityBaselineRaw = lastAcceptedRawReading;
        return false;
    }

    if (baselineDelta >= PLAUSIBILITY_JUMP_GRAMS) {
        plausibilityCandidateActive = true;
        plausibilityCandidateCount = 1;
        plausibilityCandidateMillis = sampleMillis;
        plausibilityCandidateRaw = rawReading;
        plausibilityBaselineRaw = lastAcceptedRawReading;
        return false;
    }

    return true;
}

void Scale::recordRejectedGlitch(unsigned long sampleMillis, float magnitudeGrams) {
    glitchCount++;
    lifetimeGlitchCount++;
    samplesSinceQualityPersist++;
    lastGlitchMillis = sampleMillis;
    lastGlitchMagnitudeGrams = magnitudeGrams;
    if (diagnosticEventLog != nullptr) {
        diagnosticEventLog->record(DiagnosticEventType::Glitch, magnitudeGrams, "plausibility rejected", sampleSequence);
    }
    persistQualityStatsIfNeeded(false);
}

void Scale::resetPlausibilityGate() {
    plausibilityCandidateActive = false;
    plausibilityCandidateCount = 0;
    plausibilityCandidateMillis = 0;
    plausibilityCandidateRaw = 0.0f;
    plausibilityBaselineRaw = 0.0f;
    lastAcceptedRawReading = 0.0f;
    hasLastAcceptedRawReading = false;
}

float Scale::applyZeroQualification(unsigned long sampleMillis, float rawReading, float filteredWeight) {
    float correctedWeight = filteredWeight - autoZeroCorrectionGrams;

    autoZeroActive = lastAutoZeroAdjustMillis > 0 &&
                     sampleMillis - lastAutoZeroAdjustMillis <= AUTO_ZERO_ACTIVE_WINDOW_MS;

    const bool suppressAfterTare = sampleMillis - lastTareMillis <= ZERO_SUPPRESS_AFTER_TARE_MS;
    const bool recentBump = hasRecentBump(2000);
    const bool recentGlitch = hasRecentGlitch(2000);
    const bool stableFilter = currentFilterState == STABLE;
    const bool nearZero = fabsf(correctedWeight) <= AUTO_ZERO_RANGE_GRAMS;
    const bool quietRawStep = !hasLastRawSampleWeight ||
                              fabsf(rawReading - lastRawSampleWeight) <= AUTO_ZERO_RAW_STEP_GRAMS;
    const bool zeroCandidate = stableFilter && !suppressAfterTare && !recentBump && !recentGlitch && nearZero && quietRawStep;

    if (zeroCandidate) {
        if (zeroWindowStartMillis == 0) {
            zeroWindowStartMillis = sampleMillis;
            zeroWindowMinGrams = correctedWeight;
            zeroWindowMaxGrams = correctedWeight;
        } else {
            if (correctedWeight < zeroWindowMinGrams) {
                zeroWindowMinGrams = correctedWeight;
            }
            if (correctedWeight > zeroWindowMaxGrams) {
                zeroWindowMaxGrams = correctedWeight;
            }
        }
    } else {
        zeroWindowStartMillis = 0;
        zeroWindowMinGrams = correctedWeight;
        zeroWindowMaxGrams = correctedWeight;
        if (!nearZero || recentBump || recentGlitch || !stableFilter) {
            zeroClampActive = false;
        }
    }

    const bool zeroWindowReady = zeroWindowStartMillis > 0 &&
                                 sampleMillis - zeroWindowStartMillis >= AUTO_ZERO_STABLE_MS;
    const float zeroWindowPeakToPeak = zeroWindowMaxGrams - zeroWindowMinGrams;
    const bool canAutoZero = zeroWindowReady &&
                             zeroWindowPeakToPeak <= AUTO_ZERO_WINDOW_P2P_GRAMS &&
                             sampleMillis - lastAutoZeroAdjustMillis >= AUTO_ZERO_ADJUST_INTERVAL_MS;

    if (canAutoZero && fabsf(correctedWeight) > 0.02f) {
        const float adjustment = clampFloat(correctedWeight * AUTO_ZERO_STEP_FRACTION,
                                            -AUTO_ZERO_MAX_STEP_GRAMS,
                                            AUTO_ZERO_MAX_STEP_GRAMS);
        if (fabsf(adjustment) >= 0.001f) {
            autoZeroCorrectionGrams = clampFloat(autoZeroCorrectionGrams + adjustment,
                                                -AUTO_ZERO_MAX_CORRECTION_GRAMS,
                                                AUTO_ZERO_MAX_CORRECTION_GRAMS);
            lastAutoZeroAdjustMillis = sampleMillis;
            autoZeroActive = true;
            correctedWeight = filteredWeight - autoZeroCorrectionGrams;
        }
    }

    if (zeroClampActive) {
        if (fabsf(correctedWeight) > ZERO_CLAMP_EXIT_GRAMS) {
            zeroClampActive = false;
        }
    } else {
        const bool clampWindowReady = zeroWindowStartMillis > 0 &&
                                      sampleMillis - zeroWindowStartMillis >= ZERO_CLAMP_ENTER_MS;
        if (clampWindowReady && fabsf(correctedWeight) <= ZERO_CLAMP_ENTER_GRAMS) {
            zeroClampActive = true;
        }
    }

    return zeroClampActive ? 0.0f : correctedWeight;
}

void Scale::resetZeroQualification() {
    zeroClampActive = false;
    autoZeroActive = false;
    zeroWindowStartMillis = 0;
    lastAutoZeroAdjustMillis = 0;
    zeroWindowMinGrams = 0.0f;
    zeroWindowMaxGrams = 0.0f;
    autoZeroCorrectionGrams = 0.0f;
}

void Scale::resetSampleCadenceStats() {
    cadenceTracker.reset();
    lastSampleMicros = 0;
    lastSampleIntervalMicros = 0;
    sampleIntervalTotalMicros = 0;
    sampleIntervalStatsCount = 0;
    sampleIntervalMinMicros = 0;
    sampleIntervalMaxMicros = 0;
    sampleIntervalLongGapCount = 0;
    bumpCount = 0;
    lastBumpMillis = 0;
    lastBumpMagnitudeGrams = 0.0f;
    glitchCount = 0;
    lastGlitchMillis = 0;
    lastGlitchMagnitudeGrams = 0.0f;
    hasLastRawSampleWeight = false;
    resetPlausibilityGate();
}

uint32_t Scale::getSampleIntervalAverageMicros() const {
    return cadenceTracker.getAverageMicros();
}

float Scale::getDetectedSampleRateHz() const {
    return cadenceTracker.getRateHz();
}

String Scale::getDetectedHx711RateMode() const {
    return String(cadenceTracker.getRateMode());
}

uint8_t Scale::getDetectedSampleRateRoundedHz() const {
    return cadenceTracker.getRoundedRateHz();
}

bool Scale::hasRecentBump(unsigned long windowMs) const {
    return lastBumpMillis > 0 && millis() - lastBumpMillis <= windowMs;
}

bool Scale::hasRecentGlitch(unsigned long windowMs) const {
    return lastGlitchMillis > 0 && millis() - lastGlitchMillis <= windowMs;
}

uint8_t Scale::scoreFromRates(uint32_t sampleCount, uint32_t longGapCount, uint32_t bumpCount, uint32_t glitchCount) {
    if (sampleCount == 0) {
        return 0;
    }

    int score = 100;
    const float gapRate = static_cast<float>(longGapCount) / static_cast<float>(sampleCount);
    const float bumpRate = static_cast<float>(bumpCount) / static_cast<float>(sampleCount);
    const float glitchRate = static_cast<float>(glitchCount) / static_cast<float>(sampleCount);

    score -= min(40, static_cast<int>(roundf(gapRate * 1000.0f)));
    score -= min(30, static_cast<int>(roundf(bumpRate * 1000.0f)));
    score -= min(30, static_cast<int>(roundf(glitchRate * 2000.0f)));

    return static_cast<uint8_t>(constrain(score, 0, 100));
}

uint8_t Scale::getScaleQualityScore() const {
    if (!isConnected) {
        return 0;
    }

    uint8_t score = scoreFromRates(sampleSequence > 0 ? sampleSequence : 1, sampleIntervalLongGapCount, bumpCount, glitchCount);

    if (sampleIntervalStatsCount >= 5) {
        const uint32_t avgMicros = getSampleIntervalAverageMicros();
        const float rateHz = getDetectedSampleRateHz();

        if (avgMicros > 0 && lastSampleIntervalMicros > avgMicros * 3UL && lastSampleIntervalMicros > 250000UL) {
            score = score > 10 ? score - 10 : 0;
        }

        if (rateHz < 6.0f) {
            score = score > 20 ? score - 20 : 0;
        } else if (rateHz >= 15.0f && rateHz < 50.0f) {
            score = score > 10 ? score - 10 : 0;
        } else if (rateHz > 95.0f) {
            score = score > 5 ? score - 5 : 0;
        }
    }

    if (hasRecentBump()) {
        score = score > 15 ? score - 15 : 0;
    }

    if (hasRecentGlitch()) {
        score = score > 15 ? score - 15 : 0;
    }

    return score;
}

uint8_t Scale::getLifetimeQualityScore() const {
    return scoreFromRates(lifetimeSampleCount, lifetimeLongGapCount, lifetimeBumpCount, lifetimeGlitchCount);
}

void Scale::loadQualityStats() {
    lifetimeSampleCount = preferences.getUInt("q_samples", 0);
    lifetimeLongGapCount = preferences.getUInt("q_gaps", 0);
    lifetimeBumpCount = preferences.getUInt("q_bumps", 0);
    lifetimeGlitchCount = preferences.getUInt("q_glitches", 0);
    samplesSinceQualityPersist = 0;
    lastQualityPersistMillis = millis();
    Serial.printf("Scale quality lifetime loaded: score=%u samples=%lu gaps=%lu bumps=%lu glitches=%lu\n",
                  getLifetimeQualityScore(),
                  static_cast<unsigned long>(lifetimeSampleCount),
                  static_cast<unsigned long>(lifetimeLongGapCount),
                  static_cast<unsigned long>(lifetimeBumpCount),
                  static_cast<unsigned long>(lifetimeGlitchCount));
}

void Scale::persistQualityStatsIfNeeded(bool force) {
    const unsigned long now = millis();
    const bool enoughSamples = samplesSinceQualityPersist >= 5000;
    const bool enoughTime = now - lastQualityPersistMillis >= 300000UL;

    if (!force && !enoughSamples && !enoughTime) {
        return;
    }

    preferences.begin("scale", false);
    preferences.putUInt("q_samples", lifetimeSampleCount);
    preferences.putUInt("q_gaps", lifetimeLongGapCount);
    preferences.putUInt("q_bumps", lifetimeBumpCount);
    preferences.putUInt("q_glitches", lifetimeGlitchCount);
    preferences.end();

    samplesSinceQualityPersist = 0;
    lastQualityPersistMillis = now;
}

long Scale::getRawValue() {
    if (!isConnected) {
        return 0;  // Return 0 if HX711 not connected
    }
#if WMBP_SIMULATION_MODE
    return static_cast<long>((simulatedRawWeight(millis()) - simulationTareOffset) * 1000.0f);
#else
    return hx711.get_value(1); // Get raw value from HX711
#endif
}

void Scale::powerDown() {
#if WMBP_SIMULATION_MODE
    Serial.println("HX711 power down skipped in WMB+ simulation mode");
#else
    if (isConnected) {
        hx711.power_down();
    }
    pinMode(clockPin, OUTPUT);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(80);
    Serial.println(isConnected
        ? "HX711 powered down for deep sleep; PD_SCK held HIGH"
        : "HX711 PD_SCK held HIGH for deep sleep before connection was confirmed");
#endif
}

void Scale::initializeSamples(float initialValue) {
    for (int i = 0; i < MAX_SAMPLES; i++) {
        readings[i] = initialValue;
    }
    samplesInitialized = true;
}

float Scale::medianFilter(int samples) {
    if (samples > MAX_SAMPLES) samples = MAX_SAMPLES;
    
    // Copy recent readings
    float temp[samples];
    for (int i = 0; i < samples; i++) {
        int idx = (readingIndex - 1 - i + MAX_SAMPLES) % MAX_SAMPLES;
        temp[i] = readings[idx];
    }
    
    // Simple bubble sort for median (efficient for small arrays)
    for (int i = 0; i < samples - 1; i++) {
        for (int j = 0; j < samples - i - 1; j++) {
            if (temp[j] > temp[j + 1]) {
                float swap = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = swap;
            }
        }
    }
    return temp[samples / 2]; // Return median
}

float Scale::averageFilter(int samples) {
    if (samples > MAX_SAMPLES) samples = MAX_SAMPLES;
    
    float sum = 0;
    int validSamples = 0;
    
    // Calculate average of recent samples
    for (int i = 0; i < samples; i++) {
        int idx = (readingIndex - 1 - i + MAX_SAMPLES) % MAX_SAMPLES;
        sum += readings[idx];
        validSamples++;
    }
    
    return sum / validSamples; // Return simple average without additional smoothing
}

// Filter parameter setters with validation
void Scale::setBrewingThreshold(float threshold) {
    if (threshold >= 0.05f && threshold <= 1.0f) { // Reasonable bounds
        brewingThreshold = threshold;
        saveFilterSettings();
    }
}

void Scale::setStabilityTimeout(unsigned long timeout) {
    if (timeout >= 500 && timeout <= 10000) { // 0.5-10 seconds
        stabilityTimeout = timeout;
        saveFilterSettings();
    }
}

void Scale::setMedianSamples(int samples) {
    if (samples >= 1 && samples <= MAX_SAMPLES) {
        medianSamples = samples;
        saveFilterSettings();
    }
}

void Scale::setAverageSamples(int samples) {
    if (samples >= 1 && samples <= MAX_SAMPLES) {
        averageSamples = samples;
        saveFilterSettings();
    }
}

void Scale::saveFilterSettings() {
    preferences.begin("scale", false);
    preferences.putFloat("brew_thresh", brewingThreshold);
    preferences.putULong("stab_timeout", stabilityTimeout);
    preferences.putInt("median_samples", medianSamples);
    preferences.putInt("avg_samples", averageSamples);
    preferences.end();
    Serial.println("Filter settings saved to EEPROM");
}

void Scale::loadFilterSettings() {
    // Load with sensible defaults
    brewingThreshold = preferences.getFloat("brew_thresh", 0.15f);
    stabilityTimeout = preferences.getULong("stab_timeout", 2000);
    medianSamples = preferences.getInt("median_samples", 3);
    averageSamples = preferences.getInt("avg_samples", 2); // Reduced for faster response
}

void Scale::setFlowRatePtr(FlowRate* flowRatePtr) {
    this->flowRatePtr = flowRatePtr;
}

String Scale::getFilterState() const {
    switch (currentFilterState) {
        case STABLE: return "STABLE";
        case BREWING: return "BREWING";
        case TRANSITIONING: return "TRANSITIONING";
        default: return "UNKNOWN";
    }
}
