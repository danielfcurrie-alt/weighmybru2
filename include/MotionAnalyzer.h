#ifndef MOTION_ANALYZER_H
#define MOTION_ANALYZER_H

#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include <stdint.h>

class MotionAnalyzer {
public:
    struct Config {
        float gravityTimeConstantMs = 750.0f;
        float vibrationTimeConstantMs = 160.0f;
        float quietRiseTimeConstantMs = 450.0f;
        float quietFallTimeConstantMs = 80.0f;
        float quietRmsThresholdG = 0.020f;
        float activeRmsThresholdG = 0.100f;
        float impactStartThresholdG = 0.300f;
        float impactEndThresholdG = 0.090f;
        float maximumTapBackgroundRmsG = 0.140f;
        uint32_t impactSettleMs = 50;
        uint32_t maximumTapDurationMs = 140;
        uint32_t minimumDoubleTapSeparationMs = 100;
        uint32_t maximumDoubleTapSeparationMs = 450;
    };

    struct Sample {
        float xG = 0.0f;
        float yG = 0.0f;
        float zG = 0.0f;
        uint32_t timestampMs = 0;
    };

    enum class State : uint8_t {
        Uninitialized,
        Quiet,
        Active,
        Impact,
    };

    struct Diagnostics {
        bool initialized = false;
        State state = State::Uninitialized;
        uint32_t sampleCount = 0;
        uint32_t lastSampleMs = 0;
        float gravityXG = 0.0f;
        float gravityYG = 0.0f;
        float gravityZG = 0.0f;
        float gravityMagnitudeG = 0.0f;
        float rollDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float linearAccelerationG = 0.0f;
        float vibrationEnergyG2 = 0.0f;
        float vibrationRmsG = 0.0f;
        float impactPeakG = 0.0f;
        float quietConfidence = 0.0f;
        bool impactActive = false;
        bool tapCandidate = false;
        bool doubleTapCandidate = false;
        uint32_t impactCount = 0;
        uint32_t tapCandidateCount = 0;
        uint32_t doubleTapCandidateCount = 0;
        uint32_t lastImpactMs = 0;
        uint32_t lastImpactDurationMs = 0;
        float lastImpactPeakG = 0.0f;
        float lastImpactBackgroundRmsG = 0.0f;
        bool lastImpactWasTapCandidate = false;
        uint32_t lastTapCandidateMs = 0;
    };

    MotionAnalyzer();
    explicit MotionAnalyzer(const Config& config);

    void reset();
    bool update(const Sample& sample);
    const Diagnostics& diagnostics() const { return snapshot; }
    const Config& configuration() const { return config; }
    static const char* stateName(State state);

private:
    Config config;
    Diagnostics snapshot;
    uint32_t impactStartedMs = 0;
    uint32_t impactLastAboveMs = 0;
    uint32_t impactPeakMs = 0;
    uint32_t previousTapCandidateMs = 0;
    float activeImpactPeakG = 0.0f;
    float impactBackgroundRmsG = 0.0f;

    static float clamp01(float value);
    static float smoothingAlpha(float elapsedMs, float timeConstantMs);
    void finishImpact();
};

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
#endif
