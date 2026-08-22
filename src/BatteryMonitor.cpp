#include "BatteryMonitor.h"
#include "SimulationProfiles.h"
#include <Wire.h>
#include <math.h>

BatteryMonitor::BatteryMonitor(uint8_t batteryPin)
    : batteryPin(batteryPin)
#if HAS_I2C_FUEL_GAUGE
    , fuelGauge(Wire, FUEL_GAUGE_MAX17048_ADDR)
#endif
{
    lastVoltage = 0.0f;
    smoothedPercentage = -1.0f;
    rawPercentage = 0;
    hasReading = false;
    dischargeStartPercentage = -1.0f;
    dischargeStartMillis = 0;
    estimatedRuntimeMinutes = -1.0f;
    dischargeRatePercentPerHour = 0.0f;
    runtimeEstimateConfidence = "learning";
    chargeStartPercentage = -1.0f;
    chargeStartMillis = 0;
    estimatedMinutesTo80 = -1.0f;
    estimatedMinutesTo100 = -1.0f;
    chargeRatePercentPerHour = 0.0f;
    chargeEstimateConfidence = "learning";
    chargingState = "unknown";
    learnedDischargeRatePercentPerHour = 0.0f;
    learnedChargeRatePercentPerHour = 0.0f;
    learnedDischargeObservations = 0;
    learnedChargeObservations = 0;
    fuelGaugeAvailable = false;
    usbPowerPresent = false;
    usbOnlyPower = false;
    fuelGaugeStateOfCharge = -1.0f;
    lastLearningSaveMillis = 0;
    lastUpdate = 0;
}

void BatteryMonitor::begin() {
    Serial.println("Initializing Battery Monitor...");

#if HAS_ADC_BATTERY
    // Configure ADC pin and settings
    if (batteryPin != BATTERY_PIN_NONE) {
        pinMode(batteryPin, INPUT);
        analogReadResolution(12);  // Use 12-bit resolution (0-4095)
        analogSetAttenuation(ADC_11db);  // 0-3.3V range for better accuracy
    }
#endif

#if HAS_USB_POWER_SENSE
    pinMode(USB_POWER_SENSE_PIN, INPUT);
    usbPowerPresent = readUsbPowerPresent();
#endif

#if HAS_I2C_FUEL_GAUGE
    fuelGaugeAvailable = beginFuelGauge();
#endif
    
    // Load calibration from preferences
    preferences.begin("battery", false);
    loadCalibration();
    loadCapacitySetting();
    loadSafetySettings();
    loadLearningProfile();
    preferences.end();
    
    // Take initial reading
    update();
    
    Serial.printf("Battery Monitor initialized using %s backend\n", getBatteryBackend().c_str());
    Serial.printf("Initial voltage: %.2fV (%d%%)\n", getBatteryVoltage(), getBatteryPercentage());
}

void BatteryMonitor::update() {
    unsigned long currentTime = millis();
    
    // Limit update frequency to reduce noise
    if (hasReading && currentTime - lastUpdate < UPDATE_INTERVAL) {
        return;
    }
    
    float newVoltage = 0.0f;
    int newRawPercentage = 0;
    if (!readBatterySnapshot(newVoltage, newRawPercentage)) {
        hasReading = false;
        lastUpdate = currentTime;
        return;
    }
    
    // Simple smoothing filter (exponential moving average)
    if (!hasReading) {
        lastVoltage = newVoltage;  // First reading
        smoothedPercentage = newRawPercentage;
    } else {
        lastVoltage = (lastVoltage * (1.0f - VOLTAGE_EMA_ALPHA)) + (newVoltage * VOLTAGE_EMA_ALPHA);

        float nextPercentage = (smoothedPercentage * (1.0f - PERCENT_EMA_ALPHA)) + (newRawPercentage * PERCENT_EMA_ALPHA);
        float delta = nextPercentage - smoothedPercentage;

        if (delta > PERCENT_MAX_STEP) {
            nextPercentage = smoothedPercentage + PERCENT_MAX_STEP;
        } else if (delta < -PERCENT_MAX_STEP) {
            nextPercentage = smoothedPercentage - PERCENT_MAX_STEP;
        }

        smoothedPercentage = nextPercentage;
    }
    
    rawPercentage = newRawPercentage;
    hasReading = true;
    updateRuntimeEstimate();
    updateChargeEstimate();
    lastUpdate = currentTime;
}

bool BatteryMonitor::readBatterySnapshot(float& voltage, int& percentage) {
#if WMBP_SIMULATION_MODE
    usbPowerPresent = SimulationProfiles::simulatedUsbPowerPresent(WMBP_SIM_BATTERY_PROFILE);
    voltage = SimulationProfiles::batteryVoltage(millis(), WMBP_SIM_BATTERY_PROFILE);
    fuelGaugeStateOfCharge = -1.0f;
    percentage = voltageToPercentage(voltage);
    return true;
#endif

#if HAS_USB_POWER_SENSE
    usbPowerPresent = readUsbPowerPresent();
#endif
    usbOnlyPower = false;

#if HAS_I2C_FUEL_GAUGE
    if (fuelGaugeAvailable) {
        float stateOfCharge = -1.0f;
        if (readFuelGaugeSnapshot(voltage, stateOfCharge)) {
            voltage += calibrationOffset;
            fuelGaugeStateOfCharge = stateOfCharge;
            percentage = stateOfChargeToPercentage(stateOfCharge);
            return true;
        }

        Serial.println("Battery fuel gauge read failed; marking battery reading invalid");
        if (usbPowerPresent) {
            usbOnlyPower = true;
            chargingState = "usb_only";
        }
        return false;
    }
#endif

#if HAS_ADC_BATTERY
    voltage = readRawVoltage();
    if (voltage <= 0.1f) {
        if (usbPowerPresent) {
            usbOnlyPower = true;
            chargingState = "usb_only";
        }
        return false;
    }
    fuelGaugeStateOfCharge = -1.0f;
    percentage = voltageToPercentage(voltage);
    return true;
#else
    if (usbPowerPresent) {
        usbOnlyPower = true;
        chargingState = "usb_only";
    }
    return false;
#endif
}

