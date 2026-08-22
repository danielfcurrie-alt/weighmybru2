#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include "MotionAnalyzer.h"

#include <math.h>

namespace {
constexpr float RADIANS_TO_DEGREES = 57.2957795131f;
}

MotionAnalyzer::MotionAnalyzer()
    : MotionAnalyzer(Config{}) {}

MotionAnalyzer::MotionAnalyzer(const Config& newConfig)
    : config(newConfig) {}

void MotionAnalyzer::reset() {
    snapshot = Diagnostics{};
    impactStartedMs = 0;
    impactLastAboveMs = 0;
    impactPeakMs = 0;
    previousTapCandidateMs = 0;
    activeImpactPeakG = 0.0f;
    impactBackgroundRmsG = 0.0f;
}

bool MotionAnalyzer::update(const Sample& sample) {
    if (!isfinite(sample.xG) || !isfinite(sample.yG) || !isfinite(sample.zG)) {
        return false;
    }

    snapshot.tapCandidate = false;
    snapshot.doubleTapCandidate = false;

    if (!snapshot.initialized) {
        snapshot.initialized = true;
        snapshot.state = State::Quiet;
        snapshot.sampleCount = 1;
        snapshot.lastSampleMs = sample.timestampMs;
        snapshot.gravityXG = sample.xG;
        snapshot.gravityYG = sample.yG;
        snapshot.gravityZG = sample.zG;
        snapshot.gravityMagnitudeG = sqrtf(sample.xG * sample.xG +
                                           sample.yG * sample.yG +
                                           sample.zG * sample.zG);
        snapshot.rollDegrees = atan2f(sample.yG, sample.zG) * RADIANS_TO_DEGREES;
        snapshot.pitchDegrees = atan2f(-sample.xG,
                                       sqrtf(sample.yG * sample.yG + sample.zG * sample.zG)) *
                                RADIANS_TO_DEGREES;
        snapshot.quietConfidence = 1.0f;
        return true;
    }

    uint32_t elapsedMs = sample.timestampMs - snapshot.lastSampleMs;
    if (elapsedMs == 0) {
        elapsedMs = 1;
    }
    const float boundedElapsedMs = elapsedMs > 250 ? 250.0f : static_cast<float>(elapsedMs);

    const float previousDx = sample.xG - snapshot.gravityXG;
    const float previousDy = sample.yG - snapshot.gravityYG;
    const float previousDz = sample.zG - snapshot.gravityZG;
    const float previousLinearG = sqrtf(previousDx * previousDx +
                                        previousDy * previousDy +
                                        previousDz * previousDz);

    const float gravityElapsedMs = boundedElapsedMs > 25.0f ? 25.0f : boundedElapsedMs;
    float gravityAlpha = smoothingAlpha(gravityElapsedMs, config.gravityTimeConstantMs);
    if (previousLinearG > config.impactEndThresholdG) {
        gravityAlpha *= 0.60f;
    }
    snapshot.gravityXG += gravityAlpha * (sample.xG - snapshot.gravityXG);
    snapshot.gravityYG += gravityAlpha * (sample.yG - snapshot.gravityYG);
    snapshot.gravityZG += gravityAlpha * (sample.zG - snapshot.gravityZG);

    const float dx = sample.xG - snapshot.gravityXG;
    const float dy = sample.yG - snapshot.gravityYG;
    const float dz = sample.zG - snapshot.gravityZG;
    snapshot.linearAccelerationG = sqrtf(dx * dx + dy * dy + dz * dz);

    const float energyDecay = config.vibrationTimeConstantMs > 0.0f
        ? expf(-boundedElapsedMs / config.vibrationTimeConstantMs)
        : 0.0f;
    snapshot.vibrationEnergyG2 *= energyDecay;
    const float backgroundRmsG = sqrtf(snapshot.vibrationEnergyG2);
    const float observationMs = boundedElapsedMs > 25.0f ? 25.0f : boundedElapsedMs;
    const float energyAlpha = smoothingAlpha(observationMs, config.vibrationTimeConstantMs);
    const float instantaneousEnergy = snapshot.linearAccelerationG * snapshot.linearAccelerationG;
    snapshot.vibrationEnergyG2 += energyAlpha * instantaneousEnergy;
    if (snapshot.vibrationEnergyG2 < 0.0f) {
        snapshot.vibrationEnergyG2 = 0.0f;
    }
    snapshot.vibrationRmsG = sqrtf(snapshot.vibrationEnergyG2);

    const float peakDecay = expf(-boundedElapsedMs / 700.0f);
    snapshot.impactPeakG *= peakDecay;
    if (snapshot.linearAccelerationG > snapshot.impactPeakG) {
        snapshot.impactPeakG = snapshot.linearAccelerationG;
    }

    if (!snapshot.impactActive &&
        snapshot.linearAccelerationG >= config.impactStartThresholdG) {
        snapshot.impactActive = true;
        impactStartedMs = sample.timestampMs;
        impactLastAboveMs = sample.timestampMs;
        impactPeakMs = sample.timestampMs;
        activeImpactPeakG = snapshot.linearAccelerationG;
        impactBackgroundRmsG = backgroundRmsG;
        snapshot.impactCount++;
        snapshot.lastImpactMs = sample.timestampMs;
    }

    if (snapshot.impactActive) {
        if (snapshot.linearAccelerationG >= config.impactEndThresholdG) {
            impactLastAboveMs = sample.timestampMs;
        }
        if (snapshot.linearAccelerationG >= activeImpactPeakG) {
            activeImpactPeakG = snapshot.linearAccelerationG;
            impactPeakMs = sample.timestampMs;
        }
        if (sample.timestampMs - impactLastAboveMs >= config.impactSettleMs) {
            finishImpact();
        }
    }

    float quietTarget = 1.0f;
    if (snapshot.vibrationRmsG > config.quietRmsThresholdG) {
        const float span = config.activeRmsThresholdG - config.quietRmsThresholdG;
        quietTarget = span > 0.0f
            ? 1.0f - clamp01((snapshot.vibrationRmsG - config.quietRmsThresholdG) / span)
            : 0.0f;
    }
    if (snapshot.impactActive) {
        quietTarget = 0.0f;
    }
    const float quietTau = quietTarget < snapshot.quietConfidence
        ? config.quietFallTimeConstantMs
        : config.quietRiseTimeConstantMs;
    snapshot.quietConfidence += smoothingAlpha(boundedElapsedMs, quietTau) *
                                (quietTarget - snapshot.quietConfidence);
    snapshot.quietConfidence = clamp01(snapshot.quietConfidence);

    snapshot.gravityMagnitudeG = sqrtf(snapshot.gravityXG * snapshot.gravityXG +
                                       snapshot.gravityYG * snapshot.gravityYG +
                                       snapshot.gravityZG * snapshot.gravityZG);
    snapshot.rollDegrees = atan2f(snapshot.gravityYG, snapshot.gravityZG) * RADIANS_TO_DEGREES;
    snapshot.pitchDegrees = atan2f(-snapshot.gravityXG,
                                   sqrtf(snapshot.gravityYG * snapshot.gravityYG +
                                         snapshot.gravityZG * snapshot.gravityZG)) *
                            RADIANS_TO_DEGREES;

    if (snapshot.impactActive) {
        snapshot.state = State::Impact;
    } else if (snapshot.quietConfidence >= 0.75f) {
        snapshot.state = State::Quiet;
    } else {
        snapshot.state = State::Active;
    }

    snapshot.sampleCount++;
    snapshot.lastSampleMs = sample.timestampMs;
    return true;
}

