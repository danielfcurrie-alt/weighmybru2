#ifndef BATTERYMONITOR_H
#define BATTERYMONITOR_H

#include <Arduino.h>
#include <Preferences.h>
#include "BoardConfig.h"
#if HAS_I2C_FUEL_GAUGE
  #include "MAX17048Driver.h"
  #include "BatteryTimeEstimator.h"
#endif

class BatteryMonitor {
public:
    BatteryMonitor(uint8_t batteryPin);
    void begin();
    
    // Battery readings
    float getBatteryVoltage();
    int getBatteryPercentage();
    int getRawBatteryPercentage();
    String getBatteryStatus();  // "Full", "Good", "Low", "Critical"
    int getEstimatedRuntimeMinutesRemaining();
    String getRuntimeEstimateConfidence() const;
    int getRuntimeObservationMinutes() const;
    float getDischargeRatePercentPerHour() const;
    String getChargingState();
    int getEstimatedMinutesTo80();
    int getEstimatedMinutesTo100();
    String getChargeEstimateConfidence() const;
    int getChargeObservationMinutes() const;
    float getChargeRatePercentPerHour() const;
    float getLearnedDischargeRatePercentPerHour() const;
    float getLearnedChargeRatePercentPerHour() const;
    float getEstimatedDischargeCurrentMa() const;
    float getEstimatedChargeCurrentMa() const;
    float getLearnedDischargeCurrentMa() const;
    float getLearnedChargeCurrentMa() const;
    uint16_t getLearnedDischargeObservations() const;
    uint16_t getLearnedChargeObservations() const;
    String getBatteryLearningConfidence() const;
    String getBatteryBackend() const;
    uint16_t getBatteryCapacityMah() const { return batteryCapacityMah; }
    void setBatteryCapacityMah(uint16_t capacityMah);
    bool isCriticalShutdownEnabled() const { return criticalShutdownEnabled; }
    void setCriticalShutdownEnabled(bool enabled);
    float getCriticalShutdownVoltage() const { return criticalShutdownVoltage; }
    void setCriticalShutdownVoltage(float voltage);
    uint8_t getCriticalShutdownPercent() const { return criticalShutdownPercent; }
    void setCriticalShutdownPercent(uint8_t percent);
    bool shouldForceCriticalSleep();
    bool hasRecoveredFromCriticalSleep();
    float getCriticalRecoveryVoltage() const;
    uint8_t getCriticalRecoveryPercent() const;
    bool hasFuelGauge() const { return fuelGaugeAvailable; }
    bool isUsbPowerPresent() const { return usbPowerPresent; }
    bool isUsbOnlyPower() const { return usbOnlyPower; }
    float getFuelGaugeStateOfCharge() const { return fuelGaugeStateOfCharge; }
#if HAS_I2C_FUEL_GAUGE
    uint16_t getFuelGaugeVersion() const { return fuelGauge.version(); }
    float getFuelGaugeChargeRatePercentPerHour() const { return fuelGauge.chargeRatePercentPerHour(); }
    uint16_t getFuelGaugeStatus() const { return fuelGauge.status(); }
    uint16_t getFuelGaugeConfiguration() const { return fuelGauge.configuration(); }
    bool isFuelGaugeAlertAsserted() const { return fuelGauge.alertAsserted(); }
    uint8_t getFuelGaugeSocAlertThresholdPercent() const { return fuelGauge.socAlertThresholdPercent(); }
    float getFuelGaugeMinimumVoltageAlert() const { return fuelGauge.minimumVoltageAlert(); }
    float getFuelGaugeMaximumVoltageAlert() const { return fuelGauge.maximumVoltageAlert(); }
    uint32_t getFuelGaugeCommunicationErrors() const { return fuelGauge.communicationErrors(); }
    uint32_t getFuelGaugeLastDiagnosticMillis() const { return fuelGauge.lastDiagnosticMillis(); }
    bool hasFreshFuelGaugeRate() const;
    int getFuelGaugeRateRuntimeMinutes() const;
    int getFuelGaugeRateMinutesTo80() const;
    int getFuelGaugeRateMinutesTo100() const;
    int getProjectedRuntimeMinutes(bool wifiOn) const;
    int getProjectedMinutesTo80(bool wifiOn) const;
    int getProjectedMinutesTo100(bool wifiOn) const;
    float getProjectedActiveCurrentMa(bool wifiOn) const;
    float getProjectedNetChargeCurrentMa(bool wifiOn) const;
    float getProjectedChargerCurrentMa() const { return BatteryTimeEstimator::TINYS3D_CHARGER_MA; }
    float getProjectedChargeEfficiency() const { return BatteryTimeEstimator::TINYS3D_CHARGE_EFFICIENCY; }
    const char* getBatteryProjectionModel() const { return BatteryTimeEstimator::TINYS3D_MODEL_NAME; }
    bool quickStartFuelGauge() { return fuelGaugeAvailable && fuelGauge.quickStart(); }
    bool clearFuelGaugeAlerts(uint16_t mask = 0x3F00) { return fuelGaugeAvailable && fuelGauge.clearAlerts(mask); }
#else
    uint16_t getFuelGaugeVersion() const { return 0; }
    float getFuelGaugeChargeRatePercentPerHour() const { return 0.0f; }
    uint16_t getFuelGaugeStatus() const { return 0; }
    uint16_t getFuelGaugeConfiguration() const { return 0; }
    bool isFuelGaugeAlertAsserted() const { return false; }
    uint8_t getFuelGaugeSocAlertThresholdPercent() const { return 0; }
    float getFuelGaugeMinimumVoltageAlert() const { return 0.0f; }
    float getFuelGaugeMaximumVoltageAlert() const { return 0.0f; }
    uint32_t getFuelGaugeCommunicationErrors() const { return 0; }
    uint32_t getFuelGaugeLastDiagnosticMillis() const { return 0; }
    bool quickStartFuelGauge() { return false; }
    bool clearFuelGaugeAlerts(uint16_t = 0x3F00) { return false; }
#endif
    