float BatteryMonitor::readRawVoltage() {
#if HAS_ADC_BATTERY
    if (batteryPin == BATTERY_PIN_NONE) {
        return 0.0f;
    }

    // Take multiple readings for accuracy
    int totalReading = 0;
    const int samples = 10;
    
    for (int i = 0; i < samples; i++) {
        totalReading += analogRead(batteryPin);
        delayMicroseconds(100);  // Small delay between readings
    }
    
    int avgReading = totalReading / samples;
    
    // Convert ADC reading to voltage
    float voltage = ((float)avgReading / ADC_MAX_READING) * ADC_REFERENCE * VOLTAGE_DIVIDER_RATIO;
    
    // Apply calibration offset
    voltage += calibrationOffset;
    
    return voltage;
#else
    return lastVoltage;
#endif
}

bool BatteryMonitor::beginFuelGauge() {
#if HAS_I2C_FUEL_GAUGE
    if (!fuelGauge.begin()) {
        Serial.printf("MAX17048 fuel gauge not found at 0x%02X\n", FUEL_GAUGE_MAX17048_ADDR);
        return false;
    }
    Serial.printf("MAX17048 fuel gauge detected at 0x%02X version=0x%04X\n",
                  FUEL_GAUGE_MAX17048_ADDR,
                  fuelGauge.version());
    return true;
#else
    return false;
#endif
}

bool BatteryMonitor::readFuelGaugeSnapshot(float& voltage, float& stateOfCharge) {
#if HAS_I2C_FUEL_GAUGE
    if (!fuelGauge.readVoltageAndSoc(voltage, stateOfCharge)) {
        return false;
    }
    if (fuelGauge.lastDiagnosticMillis() == 0 ||
        millis() - fuelGauge.lastDiagnosticMillis() >= FUEL_GAUGE_DIAGNOSTIC_INTERVAL_MS) {
        fuelGauge.refreshDiagnostics();
    }
    return true;
#else
    return false;
#endif
}

bool BatteryMonitor::readUsbPowerPresent() {
#if WMBP_SIMULATION_MODE
    return SimulationProfiles::simulatedUsbPowerPresent(WMBP_SIM_BATTERY_PROFILE);
#endif

#if HAS_USB_POWER_SENSE
    return digitalRead(USB_POWER_SENSE_PIN) == HIGH;
#else
    return false;
#endif
}

float BatteryMonitor::getBatteryVoltage() {
    return lastVoltage;
}

int BatteryMonitor::getBatteryPercentage() {
    if (!hasReading) {
        update();
    }

    if (!hasReading) {
        return 0;
    }

    const int percentage = (int)roundf(smoothedPercentage);

    // Fuel-gauge boards have a real SOC estimator, so expose the fine-grained
    // value. ADC-backed boards are voltage-derived and sag under load, so expose
    // a coarse visible percentage while preserving raw voltage/raw percentage in
    // diagnostics.
    if (fuelGaugeAvailable) {
        return constrain(percentage, 0, 100);
    }

    return quantizePercentage(percentage);
}

int BatteryMonitor::getRawBatteryPercentage() {
    if (!hasReading) {
        update();
    }

    return constrain(rawPercentage, 0, 100);
}

int BatteryMonitor::getEstimatedRuntimeMinutesRemaining() {
    if (!hasReading) {
        update();
    }

    #if HAS_I2C_FUEL_GAUGE
    if (estimatedRuntimeMinutes < 0.0f && !usbPowerPresent) {
        const int gaugeMinutes = getFuelGaugeRateRuntimeMinutes();
        if (gaugeMinutes >= 0) {
            return gaugeMinutes;
        }
    }
    #endif

    if (estimatedRuntimeMinutes < 0.0f &&
        learnedDischargeRatePercentPerHour > 0.0f &&
        chargingState != "charging_likely" &&
        chargingState != "usb_present" &&
        chargingState != "full") {
        const float currentPercentage = constrain(smoothedPercentage, 0.0f, 100.0f);
        const float learnedPercentPerMinute = learnedDischargeRatePercentPerHour / 60.0f;
        if (learnedPercentPerMinute > 0.0f) {
            return (int)roundf(currentPercentage / learnedPercentPerMinute);
        }
    }

    if (estimatedRuntimeMinutes < 0.0f) {
        return -1;
    }

    return (int)roundf(estimatedRuntimeMinutes);
}

String BatteryMonitor::getRuntimeEstimateConfidence() const {
    #if HAS_I2C_FUEL_GAUGE
    if (estimatedRuntimeMinutes < 0.0f && !usbPowerPresent &&
        getFuelGaugeRateRuntimeMinutes() >= 0) {
        return "fuel-gauge-rate";
    }
    #endif

    if (estimatedRuntimeMinutes < 0.0f &&
        learnedDischargeRatePercentPerHour > 0.0f &&
        chargingState != "charging_likely" &&
        chargingState != "usb_present" &&
        chargingState != "full") {
        return "learned-" + getBatteryLearningConfidence();
    }

    return runtimeEstimateConfidence;
}

