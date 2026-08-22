#include "BatteryDrainSession.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "BatteryTimeEstimator.h"

namespace {

constexpr float kBatteryFull = 4.20f;
constexpr float kBatteryGood = 4.00f;
constexpr float kBatteryNominal = 3.80f;
constexpr float kBatteryLow = 3.60f;
constexpr float kBatteryCritical = 3.40f;
constexpr float kBatteryEmpty = 3.20f;

constexpr float kDefaultCriticalShutdownVoltage = 3.45f;
constexpr int kDefaultCriticalShutdownPercent = 7;
constexpr float kCriticalRecoveryMarginVoltage = 0.10f;
constexpr int kCriticalRecoveryMarginPercent = 3;
constexpr int kVisiblePercentStep = 5;
constexpr float kDefaultBatteryCapacityMah = 700.0f;
constexpr float kLargeBatteryCapacityMah = 1000.0f;

struct BoardEstimate {
    const char* name;
    float activeBaseMa;
    float wifiDeltaMa;
    float sps80DeltaMa;
    float sleepMa;
    float hx711Ma;
    float chargeCurrentMa;
};

constexpr BoardEstimate kXiao{
    "xiao-esp32s3",
    42.0f,
    45.0f,
    3.0f,
    0.22f,
    1.5f,
    100.0f,
};

constexpr BoardEstimate kTinyS3D{
    "tinys3d",
    36.0f,
    42.0f,
    3.0f,
    0.10f,
    1.5f,
    300.0f,
};

static bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

static int clampInt(int value, int lower, int upper) {
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

static int voltageToPercentage(float voltage) {
    int percentage = 0;

    if (voltage >= kBatteryFull) {
        percentage = 100;
    } else if (voltage >= kBatteryGood) {
        percentage = 75 + static_cast<int>(((voltage - kBatteryGood) / (kBatteryFull - kBatteryGood)) * 25.0f);
    } else if (voltage >= kBatteryNominal) {
        percentage = 50 + static_cast<int>(((voltage - kBatteryNominal) / (kBatteryGood - kBatteryNominal)) * 25.0f);
    } else if (voltage >= kBatteryLow) {
        percentage = 25 + static_cast<int>(((voltage - kBatteryLow) / (kBatteryNominal - kBatteryLow)) * 25.0f);
    } else if (voltage >= kBatteryCritical) {
        percentage = 5 + static_cast<int>(((voltage - kBatteryCritical) / (kBatteryLow - kBatteryCritical)) * 20.0f);
    } else if (voltage >= kBatteryEmpty) {
        percentage = static_cast<int>(((voltage - kBatteryEmpty) / (kBatteryCritical - kBatteryEmpty)) * 5.0f);
    } else {
        percentage = 0;
    }

    return clampInt(percentage, 0, 100);
}

static int quantizeVisiblePercentage(int percentage) {
    const int clamped = clampInt(percentage, 0, 100);
    return clampInt(((clamped + (kVisiblePercentStep / 2)) / kVisiblePercentStep) * kVisiblePercentStep, 0, 100);
}

static bool shouldForceCriticalSleep(float voltage,
                                     int fuelGaugePercent,
                                     bool hasReading,
                                     bool usbPowerPresent,
                                     bool fuelGaugeAvailable) {
    if (!hasReading || usbPowerPresent || voltage <= 0.1f) {
        return false;
    }
    if (fuelGaugeAvailable && fuelGaugePercent >= 0 && fuelGaugePercent <= kDefaultCriticalShutdownPercent) {
        return true;
    }
    return voltage <= kDefaultCriticalShutdownVoltage;
}

static bool hasRecoveredFromCriticalSleep(float voltage,
                                          int fuelGaugePercent,
                                          bool hasReading,
                                          bool usbPowerPresent,
                                          bool usbOnlyPower,
                                          bool fuelGaugeAvailable) {
    if (usbPowerPresent || usbOnlyPower) {
        return true;
    }
    if (!hasReading) {
        return false;
    }
    if (fuelGaugeAvailable &&
        fuelGaugePercent < (kDefaultCriticalShutdownPercent + kCriticalRecoveryMarginPercent)) {
        return false;
    }
    return voltage >= (kDefaultCriticalShutdownVoltage + kCriticalRecoveryMarginVoltage);
}

static float activeCurrentMa(const BoardEstimate& board, bool wifiOn, bool sps80) {
    return board.activeBaseMa + (wifiOn ? board.wifiDeltaMa : 0.0f) + (sps80 ? board.sps80DeltaMa : 0.0f);
}

static float sleepCurrentMa(const BoardEstimate& board, bool hx711Powered) {
    return board.sleepMa + (hx711Powered ? board.hx711Ma : 0.0f);
}

static float runtimeHours(float capacityMah, float currentMa) {
    assert(currentMa > 0.0f);
    return capacityMah / currentMa;
}

static float percentPerHour(float capacityMah, float currentMa) {
    assert(capacityMah > 0.0f);
    return (currentMa / capacityMah) * 100.0f;
}

static float minutesToCharge(float capacityMah,
                             float startPercent,
                             float targetPercent,
                             float chargerMa,
                             float efficiency,
                             float loadMa) {
    assert(capacityMah > 0.0f);
    assert(chargerMa > 0.0f);
    assert(efficiency > 0.0f);
    assert(efficiency <= 1.0f);
    const float netMa = (chargerMa * efficiency) - loadMa;
    assert(netMa > 0.0f);
    const float neededMah = capacityMah * ((targetPercent - startPercent) / 100.0f);
    return (neededMah / netMa) * 60.0f;
}

static void testVoltageCurveAndVisibleAdcSteps() {
    assert(voltageToPercentage(4.25f) == 100);
    assert(voltageToPercentage(4.00f) == 75);
    assert(voltageToPercentage(3.80f) == 50);
    assert(voltageToPercentage(3.60f) == 25);
    assert(voltageToPercentage(3.40f) == 5);
    assert(voltageToPercentage(3.20f) == 0);

    for (int percentage = 0; percentage <= 100; ++percentage) {
        const int visible = quantizeVisiblePercentage(percentage);
        assert(visible >= 0);
        assert(visible <= 100);
        assert((visible % kVisiblePercentStep) == 0);
    }
}

static void testCriticalSleepAndRecoveryHysteresis() {
    assert(shouldForceCriticalSleep(3.44f, -1, true, false, false));
    assert(shouldForceCriticalSleep(3.70f, 5, true, false, true));
    assert(!shouldForceCriticalSleep(3.44f, -1, true, true, false));
    assert(!shouldForceCriticalSleep(3.90f, -1, false, false, false));

    assert(!hasRecoveredFromCriticalSleep(3.50f, -1, true, false, false, false));
    assert(hasRecoveredFromCriticalSleep(3.56f, -1, true, false, false, false));
    assert(!hasRecoveredFromCriticalSleep(3.70f, 9, true, false, false, true));
    assert(hasRecoveredFromCriticalSleep(3.70f, 10, true, false, false, true));
    assert(hasRecoveredFromCriticalSleep(3.30f, 1, true, true, false, true));
    assert(hasRecoveredFromCriticalSleep(0.0f, -1, false, false, true, false));
}

static void testBoardRuntimeMatrix() {
    const float xiaoBase = activeCurrentMa(kXiao, false, false);
    const float xiao80 = activeCurrentMa(kXiao, false, true);
    const float xiaoWifi80 = activeCurrentMa(kXiao, true, true);

    assert(near(xiaoBase, 42.0f, 0.001f));
    assert(xiao80 > xiaoBase);
    assert(xiao80 < xiaoBase * 1.10f);
    assert(xiaoWifi80 > xiaoBase * 2.0f);

    assert(near(runtimeHours(kDefaultBatteryCapacityMah, xiaoBase), 16.67f, 0.05f));
    assert(near(runtimeHours(kLargeBatteryCapacityMah, xiaoBase), 23.81f, 0.05f));
    assert(near(percentPerHour(kDefaultBatteryCapacityMah, xiaoBase), 6.0f, 0.01f));

    const float xiaoSleepHxOn = sleepCurrentMa(kXiao, true);
    const float xiaoSleepHxOff = sleepCurrentMa(kXiao, false);
    assert(xiaoSleepHxOn > xiaoSleepHxOff * 7.0f);
    assert(runtimeHours(kDefaultBatteryCapacityMah, xiaoSleepHxOff) > 3000.0f);
    assert(runtimeHours(kDefaultBatteryCapacityMah, xiaoSleepHxOn) < 500.0f);

    const float tinyBase = activeCurrentMa(kTinyS3D, false, false);
    assert(tinyBase < xiaoBase);
    assert(runtimeHours(kDefaultBatteryCapacityMah, tinyBase) > runtimeHours(kDefaultBatteryCapacityMah, xiaoBase));
}

static void testSleepChargeEstimates() {
    const float efficiency = 0.85f;
    const float startPercent = 20.0f;

    const float xiaoTo80 = minutesToCharge(kDefaultBatteryCapacityMah,
                                           startPercent,
                                           80.0f,
                                           kXiao.chargeCurrentMa,
                                           efficiency,
                                           sleepCurrentMa(kXiao, true));
    const float xiaoLargeTo80 = minutesToCharge(kLargeBatteryCapacityMah,
                                                startPercent,
                                                80.0f,
                                                kXiao.chargeCurrentMa,
                                                efficiency,
                                                sleepCurrentMa(kXiao, true));
    const float tinyTo80 = minutesToCharge(kDefaultBatteryCapacityMah,
                                           startPercent,
                                           80.0f,
                                           kTinyS3D.chargeCurrentMa,
                                           efficiency,
                                           sleepCurrentMa(kTinyS3D, true));

    assert(xiaoTo80 > 290.0f);
    assert(xiaoTo80 < 315.0f);
    assert(xiaoLargeTo80 > xiaoTo80 * 1.40f);
    assert(xiaoLargeTo80 < xiaoTo80 * 1.45f);
    assert(tinyTo80 < xiaoTo80 * 0.40f);
}

static void testTinyS3FuelGaugeTimeEstimates() {
    using namespace BatteryTimeEstimator;

    assert(near(activeLoadMa(false), 39.0f, 0.001f));
    assert(near(activeLoadMa(true), 81.0f, 0.001f));
    assert(near(netChargeCurrentMa(false), 216.0f, 0.001f));
    assert(near(netChargeCurrentMa(true), 174.0f, 0.001f));

    const float runtimeWifiOff = runtimeMinutesFromCurrent(700.0f, 40.0f, activeLoadMa(false));
    const float runtimeWifiOn = runtimeMinutesFromCurrent(700.0f, 40.0f, activeLoadMa(true));
    assert(near(runtimeWifiOff, 430.77f, 0.01f));
    assert(near(runtimeWifiOn, 207.41f, 0.01f));

    assert(near(minutesToTargetFromCurrent(700.0f, 40.0f, 80.0f,
                                          netChargeCurrentMa(false)),
                77.78f, 0.01f));
    assert(near(minutesToTargetFromCurrent(700.0f, 40.0f, 100.0f,
                                          netChargeCurrentMa(true)),
                144.83f, 0.01f));

    assert(near(runtimeMinutesFromGaugeRate(40.0f, -8.0f), 300.0f, 0.001f));
    assert(near(minutesToTargetFromGaugeRate(40.0f, 80.0f, 20.0f), 120.0f, 0.001f));
    assert(minutesToTargetFromGaugeRate(85.0f, 80.0f, 20.0f) == 0.0f);
    assert(runtimeMinutesFromGaugeRate(40.0f, -0.2f) < 0.0f);
    assert(minutesToTargetFromGaugeRate(40.0f, 80.0f, -8.0f) < 0.0f);
}

static void testDrainSessionTrendExamples() {
    BatteryDrainSession drain;
    drain.begin(0, 4.000f, 75, 75, true, "runtime-xiao-80sps");
    drain.update(60UL * 60UL * 1000UL, 3.940f, 70, 70, true);
    assert(std::strcmp(drain.getTrend(), "draining") == 0);
    assert(std::strcmp(drain.getConfidence(), "medium") == 0);

    BatteryDrainSession charge;
    charge.begin(0, 3.700f, 35, 35, true, "usb-charge-xiao");
    charge.update(35UL * 60UL * 1000UL, 3.760f, 40, 40, true);
    assert(std::strcmp(charge.getTrend(), "charging") == 0);
    assert(std::strcmp(charge.getConfidence(), "medium") == 0);

    BatteryDrainSession flat;
    flat.begin(0, 3.900f, 60, 60, true, "sleep-hx711-off");
    flat.update(10UL * 60UL * 1000UL, 3.901f, 60, 60, true);
    assert(std::strcmp(flat.getTrend(), "flat") == 0);
    assert(std::strcmp(flat.getConfidence(), "low") == 0);
}

} // namespace

int main() {
    testVoltageCurveAndVisibleAdcSteps();
    testCriticalSleepAndRecoveryHysteresis();
    testBoardRuntimeMatrix();
    testSleepChargeEstimates();
    testTinyS3FuelGaugeTimeEstimates();
    testDrainSessionTrendExamples();

    std::cout << "Battery simulation matrix host tests passed\n";
    return 0;
}
