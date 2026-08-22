#include "MotionAnalyzer.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
constexpr float PI = 3.14159265358979323846f;

static bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

static void feed(MotionAnalyzer& analyzer, uint32_t startMs, uint32_t durationMs,
                 float (*axis)(uint32_t), float baseZ = 1.0f) {
    for (uint32_t elapsed = 0; elapsed <= durationMs; elapsed += 10) {
        const float motion = axis(elapsed);
        assert(analyzer.update({motion, 0.0f, baseZ, startMs + elapsed}));
    }
}

static float quietPattern(uint32_t elapsedMs) {
    return 0.003f * std::sin(2.0f * PI * 1.7f * static_cast<float>(elapsedMs) / 1000.0f);
}

static float vibrationPattern(uint32_t elapsedMs) {
    return 0.14f * std::sin(2.0f * PI * 19.0f * static_cast<float>(elapsedMs) / 1000.0f);
}

static float cupPlacementPattern(uint32_t elapsedMs) {
    if (elapsedMs < 300 || elapsedMs > 850) {
        return 0.0f;
    }
    const float ageMs = static_cast<float>(elapsedMs - 300);
    return 0.75f * std::exp(-ageMs / 230.0f) *
           std::sin(2.0f * PI * 11.0f * ageMs / 1000.0f);
}

static void settle(MotionAnalyzer& analyzer, uint32_t startMs, uint32_t durationMs) {
    feed(analyzer, startMs, durationMs, quietPattern);
}

static void testQuietAndOrientation() {
    MotionAnalyzer analyzer;
    feed(analyzer, 0, 2500, quietPattern);
    const auto quiet = analyzer.diagnostics();
    assert(quiet.state == MotionAnalyzer::State::Quiet);
    assert(quiet.quietConfidence > 0.90f);
    assert(quiet.vibrationRmsG < 0.01f);
    assert(near(quiet.gravityMagnitudeG, 1.0f, 0.01f));

    const float tilt = 30.0f * PI / 180.0f;
    uint32_t now = 2510;
    for (; now < 7000; now += 10) {
        assert(analyzer.update({-std::sin(tilt), 0.0f, std::cos(tilt), now}));
    }
    const auto oriented = analyzer.diagnostics();
    assert(near(oriented.pitchDegrees, 30.0f, 1.5f));
    assert(near(oriented.rollDegrees, 0.0f, 1.0f));
}

static void testSustainedVibration() {
    MotionAnalyzer analyzer;
    settle(analyzer, 0, 1000);
    feed(analyzer, 1010, 1500, vibrationPattern);
    const auto active = analyzer.diagnostics();
    assert(active.state == MotionAnalyzer::State::Active);
    assert(active.vibrationRmsG > 0.07f);
    assert(active.quietConfidence < 0.35f);
    assert(active.doubleTapCandidateCount == 0);
}

static void emitTap(MotionAnalyzer& analyzer, uint32_t atMs) {
    assert(analyzer.update({0.65f, 0.0f, 1.0f, atMs}));
    assert(analyzer.update({0.0f, 0.0f, 1.0f, atMs + 10}));
    for (uint32_t now = atMs + 20; now <= atMs + 90; now += 10) {
        assert(analyzer.update({0.0f, 0.0f, 1.0f, now}));
    }
}

static void testKnockAndDoubleTapCandidates() {
    MotionAnalyzer analyzer;
    settle(analyzer, 0, 1000);
    emitTap(analyzer, 1100);
    assert(analyzer.diagnostics().tapCandidateCount == 1);
    assert(analyzer.diagnostics().doubleTapCandidateCount == 0);
    assert(analyzer.diagnostics().impactPeakG > 0.50f);

    emitTap(analyzer, 1350);
    const auto doubleTap = analyzer.diagnostics();
    if (doubleTap.tapCandidateCount != 2) {
        std::cerr << "tap candidates=" << doubleTap.tapCandidateCount
                  << " impacts=" << doubleTap.impactCount
                  << " durationMs=" << doubleTap.lastImpactDurationMs
                  << " backgroundRms=" << doubleTap.lastImpactBackgroundRmsG
                  << " accepted=" << doubleTap.lastImpactWasTapCandidate
                  << " active=" << doubleTap.impactActive
                  << " linear=" << doubleTap.linearAccelerationG << "\n";
    }
    assert(doubleTap.tapCandidateCount == 2);
    assert(doubleTap.doubleTapCandidateCount == 1);
}

static void testCupPlacementIsNotTap() {
    MotionAnalyzer analyzer;
    settle(analyzer, 0, 1000);
    feed(analyzer, 1010, 1100, cupPlacementPattern);
    settle(analyzer, 2120, 1000);
    const auto placement = analyzer.diagnostics();
    assert(placement.impactCount >= 1);
    assert(placement.lastImpactPeakG > 0.30f);
    assert(placement.tapCandidateCount == 0);
    assert(placement.doubleTapCandidateCount == 0);
    assert(placement.quietConfidence > 0.75f);
}

static void testInvalidSamplesAreRejected() {
    MotionAnalyzer analyzer;
    assert(!analyzer.update({NAN, 0.0f, 1.0f, 0}));
    assert(!analyzer.diagnostics().initialized);
}
}

int main() {
    testQuietAndOrientation();
    testSustainedVibration();
    testKnockAndDoubleTapCandidates();
    testCupPlacementIsNotTap();
    testInvalidSamplesAreRejected();
    std::cout << "MotionAnalyzer host tests passed\n";
    return 0;
}