int BatteryMonitor::getRuntimeObservationMinutes() const {
    if (dischargeStartMillis == 0) {
        return 0;
    }

    return (int)((millis() - dischargeStartMillis) / 60000UL);
}

float BatteryMonitor::getDischargeRatePercentPerHour() const {
    return dischargeRatePercentPerHour;
}

String BatteryMonitor::getChargingState() {
    if (!hasReading) {
        update();
    }

    #if HAS_I2C_FUEL_GAUGE
    if (hasFreshFuelGaugeRate() && usbPowerPresent &&
        fuelGauge.chargeRatePercentPerHour() >= BatteryTimeEstimator::MIN_RATE_PERCENT_PER_HOUR) {
        return "charging_likely";
    }
    #endif

    return chargingState;
}

int BatteryMonitor::getEstimatedMinutesTo80() {
    if (!hasReading) {
        update();
    }

    #if HAS_I2C_FUEL_GAUGE
    if (estimatedMinutesTo80 < 0.0f) {
        const int gaugeMinutes = getFuelGaugeRateMinutesTo80();
        if (gaugeMinutes >= 0) {
            return gaugeMinutes;
        }
    }
    #endif

    if (estimatedMinutesTo80 < 0.0f &&
        learnedChargeRatePercentPerHour > 0.0f &&
        chargingState == "charging_likely") {
        const float currentPercentage = constrain(smoothedPercentage, 0.0f, 100.0f);
        if (currentPercentage >= 80.0f) {
            return 0;
        }

        const float learnedPercentPerMinute = learnedChargeRatePercentPerHour / 60.0f;
        if (learnedPercentPerMinute > 0.0f) {
            return (int)roundf((80.0f - currentPercentage) / learnedPercentPerMinute);
        }
    }

    if (estimatedMinutesTo80 < 0.0f) {
        return -1;
    }

    return (int)roundf(estimatedMinutesTo80);
}

int BatteryMonitor::getEstimatedMinutesTo100() {
    if (!hasReading) {
        update();
    }

    #if HAS_I2C_FUEL_GAUGE
    if (estimatedMinutesTo100 < 0.0f) {
        const int gaugeMinutes = getFuelGaugeRateMinutesTo100();
        if (gaugeMinutes >= 0) {
            return gaugeMinutes;
        }
    }
    #endif

    if (estimatedMinutesTo100 < 0.0f &&
        learnedChargeRatePercentPerHour > 0.0f &&
        chargingState == "charging_likely") {
        const float currentPercentage = constrain(smoothedPercentage, 0.0f, 100.0f);
        if (currentPercentage >= 100.0f) {
            return 0;
        }

        const float learnedPercentPerMinute = learnedChargeRatePercentPerHour / 60.0f;
        if (learnedPercentPerMinute > 0.0f) {
            return (int)roundf((100.0f - currentPercentage) / learnedPercentPerMinute);
        }
    }

    if (estimatedMinutesTo100 < 0.0f) {
        return -1;
    }

    return (int)roundf(estimatedMinutesTo100);
}

String BatteryMonitor::getChargeEstimateConfidence() const {
    #if HAS_I2C_FUEL_GAUGE
    if (estimatedMinutesTo80 < 0.0f && estimatedMinutesTo100 < 0.0f &&
        (getFuelGaugeRateMinutesTo80() >= 0 || getFuelGaugeRateMinutesTo100() >= 0)) {
        return "fuel-gauge-rate";
    }
    #endif

    if (estimatedMinutesTo80 < 0.0f &&
        estimatedMinutesTo100 < 0.0f &&
        learnedChargeRatePercentPerHour > 0.0f &&
        chargingState == "charging_likely") {
        return "learned-" + getBatteryLearningConfidence();
    }

    return chargeEstimateConfidence;
}

int BatteryMonitor::getChargeObservationMinutes() const {
    if (chargeStartMillis == 0) {
        return 0;
    }

    return (int)((millis() - chargeStartMillis) / 60000UL);
}

float BatteryMonitor::getChargeRatePercentPerHour() const {
    return chargeRatePercentPerHour;
}

float BatteryMonitor::getLearnedDischargeRatePercentPerHour() const {
    return learnedDischargeRatePercentPerHour;
}

float BatteryMonitor::getLearnedChargeRatePercentPerHour() const {
    return learnedChargeRatePercentPerHour;
}

float BatteryMonitor::getEstimatedDischargeCurrentMa() const {
    if (dischargeRatePercentPerHour <= 0.0f) {
        return 0.0f;
    }
    return (dischargeRatePercentPerHour * batteryCapacityMah) / 100.0f;
}

float BatteryMonitor::getEstimatedChargeCurrentMa() const {
    if (chargeRatePercentPerHour <= 0.0f) {
        return 0.0f;
    }
    return (chargeRatePercentPerHour * batteryCapacityMah) / 100.0f;
}

float BatteryMonitor::getLearnedDischargeCurrentMa() const {
    if (learnedDischargeRatePercentPerHour <= 0.0f) {
        return 0.0f;
    }
    return (learnedDischargeRatePercentPerHour * batteryCapacityMah) / 100.0f;
}

float BatteryMonitor::getLearnedChargeCurrentMa() const {
    if (learnedChargeRatePercentPerHour <= 0.0f) {
        return 0.0f;
    }
    return (learnedChargeRatePercentPerHour * batteryCapacityMah) / 100.0f;
}

