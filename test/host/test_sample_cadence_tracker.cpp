#include "SampleCadenceTracker.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

static bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

static void test80SpsDetection() {
    SampleCadenceTracker tracker;
    for (int i = 0; i < 20; ++i) {
        tracker.recordSampleMicros(1000UL + static_cast<uint32_t>(i) * 12500UL);
    }

    assert(tracker.getStatsCount() == 19);
    assert(tracker.getAverageMicros() == 12500);
    assert(near(tracker.getRateHz(), 80.0f, 0.01f));
    assert(std::strcmp(tracker.getRateMode(), "80SPS") == 0);
    assert(tracker.getRoundedRateHz() == 80);
    assert(tracker.getLongGapCount() == 0);
}

static void test10SpsDetection() {
    SampleCadenceTracker tracker;
    for (int i = 0; i < 12; ++i) {
        tracker.recordSampleMicros(5000UL + static_cast<uint32_t>(i) * 100000UL);
    }

    assert(tracker.getStatsCount() == 11);
    assert(tracker.getAverageMicros() == 100000);
    assert(near(tracker.getRateHz(), 10.0f, 0.01f));
    assert(std::strcmp(tracker.getRateMode(), "10SPS") == 0);
    assert(tracker.getRoundedRateHz() == 10);
    assert(tracker.getLongGapCount() == 0);
}

static void testLongGapUsesEstablishedCadence() {
    SampleCadenceTracker tracker;
    tracker.recordSampleMicros(1000);
    tracker.recordSampleMicros(13500);
    tracker.recordSampleMicros(26000);
    tracker.recordSampleMicros(38500);
    tracker.recordSampleMicros(51000);

    const bool longGap = tracker.recordSampleMicros(851000);

    assert(longGap);
    assert(tracker.getLongGapCount() == 1);
    assert(tracker.getMaxMicros() == 800000);
}

static void testReset() {
    SampleCadenceTracker tracker;
    tracker.recordSampleMicros(1000);
    tracker.recordSampleMicros(13500);
    assert(tracker.getStatsCount() == 1);

    tracker.reset();
    assert(tracker.getStatsCount() == 0);
    assert(tracker.getAverageMicros() == 0);
    assert(std::strcmp(tracker.getRateMode(), "UNKNOWN") == 0);
}

int main() {
    test80SpsDetection();
    test10SpsDetection();
    testLongGapUsesEstablishedCadence();
    testReset();

    std::cout << "SampleCadenceTracker host tests passed\n";
    return 0;
}
