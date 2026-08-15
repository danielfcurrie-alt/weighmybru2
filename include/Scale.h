#ifndef SCALE_H
#define SCALE_H

#include <HX711.h>
#include <Preferences.h>
#include "BoardConfig.h"
#include "SampleCadenceTracker.h"
#include "SimulationProfiles.h"

class DiagnosticEventLog;

class Scale {
public:
    Scale(uint8_t dataPin, uint8_t clockPin, float calibrationFactor);
    bool begin();  // Returns true if successful, false if HX711 fails
    void tare(uint8_t times = 20);
    void set_scale(float factor);
    float getWeight();
    float getCurrentWeight();
    uint32_t getSampleSequence() const { return sampleSequence; }
    unsigned long getLastSampleMillis() const { return lastSampleMillis; }
    float getDetectedSampleRateHz() const;
    uint32_t getSampleIntervalAverageMicros() const;
    uint32_t getSampleIntervalMinMicros() const { return sampleIntervalMinMicros; }
    uint32_t getSampleIntervalMaxMicros() const { return sampleIntervalMaxMicros; }
    uint32_t getSampleIntervalLongGapCount() const { return sampleIntervalLongGapCount; }
    uint32_t getSampleIntervalStatsCount() const { return sampleIntervalStatsCount; }
    String getDetectedHx711RateMode() const;
    uint8_t getDetectedSampleRateRoundedHz() const;
    long getLastRawValue() const { return lastRawValueCounts; }
    bool hasLastRawValue() const { return hasLastRawValueCounts; }
    uint8_t getScaleQualityScore() const;
    uint8_t getLifetimeQualityScore() const;
    uint32_t getBumpCount() const { return bumpCount; }
    unsigned long getLastBumpMillis() const { return lastBumpMillis; }
    float getLastBumpMagnitudeGrams() const { return lastBumpMagnitudeGrams; }
    bool hasRecentBump(unsigned long windowMs = 1500) const;
    uint32_t getGlitchCount() const { return glitchCount; }
    unsigned long getLastGlitchMillis() const { return lastGlitchMillis; }
    float getLastGlitchMagnitudeGrams() const { return lastGlitchMagnitudeGrams; }
    bool hasRecentGlitch(unsigned long windowMs = 1500) const;
    uint32_t getLifetimeSampleCount() const { return lifetimeSampleCount; }
    uint32_t getLifetimeLongGapCount() const { return lifetimeLongGapCount; }
    uint32_t getLifetimeBumpCount() const { return lifetimeBumpCount; }
    uint32_t getLifetimeGlitchCount() const { return lifetimeGlitchCount; }
    bool isZeroClamped() const { return zeroClampActive; }
    bool isAutoZeroActive() const { return autoZeroActive; }
    float getAutoZeroCorrectionGrams() const { return autoZeroCorrectionGrams; }
    void resetSampleCadenceStats();
    long getRawValue();
    void saveCalibration(); // Save calibration factor to NVS
    void loadCalibration(); // Load calibration factor from NVS
    float getCalibrationFactor() const { return calibrationFactor; } // Getter for API
    bool isHX711Connected() const { return isConnected; } // Check if HX711 is responding
    void powerDown(); // Put HX711 into its low-power state before ESP deep sleep
    
    // Filtering configuration - adjustable for different load cells
    void setBrewingThreshold(float threshold);
    void setStabilityTimeout(unsigned long timeout);
    void setMedianSamples(int samples);
    void setAverageSamples(int samples);
    
    float getBrewingThreshold() const { return brewingThreshold; }
    unsigned long getStabilityTimeout() const { return stabilityTimeout; }
    int getMedianSamples() const { return medianSamples; }
    int getAverageSamples() const { return averageSamples; }
    String getFilterState() const; // Get current filter state as string for debugging
    
    void saveFilterSettings();
    void loadFilterSettings();
    