#if HAS_I2C_FUEL_GAUGE
namespace {
int roundedMinutes(float minutes) {
    return minutes < 0.0f ? -1 : static_cast<int>(roundf(minutes));
}
}

bool BatteryMonitor::hasFreshFuelGaugeRate() const {
    const uint32_t lastDiagnostic = fuelGauge.lastDiagnosticMillis();
    return fuelGaugeAvailable && hasReading && lastDiagnostic != 0 &&
           millis() - lastDiagnostic <= FUEL_GAUGE_RATE_MAX_AGE_MS;
}

int BatteryMonitor::getFuelGaugeRateRuntimeMinutes() const {
    if (!hasFreshFuelGaugeRate()) {
        return -1;
    }
    return roundedMinutes(BatteryTimeEstimator::runtimeMinutesFromGaugeRate(
        fuelGaugeStateOfCharge, fuelGauge.chargeRatePercentPerHour()));
}

int BatteryMonitor::getFuelGaugeRateMinutesTo80() const {
    if (!hasFreshFuelGaugeRate() || !usbPowerPresent) {
        return -1;
    }
    return roundedMinutes(BatteryTimeEstimator::minutesToTargetFromGaugeRate(
        fuelGaugeStateOfCharge, 80.0f, fuelGauge.chargeRatePercentPerHour()));
}

int BatteryMonitor::getFuelGaugeRateMinutesTo100() const {
    if (!hasFreshFuelGaugeRate() || !usbPowerPresent) {
        return -1;
    }
    return roundedMinutes(BatteryTimeEstimator::minutesToTargetFromGaugeRate(
        fuelGaugeStateOfCharge, 100.0f, fuelGauge.chargeRatePercentPerHour()));
}

int BatteryMonitor::getProjectedRuntimeMinutes(bool wifiOn) const {
    if (!fuelGaugeAvailable || !hasReading || fuelGaugeStateOfCharge < 0.0f) {
        return -1;
    }
    return roundedMinutes(BatteryTimeEstimator::runtimeMinutesFromCurrent(
        batteryCapacityMah, fuelGaugeStateOfCharge, getProjectedActiveCurrentMa(wifiOn)));
}

int BatteryMonitor::getProjectedMinutesTo80(bool wifiOn) const {
    if (!fuelGaugeAvailable || !hasReading || fuelGaugeStateOfCharge < 0.0f) {
        return -1;
    }
    return roundedMinutes(BatteryTimeEstimator::minutesToTargetFromCurrent(
        batteryCapacityMah, fuelGaugeStateOfCharge, 80.0f,
        getProjectedNetChargeCurrentMa(wifiOn)));
}

int BatteryMonitor::getProjectedMinutesTo100(bool wifiOn) const {
    if (!fuelGaugeAvailable || !hasReading || fuelGaugeStateOfCharge < 0.0f) {
        return -1;
    }
    return roundedMinutes(BatteryTimeEstimator::minutesToTargetFromCurrent(
        batteryCapacityMah, fuelGaugeStateOfCharge, 100.0f,
        getProjectedNetChargeCurrentMa(wifiOn)));
}

float BatteryMonitor::getProjectedActiveCurrentMa(bool wifiOn) const {
    return BatteryTimeEstimator::activeLoadMa(wifiOn);
}

float BatteryMonitor::getProjectedNetChargeCurrentMa(bool wifiOn) const {
    return BatteryTimeEstimator::netChargeCurrentMa(wifiOn);
}
#endif

uint16_t BatteryMonitor::getLearnedDischargeObservations() const {
    return learnedDischargeObservations;
}

uint16_t BatteryMonitor::getLearnedChargeObservations() const {
    return learnedChargeObservations;
}

String BatteryMonitor::getBatteryLearningConfidence() const {
    const uint16_t observations = learnedDischargeObservations + learnedChargeObservations;
    if (observations >= 8) {
        return "high";
    }
    if (observations >= 3) {
        return "medium";
    }
    if (observations > 0) {
        return "low";
    }

    return "none";
}

String BatteryMonitor::getBatteryBackend() const {
#if WMBP_SIMULATION_MODE
    return "sim-" + String(SimulationProfiles::batteryProfileName(WMBP_SIM_BATTERY_PROFILE));
#endif

    if (fuelGaugeAvailable) {
        return "max17048";
    }
#if HAS_I2C_FUEL_GAUGE
    if (!fuelGaugeAvailable) {
        return "max17048-missing";
    }
#endif
#if HAS_ADC_BATTERY
    return "adc";
#else
    return "none";
#endif
}

int BatteryMonitor::voltageToPercentage(float voltage) const {
    // Convert voltage to percentage using Li-ion discharge curve
    int percentage;
    
    if (voltage >= BATTERY_FULL) {
        percentage = 100;
    } else if (voltage >= BATTERY_GOOD) {
        // 100% to 75% range
        percentage = 75 + (int)((voltage - BATTERY_GOOD) / (BATTERY_FULL - BATTERY_GOOD) * 25);
    } else if (voltage >= BATTERY_NOMINAL) {
        // 75% to 50% range  
        percentage = 50 + (int)((voltage - BATTERY_NOMINAL) / (BATTERY_GOOD - BATTERY_NOMINAL) * 25);
    } else if (voltage >= BATTERY_LOW) {
        // 50% to 25% range
        percentage = 25 + (int)((voltage - BATTERY_LOW) / (BATTERY_NOMINAL - BATTERY_LOW) * 25);
    } else if (voltage >= BATTERY_CRITICAL) {
        // 25% to 5% range
        percentage = 5 + (int)((voltage - BATTERY_CRITICAL) / (BATTERY_LOW - BATTERY_CRITICAL) * 20);
    } else if (voltage >= BATTERY_EMPTY) {
        // 5% to 0% range
        percentage = (int)((voltage - BATTERY_EMPTY) / (BATTERY_CRITICAL - BATTERY_EMPTY) * 5);
    } else {
        percentage = 0;  // Below 3.2V threshold
    }
    
    return constrain(percentage, 0, 100);
}

