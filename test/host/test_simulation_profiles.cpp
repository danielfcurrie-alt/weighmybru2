#include "SimulationProfiles.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

static bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

static void testSampleIntervals() {
    assert(SimulationProfiles::sampleIntervalMicros(80) == 12500);
    assert(SimulationProfiles::sampleIntervalMicros(10) == 100000);
}

static void testScenarioNames() {
    assert(std::strcmp(SimulationProfiles::scenarioName(WMBP_SIM_SCENARIO_SHOT), "shot") == 0);
    assert(std::strcmp(SimulationProfiles::scenarioName(WMBP_SIM_SCENARIO_GLITCH), "glitch") == 0);
    assert(std::strcmp(SimulationProfiles::scenarioName(WMBP_SIM_SCENARIO_BUMP), "bump") == 0);
}

static void testShotShape() {
    const float beforePour = SimulationProfiles::weightForScenario(2000, WMBP_SIM_SCENARIO_SHOT, 80);
    const float midPour = SimulationProfiles::weightForScenario(13000, WMBP_SIM_SCENARIO_SHOT, 80);
    const float settled = SimulationProfiles::weightForScenario(23000, WMBP_SIM_SCENARIO_SHOT, 80);

    assert(std::fabs(beforePour) < 0.1f);
    assert(midPour > 20.0f && midPour < 22.0f);
    assert(settled > 37.0f && settled < 38.5f);
}

static void testGlitchAndBumpScenarios() {
    const float normal = SimulationProfiles::weightForScenario(12680, WMBP_SIM_SCENARIO_SHOT, 80);
    const float glitch = SimulationProfiles::weightForScenario(12680, WMBP_SIM_SCENARIO_GLITCH, 80);
    const float normalAtBump = SimulationProfiles::weightForScenario(12005, WMBP_SIM_SCENARIO_SHOT, 80);
    const float bump = SimulationProfiles::weightForScenario(12005, WMBP_SIM_SCENARIO_BUMP, 80);

    assert(normal > 19.0f && normal < 21.0f);
    assert(glitch < -590.0f);
    assert(bump > normalAtBump + 6.0f);
}

static void testBatteryProfiles() {
    const uint32_t oneHour = 3600000UL;
    const float baselineStart = SimulationProfiles::batteryVoltage(0, WMBP_SIM_BATTERY_PROFILE_BASELINE);
    const float baselineHour = SimulationProfiles::batteryVoltage(oneHour, WMBP_SIM_BATTERY_PROFILE_BASELINE);
    const float wifiHour = SimulationProfiles::batteryVoltage(oneHour, WMBP_SIM_BATTERY_PROFILE_WIFI_AP);
    const float chargeHour = SimulationProfiles::batteryVoltage(oneHour, WMBP_SIM_BATTERY_PROFILE_CHARGING);
    const float slowChargeHour = SimulationProfiles::batteryVoltage(oneHour, WMBP_SIM_BATTERY_PROFILE_SLOW_CHARGE);

    assert(near(baselineStart, 3.950f, 0.001f));
    assert(baselineHour < baselineStart);
    assert(wifiHour < baselineHour);
    assert(chargeHour > baselineStart);
    assert(slowChargeHour > baselineStart);
    assert(slowChargeHour < chargeHour);
    assert(!SimulationProfiles::simulatedUsbPowerPresent(WMBP_SIM_BATTERY_PROFILE_BASELINE));
    assert(SimulationProfiles::simulatedUsbPowerPresent(WMBP_SIM_BATTERY_PROFILE_CHARGING));
}

int main() {
    testSampleIntervals();
    testScenarioNames();
    testShotShape();
    testGlitchAndBumpScenarios();
    testBatteryProfiles();

    std::cout << "SimulationProfiles host tests passed\n";
    return 0;
}