    // Battery state indicators
    bool isCharging();
    bool isLowBattery();
    bool isCriticalBattery();
    bool hasValidReading() const { return hasReading; }
    
    // Configuration and calibration
    void calibrateVoltage(float actualVoltage);  // For fine-tuning readings
    float getCalibrationOffset() const { return calibrationOffset; }
    
    // Update method for periodic readings
    void update();
    
    // OLED display helper - returns battery segments (0-3)
    int getBatterySegments();
    
private:
    uint8_t batteryPin;
    Preferences preferences;
#if HAS_I2C_FUEL_GAUGE
    MAX17048Driver fuelGauge;
#endif
    
    // Li-ion voltage thresholds optimized for ESP32 operation. Capacity is
    // separately configurable for runtime/current estimates.
    static constexpr float BATTERY_FULL = 4.2f;      // 100% - Fresh charge
    static constexpr float BATTERY_GOOD = 4.0f;      // ~75% - Reliable ESP32 operation
    static constexpr float BATTERY_NOMINAL = 3.8f;   // ~50% - Normal operation
    static constexpr float BATTERY_LOW = 3.6f;       // ~25% - Consider charging soon
    static constexpr float BATTERY_CRITICAL = 3.4f;  // ~5%  - Charge immediately
    static constexpr float BATTERY_EMPTY = 3.2f;     // 0%   - Lower discharge threshold
    
    // Hardware configuration
    static constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;  // 100k + 100k resistors
    static constexpr float ADC_REFERENCE = 3.3f;          // ESP32-S3 with ADC_11db attenuation (0-3.3V)
    static constexpr int ADC_MAX_READING = 4095;
    static constexpr uint16_t DEFAULT_BATTERY_CAPACITY_MAH = 700;
    static constexpr uint16_t MIN_BATTERY_CAPACITY_MAH = 100;
    static constexpr uint16_t MAX_BATTERY_CAPACITY_MAH = 5000;
    static constexpr float DEFAULT_CRITICAL_SHUTDOWN_VOLTAGE = 3.45f;
    static constexpr float MIN_CRITICAL_SHUTDOWN_VOLTAGE = 3.20f;
    static constexpr float MAX_CRITICAL_SHUTDOWN_VOLTAGE = 3.80f;
    static constexpr uint8_t DEFAULT_CRITICAL_SHUTDOWN_PERCENT = 7;
    static constexpr uint8_t MIN_CRITICAL_SHUTDOWN_PERCENT = 1;
    static constexpr uint8_t MAX_CRITICAL_SHUTDOWN_PERCENT = 20;
    static constexpr float CRITICAL_RECOVERY_MARGIN_VOLTAGE = 0.10f;
    static constexpr uint8_t CRITICAL_RECOVERY_MARGIN_PERCENT = 3;
    