int BatteryMonitor::stateOfChargeToPercentage(float stateOfCharge) const {
    return constrain((int)roundf(stateOfCharge), 0, 100);
}

int BatteryMonitor::quantizePercentage(int percentage) const {
    percentage = constrain(percentage, 0, 100);
    return constrain(((percentage + (PERCENT_VISIBLE_STEP / 2)) / PERCENT_VISIBLE_STEP) * PERCENT_VISIBLE_STEP, 0, 100);
}

void BatteryMonitor::updateRuntimeEstimate() {
    const unsigned long now = millis();
    const float currentPercentage = constrain(smoothedPercentage, 0.0f, 100.0f);

    if (usbPowerPresent) {
        dischargeStartPercentage = currentPercentage;
        dischargeStartMillis = now;
        estimatedRuntimeMinutes = -1.0f;
        dischargeRatePercentPerHour = 0.0f;
        runtimeEstimateConfidence = "usb-present";
        return;
    }

    if (dischargeStartPercentage < 0.0f) {
        dischargeStartPercentage = currentPercentage;
        dischargeStartMillis = now;
        estimatedRuntimeMinutes = -1.0f;
        dischargeRatePercentPerHour = 0.0f;
        runtimeEstimateConfidence = "learning";
        return;
    }

    // A material increase means USB/charging/noise changed the discharge curve.
    // Start a new observation window rather than learning from mixed states.
    if (currentPercentage > dischargeStartPercentage + 1.0f) {
        dischargeStartPercentage = currentPercentage;
        dischargeStartMillis = now;
        estimatedRuntimeMinutes = -1.0f;
        dischargeRatePercentPerHour = 0.0f;
        runtimeEstimateConfidence = "reset";
        return;
    }

    const unsigned long elapsedMillis = now - dischargeStartMillis;
    const float elapsedMinutes = elapsedMillis / 60000.0f;
    const float percentDrop = dischargeStartPercentage - currentPercentage;

    if (elapsedMinutes < 5.0f || percentDrop < 1.0f) {
        estimatedRuntimeMinutes = -1.0f;
        dischargeRatePercentPerHour = 0.0f;
        runtimeEstimateConfidence = "learning";
        return;
    }

    const float percentPerMinute = percentDrop / elapsedMinutes;
    if (percentPerMinute <= 0.0f) {
        estimatedRuntimeMinutes = -1.0f;
        dischargeRatePercentPerHour = 0.0f;
        runtimeEstimateConfidence = "learning";
        return;
    }

    estimatedRuntimeMinutes = currentPercentage / percentPerMinute;
    dischargeRatePercentPerHour = percentPerMinute * 60.0f;

    if (elapsedMinutes >= 60.0f && percentDrop >= 8.0f) {
        runtimeEstimateConfidence = "high";
    } else if (elapsedMinutes >= 20.0f && percentDrop >= 3.0f) {
        runtimeEstimateConfidence = "medium";
    } else {
        runtimeEstimateConfidence = "low";
    }

    maybeLearnDischargeRate(dischargeRatePercentPerHour, elapsedMinutes);
}

void BatteryMonitor::updateChargeEstimate() {
    const unsigned long now = millis();
    const float currentPercentage = constrain(smoothedPercentage, 0.0f, 100.0f);

    if (chargeStartPercentage < 0.0f || chargeStartMillis == 0) {
        chargeStartPercentage = currentPercentage;
        chargeStartMillis = now;
        estimatedMinutesTo80 = -1.0f;
        estimatedMinutesTo100 = -1.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = "learning";
        chargingState = "unknown";
        return;
    }

    // Near full is a state, not an ETA problem. With only ADC battery sense we
    // intentionally call this "full" instead of "done charging"; charger STAT
    // hardware can make that authoritative later.
    if (currentPercentage >= CHARGE_FULL_PERCENT || lastVoltage >= CHARGE_FULL_VOLTAGE) {
        estimatedMinutesTo80 = 0.0f;
        estimatedMinutesTo100 = 0.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = "full";
        chargingState = "full";
        return;
    }

    const float percentChange = currentPercentage - chargeStartPercentage;

    // A material drop means the pack is discharging or the previous charging
    // observation window was invalid. Start fresh from the current level.
    if (percentChange < -CHARGE_DETECTION_DELTA_PERCENT) {
        chargeStartPercentage = currentPercentage;
        chargeStartMillis = now;
        estimatedMinutesTo80 = -1.0f;
        estimatedMinutesTo100 = -1.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = usbPowerPresent ? "usb-present" : "discharging";
        chargingState = usbPowerPresent ? "usb_present" : "discharging";
        return;
    }

    const unsigned long elapsedMillis = now - chargeStartMillis;
    const float elapsedMinutes = elapsedMillis / 60000.0f;

    if (percentChange < CHARGE_DETECTION_DELTA_PERCENT) {
        estimatedMinutesTo80 = -1.0f;
        estimatedMinutesTo100 = -1.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = usbPowerPresent ? "usb-present" : "learning";
        chargingState = usbPowerPresent
            ? "usb_present"
            : (dischargeRatePercentPerHour > 0.0f ? "discharging" : "unknown");
        return;
    }

    chargingState = "charging_likely";

    if (elapsedMinutes < CHARGE_ESTIMATE_MIN_MINUTES ||
        percentChange < CHARGE_ESTIMATE_MIN_DELTA_PERCENT) {
        estimatedMinutesTo80 = -1.0f;
        estimatedMinutesTo100 = -1.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = "learning";
        return;
    }

    const float percentPerMinute = percentChange / elapsedMinutes;
    if (percentPerMinute <= 0.0f) {
        estimatedMinutesTo80 = -1.0f;
        estimatedMinutesTo100 = -1.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = "learning";
        return;
    }

    estimatedMinutesTo80 = currentPercentage >= 80.0f ? 0.0f : (80.0f - currentPercentage) / percentPerMinute;
    estimatedMinutesTo100 = currentPercentage >= 100.0f ? 0.0f : (100.0f - currentPercentage) / percentPerMinute;
    chargeRatePercentPerHour = percentPerMinute * 60.0f;

    if (elapsedMinutes >= 45.0f && percentChange >= 8.0f) {
        chargeEstimateConfidence = "high";
    } else if (elapsedMinutes >= 15.0f && percentChange >= 3.0f) {
        chargeEstimateConfidence = "medium";
    } else {
        chargeEstimateConfidence = "low";
    }

    maybeLearnChargeRate(chargeRatePercentPerHour, elapsedMinutes);
}

