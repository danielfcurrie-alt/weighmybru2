#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include "MAX17048Driver.h"

#include <math.h>

namespace {
constexpr uint8_t REG_VCELL = 0x02;
constexpr uint8_t REG_SOC = 0x04;
constexpr uint8_t REG_MODE = 0x06;
constexpr uint8_t REG_VERSION = 0x08;
constexpr uint8_t REG_CONFIG = 0x0C;
constexpr uint8_t REG_VALRT = 0x14;
constexpr uint8_t REG_CRATE = 0x16;
constexpr uint8_t REG_STATUS = 0x1A;
constexpr uint16_t MODE_QUICK_START = 0x4000;
constexpr float CRATE_PERCENT_PER_HOUR_PER_LSB = 0.208f;
constexpr float VOLTAGE_ALERT_VOLTS_PER_LSB = 0.020f;
}

MAX17048Driver::MAX17048Driver(TwoWire& wire, uint8_t address)
    : wire(wire), address(address), connected(false), cachedVersion(0),
      cachedChargeRatePercentPerHour(0.0f), cachedStatus(0),
      cachedConfiguration(0), cachedVoltageAlert(0), errorCount(0),
      lastReadMs(0), lastDiagnosticMs(0) {}

bool MAX17048Driver::begin() {
    connected = false;
    wire.beginTransmission(address);
    if (wire.endTransmission(true) != 0 || !readRegister16(REG_VERSION, cachedVersion)) {
        return false;
    }
    if (cachedVersion == 0x0000 || cachedVersion == 0xFFFF) {
        return false;
    }
    connected = true;
    refreshDiagnostics();
    return true;
}

bool MAX17048Driver::readVoltageAndSoc(float& voltage, float& stateOfCharge) {
    uint16_t vcell = 0;
    uint16_t soc = 0;
    if (!readRegister16(REG_VCELL, vcell) || !readRegister16(REG_SOC, soc)) {
        return false;
    }

    voltage = static_cast<float>(vcell >> 4) * 0.00125f;
    stateOfCharge = static_cast<float>(soc) / 256.0f;
    if (voltage < 2.0f || voltage > 5.0f || stateOfCharge < 0.0f || stateOfCharge > 110.0f) {
        errorCount++;
        return false;
    }
    lastReadMs = millis();
    return true;
}

bool MAX17048Driver::refreshDiagnostics() {
    uint16_t crate = 0;
    uint16_t statusValue = 0;
    uint16_t configValue = 0;
    uint16_t voltageAlertValue = 0;
    if (!readRegister16(REG_CRATE, crate) ||
        !readRegister16(REG_STATUS, statusValue) ||
        !readRegister16(REG_CONFIG, configValue) ||
        !readRegister16(REG_VALRT, voltageAlertValue)) {
        return false;
    }

    cachedChargeRatePercentPerHour =
        static_cast<int16_t>(crate) * CRATE_PERCENT_PER_HOUR_PER_LSB;
    cachedStatus = statusValue;
    cachedConfiguration = configValue;
    cachedVoltageAlert = voltageAlertValue;
    lastDiagnosticMs = millis();
    return true;
}

uint8_t MAX17048Driver::socAlertThresholdPercent() const {
    return 32 - static_cast<uint8_t>(cachedConfiguration & CONFIG_SOC_THRESHOLD_MASK);
}

float MAX17048Driver::minimumVoltageAlert() const {
    return static_cast<uint8_t>(cachedVoltageAlert >> 8) * VOLTAGE_ALERT_VOLTS_PER_LSB;
}

float MAX17048Driver::maximumVoltageAlert() const {
    return static_cast<uint8_t>(cachedVoltageAlert) * VOLTAGE_ALERT_VOLTS_PER_LSB;
}

bool MAX17048Driver::quickStart() {
    return connected && writeRegister16(REG_MODE, MODE_QUICK_START);
}

bool MAX17048Driver::setSocAlertThresholdPercent(uint8_t percentage) {
    if (!connected || percentage < 1 || percentage > 32) {
        return false;
    }
    const uint16_t next = (cachedConfiguration & ~CONFIG_SOC_THRESHOLD_MASK) |
                          static_cast<uint16_t>(32 - percentage);
    if (!writeRegister16(REG_CONFIG, next)) {
        return false;
    }
    cachedConfiguration = next;
    return true;
}

bool MAX17048Driver::setSocChangeAlertEnabled(bool enabled) {
    if (!connected) {
        return false;
    }
    const uint16_t next = enabled
        ? cachedConfiguration | CONFIG_SOC_CHANGE_ALERT
        : cachedConfiguration & ~CONFIG_SOC_CHANGE_ALERT;
    if (!writeRegister16(REG_CONFIG, next)) {
        return false;
    }
    cachedConfiguration = next;
    return true;
}

bool MAX17048Driver::setVoltageAlertRange(float minimumVoltage, float maximumVoltage) {
    if (!connected || minimumVoltage < 0.0f || maximumVoltage > 5.10f ||
        minimumVoltage >= maximumVoltage) {
        return false;
    }
    const uint8_t minimum = static_cast<uint8_t>(lroundf(minimumVoltage / VOLTAGE_ALERT_VOLTS_PER_LSB));
    const uint8_t maximum = static_cast<uint8_t>(lroundf(maximumVoltage / VOLTAGE_ALERT_VOLTS_PER_LSB));
    const uint16_t next = (static_cast<uint16_t>(minimum) << 8) | maximum;
    if (!writeRegister16(REG_VALRT, next)) {
        return false;
    }
    cachedVoltageAlert = next;
    return true;
}

bool MAX17048Driver::clearAlerts(uint16_t statusMask) {
    if (!connected) {
        return false;
    }
    const uint16_t nextStatus = cachedStatus & ~statusMask;
    const uint16_t nextConfiguration = cachedConfiguration & ~CONFIG_ALERT;
    if (!writeRegister16(REG_STATUS, nextStatus) ||
        !writeRegister16(REG_CONFIG, nextConfiguration)) {
        return false;
    }
    cachedStatus = nextStatus;
    cachedConfiguration = nextConfiguration;
    return true;
}

bool MAX17048Driver::readRegister16(uint8_t reg, uint16_t& value) {
    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission(false) != 0) {
        errorCount++;
        return false;
    }

    const size_t bytesRead = wire.requestFrom(static_cast<uint16_t>(address),
                                              static_cast<size_t>(2), true);
    if (bytesRead != 2 || wire.available() < 2) {
        while (wire.available()) {
            wire.read();
        }
        errorCount++;
        return false;
    }
    value = (static_cast<uint16_t>(wire.read()) << 8) | wire.read();
    return true;
}

bool MAX17048Driver::writeRegister16(uint8_t reg, uint16_t value) {
    wire.beginTransmission(address);
    wire.write(reg);
    wire.write(static_cast<uint8_t>(value >> 8));
    wire.write(static_cast<uint8_t>(value));
    if (wire.endTransmission(true) != 0) {
        errorCount++;
        return false;
    }
    return true;
}

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