    // FlowRate integration for tare operations
    void setFlowRatePtr(class FlowRate* flowRatePtr);
    void setDiagnosticEventLog(DiagnosticEventLog* log) { diagnosticEventLog = log; }
    
private:
    HX711 hx711;
    Preferences preferences;
    uint8_t dataPin;
    uint8_t clockPin;
    float calibrationFactor = 0.0f;
    float currentWeight;
    uint32_t sampleSequence = 0;      // Advances when the public weight value is refreshed
    unsigned long lastSampleMillis = 0;
    uint32_t lastSampleMicros = 0;
    uint64_t sampleIntervalTotalMicros = 0;
    uint32_t sampleIntervalStatsCount = 0;
    uint32_t sampleIntervalMinMicros = 0;
    uint32_t sampleIntervalMaxMicros = 0;
    uint32_t sampleIntervalLongGapCount = 0;
    uint32_t lastSampleIntervalMicros = 0;
    float lastRawSampleWeight = 0.0f;
    bool hasLastRawSampleWeight = false;
    long lastRawValueCounts = 0;
    bool hasLastRawValueCounts = false;
    uint32_t bumpCount = 0;
    unsigned long lastBumpMillis = 0;
    float lastBumpMagnitudeGrams = 0.0f;
    uint32_t glitchCount = 0;
    unsigned long lastGlitchMillis = 0;
    float lastGlitchMagnitudeGrams = 0.0f;
    unsigned long lastTareMillis = 0;
    uint32_t lifetimeSampleCount = 0;
    uint32_t lifetimeLongGapCount = 0;
    uint32_t lifetimeBumpCount = 0;
    uint32_t lifetimeGlitchCount = 0;
    uint32_t samplesSinceQualityPersist = 0;
    unsigned long lastQualityPersistMillis = 0;
    bool plausibilityCandidateActive = false;
    uint8_t plausibilityCandidateCount = 0;
    unsigned long plausibilityCandidateMillis = 0;
    float plausibilityCandidateRaw = 0.0f;
    float plausibilityBaselineRaw = 0.0f;
    float lastAcceptedRawReading = 0.0f;
    bool hasLastAcceptedRawReading = false;
    bool zeroClampActive = false;
    bool autoZeroActive = false;
    unsigned long zeroWindowStartMillis = 0;
    unsigned long lastAutoZeroAdjustMillis = 0;
    float zeroWindowMinGrams = 0.0f;
    float zeroWindowMaxGrams = 0.0f;
    float autoZeroCorrectionGrams = 0.0f;
    bool isConnected = false;  // Track HX711 connection status
    class FlowRate* flowRatePtr = nullptr; // For pausing flow rate during tare
    DiagnosticEventLog* diagnosticEventLog = nullptr;
    SampleCadenceTracker cadenceTracker;
    
    // Smart filtering variables - reduced buffer for faster response
    static const int MAX_SAMPLES = 10;  // Reduced from 50 to 10 for faster response
    float readings[MAX_SAMPLES];
    int readingIndex = 0;
    bool samplesInitialized = false;
    float previousFilteredWeight = 0;
    
    // Brewing state tracking for smart filtering
    enum FilterState {
        STABLE,     // Using average filter - stable weight
        BREWING,    // Using median filter - active brewing
        TRANSITIONING // Waiting for stability after brewing activity
    };
    FilterState currentFilterState = STABLE;
    unsigned long lastBrewingActivity = 0;  // Track when brewing was last detected
    float lastStableWeight = 0.0f;          // Last weight when in stable state
    
    // Configurable filtering parameters
    float brewingThreshold = 0.15f;  // Keep for API compatibility
    unsigned long stabilityTimeout = 2000;  // Keep for API compatibility
    int medianSamples = 3;  // Keep for API compatibility
    int averageSamples = 2;  // Samples for average filter - reduced for faster response
    
    // Filter methods
    float medianFilter(int samples);
    float averageFilter(int samples);
    void initializeSamples(float initialValue);
    void recordSampleCadence(unsigned long sampleMillis);
    void recordAcceptedSample(unsigned long sampleMillis, float rawReading, float publicWeight);
    void recordMeasurementQuality(unsigned long sampleMillis, float rawReading, float publicWeight);
    bool qualifyRawReading(unsigned long sampleMillis, float rawReading, float& qualifiedRawReading);
    void recordRejectedGlitch(unsigned long sampleMillis, float magnitudeGrams);
    void resetPlausibilityGate();
    float applyZeroQualification(unsigned long sampleMillis, float rawReading, float filteredWeight);
    void resetZeroQualification();
    void loadQualityStats();
    void persistQualityStatsIfNeeded(bool force = false);
    static uint8_t scoreFromRates(uint32_t sampleCount, uint32_t longGapCount, uint32_t bumpCount, uint32_t glitchCount);

#if WMBP_SIMULATION_MODE
    unsigned long simulationStartMillis = 0;
    unsigned long simulationLastSampleMillis = 0;
    uint32_t simulationLastSampleMicros = 0;
    float simulationTareOffset = 0.0f;
    float simulatedRawWeight(unsigned long sampleMillis) const;
#endif
};

#endif