const char* MotionAnalyzer::stateName(State state) {
    switch (state) {
        case State::Quiet:
            return "quiet";
        case State::Active:
            return "active";
        case State::Impact:
            return "impact";
        default:
            return "uninitialized";
    }
}

float MotionAnalyzer::clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float MotionAnalyzer::smoothingAlpha(float elapsedMs, float timeConstantMs) {
    if (timeConstantMs <= 0.0f) {
        return 1.0f;
    }
    return 1.0f - expf(-elapsedMs / timeConstantMs);
}

void MotionAnalyzer::finishImpact() {
    const uint32_t durationMs = impactLastAboveMs - impactStartedMs;
    const bool shortEnough = durationMs <= config.maximumTapDurationMs;
    const bool quietBackground = impactBackgroundRmsG <= config.maximumTapBackgroundRmsG;

    snapshot.impactActive = false;
    snapshot.lastImpactDurationMs = durationMs;
    snapshot.lastImpactPeakG = activeImpactPeakG;
    snapshot.lastImpactBackgroundRmsG = impactBackgroundRmsG;
    snapshot.lastImpactWasTapCandidate = shortEnough && quietBackground;
    if (!shortEnough || !quietBackground) {
        return;
    }

    snapshot.tapCandidate = true;
    snapshot.tapCandidateCount++;
    snapshot.lastTapCandidateMs = impactPeakMs;

    if (previousTapCandidateMs != 0) {
        const uint32_t separationMs = impactPeakMs - previousTapCandidateMs;
        if (separationMs >= config.minimumDoubleTapSeparationMs &&
            separationMs <= config.maximumDoubleTapSeparationMs) {
            snapshot.doubleTapCandidate = true;
            snapshot.doubleTapCandidateCount++;
        }
    }
    previousTapCandidateMs = impactPeakMs;
}

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