void BatteryMonitor::maybeLearnDischargeRate(float ratePercentPerHour, float elapsedMinutes) {
    const unsigned long now = millis();
    if (elapsedMinutes < LEARNING_MIN_DISCHARGE_OBSERVATION_MINUTES ||
        ratePercentPerHour < LEARNING_MIN_DISCHARGE_RATE_PERCENT_PER_HOUR ||
        ratePercentPerHour > LEARNING_MAX_DISCHARGE_RATE_PERCENT_PER_HOUR ||
        (lastLearningSaveMillis != 0 && now - lastLearningSaveMillis < LEARNING_SAVE_INTERVAL_MS)) {
        return;
    }

    const float alpha = learnedDischargeObservations == 0 ? 1.0f : (elapsedMinutes >= 60.0f ? 0.20f : 0.12f);
    learnedDischargeRatePercentPerHour =
        learnedDischargeObservations == 0
            ? ratePercentPerHour
            : (learnedDischargeRatePercentPerHour * (1.0f - alpha)) + (ratePercentPerHour * alpha);

    if (learnedDischargeObservations < UINT16_MAX) {
        learnedDischargeObservations++;
    }

    saveLearningProfile();
    lastLearningSaveMillis = now;
}

void BatteryMonitor::maybeLearnChargeRate(float ratePercentPerHour, float elapsedMinutes) {
    const unsigned long now = millis();
    if (elapsedMinutes < LEARNING_MIN_CHARGE_OBSERVATION_MINUTES ||
        ratePercentPerHour < LEARNING_MIN_CHARGE_RATE_PERCENT_PER_HOUR ||
        ratePercentPerHour > LEARNING_MAX_CHARGE_RATE_PERCENT_PER_HOUR ||
        (lastLearningSaveMillis != 0 && now - lastLearningSaveMillis < LEARNING_SAVE_INTERVAL_MS)) {
        return;
    }

    const float alpha = learnedChargeObservations == 0 ? 1.0f : (elapsedMinutes >= 45.0f ? 0.20f : 0.12f);
    learnedChargeRatePercentPerHour =
        learnedChargeObservations == 0
            ? ratePercentPerHour
            : (learnedChargeRatePercentPerHour * (1.0f - alpha)) + (ratePercentPerHour * alpha);

    if (learnedChargeObservations < UINT16_MAX) {
        learnedChargeObservations++;
    }

    saveLearningProfile();
    lastLearningSaveMillis = now;
}

String BatteryMonitor::getBatteryStatus() {
    if (usbOnlyPower || (usbPowerPresent && !hasReading)) {
        return "USB Only";
    }

    float voltage = getBatteryVoltage();
    const bool critical = isCriticalBattery();
    const bool low = isLowBattery();

    if (critical) {
        return voltage < BATTERY_EMPTY ? "Empty" : "Critical";
    }
    if (low) {
        return "Low";
    }
    
    if (voltage >= BATTERY_FULL) {
        return "Full";
    } else if (voltage >= BATTERY_GOOD) {
        return "Good";        // 4.0V+ - Reliable ESP32 operation
    } else if (voltage >= BATTERY_NOMINAL) {
        return "Fair";        // 3.8V+ - Normal operation
    } else if (voltage >= BATTERY_EMPTY) {
        return "Fair";
    } else {
        return "Empty";       // Below lower discharge threshold
    }
}

bool BatteryMonitor::isCharging() {
    if (usbOnlyPower || (usbPowerPresent && !hasReading)) {
        return true;
    }
    if (!hasReading) {
        update();
    }

    return chargingState == "charging_likely" || chargingState == "usb_present" || chargingState == "usb_only";
}

bool BatteryMonitor::isLowBattery() {
    if (usbOnlyPower || (usbPowerPresent && !hasReading) || !hasValidReading()) {
        return false;
    }
    if (fuelGaugeSocAvailable() && fuelGaugeStateOfCharge <= lowBatteryPercentThreshold()) {
        return true;
    }
    return getBatteryVoltage() <= lowBatteryVoltageThreshold();
}

