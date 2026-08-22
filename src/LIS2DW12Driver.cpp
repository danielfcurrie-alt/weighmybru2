#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include "LIS2DW12Driver.h"

namespace {
constexpr uint8_t REG_OUT_T_H = 0x0E;
constexpr uint8_t REG_WHO_AM_I = 0x0F;
constexpr uint8_t REG_CTRL1 = 0x20;
constexpr uint8_t REG_CTRL2 = 0x21;
constexpr uint8_t REG_CTRL4_INT1_PAD_CTRL = 0x23;
constexpr uint8_t REG_CTRL6 = 0x25;
constexpr uint8_t REG_STATUS = 0x27;
constexpr uint8_t REG_OUT_X_L = 0x28;

constexpr uint8_t CTRL2_SOFT_RESET = 0x40;
constexpr uint8_t CTRL2_BLOCK_DATA_UPDATE = 0x08;
constexpr uint8_t CTRL2_AUTO_INCREMENT = 0x04;
constexpr uint8_t CTRL4_INT1_DATA_READY = 0x01;
constexpr uint8_t STATUS_DATA_READY = 0x01;
constexpr uint8_t CTRL6_LOW_NOISE = 0x04;
constexpr uint32_t RESET_TIMEOUT_MS = 100;
}

LIS2DW12Driver::LIS2DW12Driver(TwoWire& wire, uint8_t address)
    : wire(wire), address(address), connected(false), detectedDeviceId(0),
      readCount(0), errorCount(0), lastSampleMs(0) {}

bool LIS2DW12Driver::begin() {
    return begin(Config{});
}

bool LIS2DW12Driver::begin(const Config& newConfig) {
    connected = false;
    detectedDeviceId = 0;

    if (!readRegister(REG_WHO_AM_I, detectedDeviceId) ||
        detectedDeviceId != EXPECTED_DEVICE_ID) {
        return false;
    }

    if (!softReset() || !configure(newConfig)) {
        return false;
    }

    connected = true;
    return true;
}

bool LIS2DW12Driver::configure(const Config& newConfig) {
    config = newConfig;

    // High-performance mode (MODE=01) gives 14-bit output. LP_MODE is ignored.
    const uint8_t ctrl1 = (static_cast<uint8_t>(config.outputDataRate) << 4) | 0x04;
    const uint8_t ctrl2 = CTRL2_BLOCK_DATA_UPDATE | CTRL2_AUTO_INCREMENT;
    const uint8_t ctrl4 = config.routeDataReadyToInterrupt ? CTRL4_INT1_DATA_READY : 0x00;
    const uint8_t ctrl6 = (static_cast<uint8_t>(config.fullScale) << 4) |
                          (config.lowNoise ? CTRL6_LOW_NOISE : 0x00);

    if (!writeRegister(REG_CTRL2, ctrl2) ||
        !writeRegister(REG_CTRL6, ctrl6) ||
        !writeRegister(REG_CTRL4_INT1_PAD_CTRL, ctrl4) ||
        !writeRegister(REG_CTRL1, ctrl1)) {
        return false;
    }

    if (config.interruptPin >= 0) {
        pinMode(config.interruptPin, INPUT);
    }
    return true;
}

bool LIS2DW12Driver::dataReady() {
    if (config.routeDataReadyToInterrupt && config.interruptPin >= 0) {
        return digitalRead(config.interruptPin) == HIGH;
    }

    uint8_t status = 0;
    return readRegister(REG_STATUS, status) && (status & STATUS_DATA_READY) != 0;
}

bool LIS2DW12Driver::readAcceleration(Sample& sample) {
    uint8_t bytes[6] = {};
    if (!readRegisters(REG_OUT_X_L, bytes, sizeof(bytes))) {
        return false;
    }

    const int16_t x = static_cast<int16_t>((static_cast<uint16_t>(bytes[1]) << 8) | bytes[0]);
    const int16_t y = static_cast<int16_t>((static_cast<uint16_t>(bytes[3]) << 8) | bytes[2]);
    const int16_t z = static_cast<int16_t>((static_cast<uint16_t>(bytes[5]) << 8) | bytes[4]);

    sample.xG = rawToG(x);
    sample.yG = rawToG(y);
    sample.zG = rawToG(z);
    sample.timestampMs = millis();
    lastSampleMs = sample.timestampMs;
    readCount++;
    return true;
}

bool LIS2DW12Driver::readTemperatureC(float& temperatureC) {
    uint8_t raw = 0;
    if (!readRegister(REG_OUT_T_H, raw)) {
        return false;
    }
    temperatureC = 25.0f + static_cast<int8_t>(raw);
    return true;
}

bool LIS2DW12Driver::softReset() {
    if (!writeRegister(REG_CTRL2, CTRL2_SOFT_RESET)) {
        return false;
    }

    const uint32_t startedAt = millis();
    uint8_t ctrl2 = CTRL2_SOFT_RESET;
    while ((ctrl2 & CTRL2_SOFT_RESET) != 0 && millis() - startedAt < RESET_TIMEOUT_MS) {
        delay(1);
        if (!readRegister(REG_CTRL2, ctrl2)) {
            return false;
        }
    }
    return (ctrl2 & CTRL2_SOFT_RESET) == 0;
}

bool LIS2DW12Driver::readRegister(uint8_t reg, uint8_t& value) {
    return readRegisters(reg, &value, 1);
}

bool LIS2DW12Driver::readRegisters(uint8_t reg, uint8_t* values, size_t length) {
    if (values == nullptr || length == 0 || length > 255) {
        errorCount++;
        return false;
    }

    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission(false) != 0) {
        errorCount++;
        return false;
    }

    const size_t received = wire.requestFrom(static_cast<uint16_t>(address), length, true);
    if (received != length) {
        while (wire.available()) {
            wire.read();
        }
        errorCount++;
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        values[i] = static_cast<uint8_t>(wire.read());
    }
    return true;
}

bool LIS2DW12Driver::writeRegister(uint8_t reg, uint8_t value) {
    wire.beginTransmission(address);
    wire.write(reg);
    wire.write(value);
    if (wire.endTransmission(true) != 0) {
        errorCount++;
        return false;
    }
    return true;
}

float LIS2DW12Driver::rawToG(int16_t raw) const {
    static constexpr float MG_PER_LSB[] = {0.244f, 0.488f, 0.976f, 1.952f};
    const uint8_t scaleIndex = static_cast<uint8_t>(config.fullScale);
    const int16_t highPerformanceSample = raw >> 2;
    return highPerformanceSample * MG_PER_LSB[scaleIndex] / 1000.0f;
}

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