    // Calibration and smoothing
    float calibrationOffset = 0.0f;  // Voltage adjustment for accuracy
    uint16_t batteryCapacityMah = DEFAULT_BATTERY_CAPACITY_MAH;
    bool criticalShutdownEnabled = true;
    float criticalShutdownVoltage = DEFAULT_CRITICAL_SHUTDOWN_VOLTAGE;
    uint8_t criticalShutdownPercent = DEFAULT_CRITICAL_SHUTDOWN_PERCENT;
    float lastVoltage = 0.0f;        // For smoothing readings
    float smoothedPercentage = -1.0f;
    int rawPercentage = 0;
    bool hasReading = false;
    float dischargeStartPercentage = -1.0f;
    unsigned long dischargeStartMillis = 0;
    float estimatedRuntimeMinutes = -1.0f;
    float dischargeRatePercentPerHour = 0.0f;
    String runtimeEstimateConfidence = "learning";
    float chargeStartPercentage = -1.0f;
    unsigned long chargeStartMillis = 0;
    float estimatedMinutesTo80 = -1.0f;
    float estimatedMinutesTo100 = -1.0f;
    float chargeRatePercentPerHour = 0.0f;
    String chargeEstimateConfidence = "learning";
    String chargingState = "unknown";
    float learnedDischargeRatePercentPerHour = 0.0f;
    float learnedChargeRatePercentPerHour = 0.0f;
    uint16_t learnedDischargeObservations = 0;
    uint16_t learnedChargeObservations = 0;
    bool fuelGaugeAvailable = false;
    bool usbPowerPresent = false;
    bool usbOnlyPower = false;
    float fuelGaugeStateOfCharge = -1.0f;
    unsigned long lastLearningSaveMillis = 0;
    unsigned long lastUpdate = 0;
    static constexpr unsigned long UPDATE_INTERVAL = 1000; // Update every 1 second
    static constexpr unsigned long FUEL_GAUGE_DIAGNOSTIC_INTERVAL_MS = 5000;
    static constexpr unsigned long FUEL_GAUGE_RATE_MAX_AGE_MS = 15000;
    static constexpr float VOLTAGE_EMA_ALPHA = 0.2f;
    static constexpr float PERCENT_EMA_ALPHA = 0.18f;
    static constexpr float PERCENT_MAX_STEP = 2.0f;
    static constexpr int PERCENT_VISIBLE_STEP = 5;
    static constexpr float CHARGE_DETECTION_DELTA_PERCENT = 1.0f;
    static constexpr float CHARGE_ESTIMATE_MIN_DELTA_PERCENT = 1.5f;
    static constexpr float CHARGE_ESTIMATE_MIN_MINUTES = 3.0f;
    static constexpr float CHARGE_FULL_PERCENT = 98.5f;
    static constexpr float CHARGE_FULL_VOLTAGE = 4.15f;
    static constexpr unsigned long LEARNING_SAVE_INTERVAL_MS = 15UL * 60UL * 1000UL;
    static constexpr float LEARNING_MIN_DISCHARGE_OBSERVATION_MINUTES = 20.0f;
    static constexpr float LEARNING_MIN_CHARGE_OBSERVATION_MINUTES = 15.0f;
    static constexpr float LEARNING_MIN_DISCHARGE_RATE_PERCENT_PER_HOUR = 1.0f;
    static constexpr float LEARNING_MAX_DISCHARGE_RATE_PERCENT_PER_HOUR = 80.0f;
    static constexpr float LEARNING_MIN_CHARGE_RATE_PERCENT_PER_HOUR = 1.0f;
    static constexpr float LEARNING_MAX_CHARGE_RATE_PERCENT_PER_HOUR = 120.0f;
    
    // Internal methods
    bool readBatterySnapshot(float& voltage, int& percentage);
    float readRawVoltage();
    bool beginFuelGauge();
    bool readFuelGaugeSnapshot(float& voltage, float& stateOfCharge);
    bool readUsbPowerPresent();
    int voltageToPercentage(float voltage) const;
    int stateOfChargeToPercentage(float stateOfCharge) const;
    int quantizePercentage(int percentage) const;
    void updateRuntimeEstimate();
    void updateChargeEstimate();
    void maybeLearnDischargeRate(float ratePercentPerHour, float elapsedMinutes);
    void maybeLearnChargeRate(float ratePercentPerHour, float elapsedMinutes);
    void loadCapacitySetting();
    void saveCapacitySetting();
    void loadSafetySettings();
    void saveSafetySettings();
    void loadLearningProfile();
    void saveLearningProfile();
    void loadCalibration();
    void saveCalibration();
    float lowBatteryVoltageThreshold() const;
    uint8_t lowBatteryPercentThreshold() const;
    bool fuelGaugeSocAvailable() const;
};

#endif