bool BatteryMonitor::isCriticalBattery() {
    if (usbOnlyPower || (usbPowerPresent && !hasReading) || !hasValidReading()) {
        return false;
    }
    if (fuelGaugeSocAvailable() && fuelGaugeStateOfCharge <= criticalShutdownPercent) {
        return true;
    }
    return getBatteryVoltage() <= criticalShutdownVoltage;
}

int BatteryMonitor::getBatterySegments() {
    int percentage = getBatteryPercentage();
    
    // Convert percentage to 3-segment display
    if (percentage >= 75) {
        return 3;  // Full battery - 3 segments
    } else if (percentage >= 50) {
        return 2;  // Good battery - 2 segments  
    } else if (percentage >= 25) {
        return 1;  // Low battery - 1 segment
    } else {
        return 0;  // Critical battery - empty/flashing
    }
}

void BatteryMonitor::calibrateVoltage(float actualVoltage) {
    float measuredVoltage = readRawVoltage() - calibrationOffset;  // Get uncalibrated reading
    if (fuelGaugeAvailable && lastVoltage > 0.1f) {
        measuredVoltage = lastVoltage - calibrationOffset;
    }
    calibrationOffset = actualVoltage - measuredVoltage;
    
    // Save calibration
    preferences.begin("battery", false);
    saveCalibration();
    preferences.end();

    hasReading = false;
    lastUpdate = 0;
    update();
    
    Serial.printf("Battery calibrated: offset = %.3fV\n", calibrationOffset);
}

void BatteryMonitor::setBatteryCapacityMah(uint16_t capacityMah) {
    uint16_t constrainedCapacity = constrain(capacityMah,
                                             MIN_BATTERY_CAPACITY_MAH,
                                             MAX_BATTERY_CAPACITY_MAH);
    if (constrainedCapacity == batteryCapacityMah) {
        return;
    }

    batteryCapacityMah = constrainedCapacity;
    preferences.begin("battery", false);
    saveCapacitySetting();
    preferences.end();

    Serial.printf("Battery capacity setting updated: %u mAh\n", batteryCapacityMah);
}

void BatteryMonitor::setCriticalShutdownEnabled(bool enabled) {
    if (enabled == criticalShutdownEnabled) {
        return;
    }

    criticalShutdownEnabled = enabled;
    preferences.begin("battery", false);
    saveSafetySettings();
    preferences.end();
    Serial.printf("Battery critical shutdown %s\n", criticalShutdownEnabled ? "enabled" : "disabled");
}

void BatteryMonitor::setCriticalShutdownVoltage(float voltage) {
    const float constrainedVoltage = constrain(voltage,
                                               MIN_CRITICAL_SHUTDOWN_VOLTAGE,
                                               MAX_CRITICAL_SHUTDOWN_VOLTAGE);
    if (fabsf(constrainedVoltage - criticalShutdownVoltage) < 0.001f) {
        return;
    }

    criticalShutdownVoltage = constrainedVoltage;
    preferences.begin("battery", false);
    saveSafetySettings();
    preferences.end();
    Serial.printf("Battery critical shutdown voltage updated: %.2fV\n", criticalShutdownVoltage);
}

void BatteryMonitor::setCriticalShutdownPercent(uint8_t percent) {
    const uint8_t constrainedPercent = constrain(percent,
                                                 MIN_CRITICAL_SHUTDOWN_PERCENT,
                                                 MAX_CRITICAL_SHUTDOWN_PERCENT);
    if (constrainedPercent == criticalShutdownPercent) {
        return;
    }

    criticalShutdownPercent = constrainedPercent;
    preferences.begin("battery", false);
    saveSafetySettings();
    preferences.end();
    Serial.printf("Battery critical shutdown percent updated: %u%%\n", criticalShutdownPercent);
}

bool BatteryMonitor::shouldForceCriticalSleep() {
#if WMBP_DISABLE_CRITICAL_BATTERY_SLEEP
    return false;
#endif
    if (!criticalShutdownEnabled) {
        return false;
    }
    if (!hasReading) {
        update();
    }
    if (!hasReading) {
        return false;
    }

    const float voltage = getBatteryVoltage();
    if (voltage <= 0.1f) {
        return false;
    }

    // Boards with real USB-power detection should not force sleep while USB is
    // present; let them charge and keep the web/USB diagnostics reachable.
    if (usbPowerPresent) {
        return false;
    }

    if (fuelGaugeAvailable && fuelGaugeStateOfCharge >= 0.0f &&
        fuelGaugeStateOfCharge <= criticalShutdownPercent) {
        return true;
    }

    return voltage <= criticalShutdownVoltage;
}

bool BatteryMonitor::hasRecoveredFromCriticalSleep() {
    if (usbPowerPresent || usbOnlyPower) {
        return true;
    }
    if (!hasReading) {
        update();
    }
    if (!hasReading) {
        return false;
    }

    if (fuelGaugeSocAvailable() && fuelGaugeStateOfCharge < getCriticalRecoveryPercent()) {
        return false;
    }

    return getBatteryVoltage() >= getCriticalRecoveryVoltage();
}

float BatteryMonitor::getCriticalRecoveryVoltage() const {
    return criticalShutdownVoltage + CRITICAL_RECOVERY_MARGIN_VOLTAGE;
}

uint8_t BatteryMonitor::getCriticalRecoveryPercent() const {
    const uint16_t threshold = static_cast<uint16_t>(criticalShutdownPercent) + CRITICAL_RECOVERY_MARGIN_PERCENT;
    return static_cast<uint8_t>(threshold > 100U ? 100U : threshold);
}

