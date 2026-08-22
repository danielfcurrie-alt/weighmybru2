#ifndef MAX17048_DRIVER_H
#define MAX17048_DRIVER_H

#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include <Arduino.h>
#include <Wire.h>

class MAX17048Driver {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x36;

    enum StatusFlag : uint16_t {
        ResetIndicator = 0x0100,
        VoltageHigh = 0x0200,
        VoltageLow = 0x0400,
        VoltageReset = 0x0800,
        SocLow = 0x1000,
        SocChanged = 0x2000,
    };

    explicit MAX17048Driver(TwoWire& wire = Wire, uint8_t address = DEFAULT_ADDRESS);

    bool begin();
    bool isConnected() const { return connected; }
    bool readVoltageAndSoc(float& voltage, float& stateOfCharge);
    bool refreshDiagnostics();

    uint16_t version() const { return cachedVersion; }
    float chargeRatePercentPerHour() const { return cachedChargeRatePercentPerHour; }
    uint16_t status() const { return cachedStatus; }
    uint16_t configuration() const { return cachedConfiguration; }
    bool alertAsserted() const { return (cachedConfiguration & CONFIG_ALERT) != 0; }
    uint8_t socAlertThresholdPercent() const;
    float minimumVoltageAlert() const;
    float maximumVoltageAlert() const;
    uint32_t communicationErrors() const { return errorCount; }
    uint32_t lastSuccessfulReadMillis() const { return lastReadMs; }
    uint32_t lastDiagnosticMillis() const { return lastDiagnosticMs; }

    bool quickStart();
    bool setSocAlertThresholdPercent(uint8_t percentage);
    bool setSocChangeAlertEnabled(bool enabled);
    bool setVoltageAlertRange(float minimumVoltage, float maximumVoltage);
    bool clearAlerts(uint16_t statusMask = 0x3F00);

private:
    static constexpr uint16_t CONFIG_SOC_CHANGE_ALERT = 0x0040;
    static constexpr uint16_t CONFIG_ALERT = 0x0020;
    static constexpr uint16_t CONFIG_SOC_THRESHOLD_MASK = 0x001F;

    TwoWire& wire;
    uint8_t address;
    bool connected;
    uint16_t cachedVersion;
    float cachedChargeRatePercentPerHour;
    uint16_t cachedStatus;
    uint16_t cachedConfiguration;
    uint16_t cachedVoltageAlert;
    uint32_t errorCount;
    uint32_t lastReadMs;
    uint32_t lastDiagnosticMs;

    bool readRegister16(uint8_t reg, uint16_t& value);
    bool writeRegister16(uint8_t reg, uint16_t value);
};

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
#endif
