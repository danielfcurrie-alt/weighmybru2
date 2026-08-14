#include "BatteryDrainSession.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

static bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

static void testDischargeSlope() {
    BatteryDrainSession session;
    session.begin(0, 4.000f, 75, 76, true, "wifi-off");
    session.update(30UL * 60UL * 1000UL, 3.900f, 65, 66, true);

    assert(std::strcmp(session.getLabel(), "wifi-off") == 0);
    assert(near(session.getElapsedMinutes(), 30.0f, 0.001f));
    assert(near(session.getDeltaVoltage(), -0.100f, 0.0001f));
    assert(session.getDeltaPercent() == -10);
    assert(session.getDeltaRawPercent() == -10);
    assert(near(session.getVoltageMillivoltsPerHour(), -200.0f, 0.01f));
    assert(near(session.getRawPercentPerHour(), -20.0f, 0.01f));
    assert(std::strcmp(session.getTrend(), "draining") == 0);
    assert(std::strcmp(session.getConfidence(), "medium") == 0);
}

static void testChargeSlope() {
    BatteryDrainSession session;
    session.begin(1000, 3.800f, 50, 51, true, "usb-charge");
    session.update(16UL * 60UL * 1000UL + 1000UL, 3.860f, 55, 56, true);

    assert(near(session.getElapsedMinutes(), 16.0f, 0.001f));
    assert(near(session.getVoltageMillivoltsPerHour(), 225.0f, 0.01f));
    assert(near(session.getRawPercentPerHour(), 18.75f, 0.01f));
    assert(std::strcmp(session.getTrend(), "charging") == 0);
    assert(std::strcmp(session.getConfidence(), "low") == 0);
}

static void testInvalidStartRecovers() {
    BatteryDrainSession session;
    session.begin(0, NAN, 0, 0, false, "boot");
    assert(!session.hasValidStart());
    assert(session.getInvalidSamples() == 1);

    session.update(5000, 4.100f, 85, 86, true);
    assert(session.hasValidStart());
    assert(session.hasValidLast());
    assert(session.getSamples() == 1);
    assert(near(session.getStartVoltage(), 4.100f, 0.0001f));
    assert(session.getStartRawPercent() == 86);
}

static void testResetAndLabelClamp() {
    BatteryDrainSession session;
    session.begin(0, 4.2f, 105, -10, true, "this-label-is-intentionally-far-too-long-for-storage");
    assert(session.getStartPercent() == 100);
    assert(session.getStartRawPercent() == 0);
    assert(std::strlen(session.getLabel()) < 32);

    session.reset(100, 3.7f, 40, 41, true, "oled-on");
    assert(std::strcmp(session.getLabel(), "oled-on") == 0);
    assert(session.getSamples() == 1);
    assert(session.getInvalidSamples() == 0);
    assert(near(session.getStartVoltage(), 3.7f, 0.0001f));
}

int main() {
    testDischargeSlope();
    testChargeSlope();
    testInvalidStartRecovers();
    testResetAndLabelClamp();

    std::cout << "BatteryDrainSession host tests passed\n";
    return 0;
}