void BatteryMonitor::loadCalibration() {
    calibrationOffset = preferences.getFloat("cal_offset", 0.0f);
    Serial.printf("Battery calibration loaded: offset = %.3fV\n", calibrationOffset);
}

void BatteryMonitor::saveCalibration() {
    preferences.putFloat("cal_offset", calibrationOffset);
    Serial.println("Battery calibration saved");
}

void BatteryMonitor::loadCapacitySetting() {
    batteryCapacityMah = preferences.getUShort("capMah", DEFAULT_BATTERY_CAPACITY_MAH);
    batteryCapacityMah = constrain(batteryCapacityMah,
                                   MIN_BATTERY_CAPACITY_MAH,
                                   MAX_BATTERY_CAPACITY_MAH);
    Serial.printf("Battery capacity loaded: %u mAh\n", batteryCapacityMah);
}

void BatteryMonitor::saveCapacitySetting() {
    preferences.putUShort("capMah", batteryCapacityMah);
    Serial.println("Battery capacity saved");
}

void BatteryMonitor::loadSafetySettings() {
    criticalShutdownEnabled = preferences.getBool("critEn", true);
    criticalShutdownVoltage = preferences.getFloat("critVolt", DEFAULT_CRITICAL_SHUTDOWN_VOLTAGE);
    criticalShutdownPercent = preferences.getUChar("critPct", DEFAULT_CRITICAL_SHUTDOWN_PERCENT);

    criticalShutdownVoltage = constrain(criticalShutdownVoltage,
                                        MIN_CRITICAL_SHUTDOWN_VOLTAGE,
                                        MAX_CRITICAL_SHUTDOWN_VOLTAGE);
    criticalShutdownPercent = constrain(criticalShutdownPercent,
                                        MIN_CRITICAL_SHUTDOWN_PERCENT,
                                        MAX_CRITICAL_SHUTDOWN_PERCENT);

    Serial.printf("Battery safety loaded: criticalShutdown=%s voltage=%.2fV percent=%u%%\n",
                  criticalShutdownEnabled ? "enabled" : "disabled",
                  criticalShutdownVoltage,
                  criticalShutdownPercent);
}

void BatteryMonitor::saveSafetySettings() {
    preferences.putBool("critEn", criticalShutdownEnabled);
    preferences.putFloat("critVolt", criticalShutdownVoltage);
    preferences.putUChar("critPct", criticalShutdownPercent);
    Serial.println("Battery safety settings saved");
}

void BatteryMonitor::loadLearningProfile() {
    learnedDischargeRatePercentPerHour = preferences.getFloat("ld_rate", 0.0f);
    learnedChargeRatePercentPerHour = preferences.getFloat("lc_rate", 0.0f);
    learnedDischargeObservations = preferences.getUShort("ld_obs", 0);
    learnedChargeObservations = preferences.getUShort("lc_obs", 0);

    if (learnedDischargeRatePercentPerHour < LEARNING_MIN_DISCHARGE_RATE_PERCENT_PER_HOUR ||
        learnedDischargeRatePercentPerHour > LEARNING_MAX_DISCHARGE_RATE_PERCENT_PER_HOUR) {
        learnedDischargeRatePercentPerHour = 0.0f;
        learnedDischargeObservations = 0;
    }
    if (learnedChargeRatePercentPerHour < LEARNING_MIN_CHARGE_RATE_PERCENT_PER_HOUR ||
        learnedChargeRatePercentPerHour > LEARNING_MAX_CHARGE_RATE_PERCENT_PER_HOUR) {
        learnedChargeRatePercentPerHour = 0.0f;
        learnedChargeObservations = 0;
    }

    Serial.printf("Battery learning loaded: discharge=%.3f%%/h (%u obs) charge=%.3f%%/h (%u obs) confidence=%s\n",
                  learnedDischargeRatePercentPerHour,
                  learnedDischargeObservations,
                  learnedChargeRatePercentPerHour,
                  learnedChargeObservations,
                  getBatteryLearningConfidence().c_str());
}

void BatteryMonitor::saveLearningProfile() {
    preferences.begin("battery", false);
    preferences.putFloat("ld_rate", learnedDischargeRatePercentPerHour);
    preferences.putFloat("lc_rate", learnedChargeRatePercentPerHour);
    preferences.putUShort("ld_obs", learnedDischargeObservations);
    preferences.putUShort("lc_obs", learnedChargeObservations);
    preferences.end();

    Serial.printf("Battery learning saved: discharge=%.3f%%/h (%u obs) charge=%.3f%%/h (%u obs) confidence=%s\n",
                  learnedDischargeRatePercentPerHour,
                  learnedDischargeObservations,
                  learnedChargeRatePercentPerHour,
                  learnedChargeObservations,
                  getBatteryLearningConfidence().c_str());
}

float BatteryMonitor::lowBatteryVoltageThreshold() const {
    const float threshold = criticalShutdownVoltage + 0.15f;
    return threshold > BATTERY_LOW ? threshold : BATTERY_LOW;
}

uint8_t BatteryMonitor::lowBatteryPercentThreshold() const {
    const uint16_t threshold = static_cast<uint16_t>(criticalShutdownPercent) + 10U;
    return static_cast<uint8_t>(threshold > 100U ? 100U : threshold);
}

bool BatteryMonitor::fuelGaugeSocAvailable() const {
    return fuelGaugeAvailable && fuelGaugeStateOfCharge >= 0.0f;
}
