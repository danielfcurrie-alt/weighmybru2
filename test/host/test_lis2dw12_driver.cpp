#include "LIS2DW12Driver.h"
#include "SampleCadenceTracker.h"

#include <cassert>
#include <cmath>
#include <iostream>

TwoWire Wire;

namespace {
static bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

static void setAxis(TwoWire& bus, uint8_t lowRegister, float valueG,
                    LIS2DW12Driver::FullScale fullScale) {
    static constexpr float MG_PER_LSB[] = {0.244f, 0.488f, 0.976f, 1.952f};
    const float mgPerLsb = MG_PER_LSB[static_cast<uint8_t>(fullScale)];
    const int16_t sample14 = static_cast<int16_t>(std::lround(valueG * 1000.0f / mgPerLsb));
    const int16_t raw = static_cast<int16_t>(sample14 * 4);
    bus.setRegister(lowRegister, static_cast<uint8_t>(raw & 0xff));
    bus.setRegister(lowRegister + 1, static_cast<uint8_t>((raw >> 8) & 0xff));
}

static void testIdentityResetAndConfiguration() {
    ArduinoFake::reset();
    TwoWire bus;
    bus.setRegister(0x30, 0xa5);
    LIS2DW12Driver driver(bus);
    LIS2DW12Driver::Config config;
    config.outputDataRate = LIS2DW12Driver::OutputDataRate::Hz100;
    config.fullScale = LIS2DW12Driver::FullScale::G4;
    config.interruptPin = 17;
    config.routeDataReadyToInterrupt = true;
    config.lowNoise = true;

    assert(driver.begin(config));
    assert(driver.isConnected());
    assert(driver.deviceId() == LIS2DW12Driver::EXPECTED_DEVICE_ID);
    assert(bus.resetCount == 1);
    assert(bus.registerValue(0x30) == 0x00);
    assert(bus.registerValue(0x20) == 0x54);
    assert(bus.registerValue(0x21) == 0x0c);
    assert(bus.registerValue(0x23) == 0x01);
    assert(bus.registerValue(0x25) == 0x14);
    assert(ArduinoFake::pinModes[17] == INPUT);

    TwoWire wrongIdentity;
    wrongIdentity.setRegister(0x0f, 0x00);
    LIS2DW12Driver missing(wrongIdentity);
    assert(!missing.begin());
    assert(!missing.isConnected());
}

static void testScalingAcrossRanges() {
    static constexpr LIS2DW12Driver::FullScale RANGES[] = {
        LIS2DW12Driver::FullScale::G2,
        LIS2DW12Driver::FullScale::G4,
        LIS2DW12Driver::FullScale::G8,
        LIS2DW12Driver::FullScale::G16,
    };

    for (const auto range : RANGES) {
        ArduinoFake::reset();
        TwoWire bus;
        LIS2DW12Driver driver(bus);
        LIS2DW12Driver::Config config;
        config.fullScale = range;
        assert(driver.begin(config));
        setAxis(bus, 0x28, 0.75f, range);
        setAxis(bus, 0x2a, -0.50f, range);
        setAxis(bus, 0x2c, 1.00f, range);
        ArduinoFake::nowMs = 1234;

        LIS2DW12Driver::Sample sample;
        assert(driver.readAcceleration(sample));
        assert(near(sample.xG, 0.75f, 0.003f));
        assert(near(sample.yG, -0.50f, 0.003f));
        assert(near(sample.zG, 1.00f, 0.003f));
        assert(sample.timestampMs == 1234);
        assert(driver.successfulReads() == 1);
        assert(driver.lastSampleMillis() == 1234);
    }
}

static void testDataReadyStatusAndInterrupt() {
    ArduinoFake::reset();
    TwoWire bus;
    LIS2DW12Driver driver(bus);
    assert(driver.begin());
    bus.setRegister(0x27, 0x00);
    assert(!driver.dataReady());
    bus.setRegister(0x27, 0x01);
    assert(driver.dataReady());

    LIS2DW12Driver::Config config;
    config.interruptPin = 9;
    config.routeDataReadyToInterrupt = true;
    assert(driver.configure(config));
    ArduinoFake::pinValues[9] = LOW;
    assert(!driver.dataReady());
    ArduinoFake::pinValues[9] = HIGH;
    assert(driver.dataReady());
}

static void testCommunicationErrorsAreCounted() {
    ArduinoFake::reset();
    TwoWire bus;
    LIS2DW12Driver driver(bus);
    assert(driver.begin());
    const uint32_t errorsBefore = driver.communicationErrors();
    bus.failRequestOnce();
    LIS2DW12Driver::Sample sample;
    assert(!driver.readAcceleration(sample));
    assert(driver.communicationErrors() == errorsBefore + 1);
    assert(driver.successfulReads() == 0);
}

static void testHx711CadenceWithAccelerometerTraffic() {
    ArduinoFake::reset();
    TwoWire bus;
    LIS2DW12Driver driver(bus);
    assert(driver.begin());
    setAxis(bus, 0x28, 0.05f, LIS2DW12Driver::FullScale::G4);
    setAxis(bus, 0x2a, -0.02f, LIS2DW12Driver::FullScale::G4);
    setAxis(bus, 0x2c, 1.00f, LIS2DW12Driver::FullScale::G4);

    SampleCadenceTracker hx711Cadence;
    uint32_t nextHx711Us = 1000;
    uint32_t nextAccelerometerUs = 1000;
    uint32_t hx711Samples = 0;
    uint32_t accelerometerSamples = 0;
    constexpr uint32_t END_US = 5000000;
    constexpr uint32_t HX711_INTERVAL_US = 10617;
    constexpr uint32_t ACCELEROMETER_INTERVAL_US = 10000;

    while (nextHx711Us <= END_US || nextAccelerometerUs <= END_US) {
        if (nextAccelerometerUs <= nextHx711Us && nextAccelerometerUs <= END_US) {
            ArduinoFake::nowMs = nextAccelerometerUs / 1000;
            LIS2DW12Driver::Sample sample;
            assert(driver.readAcceleration(sample));
            accelerometerSamples++;
            nextAccelerometerUs += ACCELEROMETER_INTERVAL_US;
        } else if (nextHx711Us <= END_US) {
            hx711Cadence.recordSampleMicros(nextHx711Us);
            hx711Samples++;
            nextHx711Us += HX711_INTERVAL_US;
        }
    }

    assert(accelerometerSamples == 500);
    assert(hx711Samples > 470);
    assert(near(hx711Cadence.getRateHz(), 94.1886f, 0.01f));
    assert(hx711Cadence.getLongGapCount() == 0);
    assert(hx711Cadence.getEstimatedLostCadenceSlots() == 0);
    assert(driver.successfulReads() == accelerometerSamples);
    assert(bus.requestCount >= accelerometerSamples);
}
}

int main() {
    testIdentityResetAndConfiguration();
    testScalingAcrossRanges();
    testDataReadyStatusAndInterrupt();
    testCommunicationErrorsAreCounted();
    testHx711CadenceWithAccelerometerTraffic();
    std::cout << "LIS2DW12 driver host tests passed\n";
    return 0;
}
