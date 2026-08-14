#include "BatteryMonitor.h"
#include <Wire.h>

BatteryMonitor::BatteryMonitor(uint8_t batteryPin) : batteryPin(batteryPin) {
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
#if HAS_USB_POWER_SENSE
    usbPowerPresent = readUsbPowerPresent();
#endif

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
        return false;
    }
#endif

#if HAS_ADC_BATTERY
    voltage = readRawVoltage();
    if (voltage <= 0.1f) {
        return false;
    }
    fuelGaugeStateOfCharge = -1.0f;
    percentage = voltageToPercentage(voltage);
    return true;
#else
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
    Wire.beginTransmission(FUEL_GAUGE_MAX17048_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("MAX17048 fuel gauge not found at 0x%02X\n", FUEL_GAUGE_MAX17048_ADDR);
        return false;
    }

    uint16_t version = 0;
    if (!readFuelGaugeRegister16(0x08, version)) {
        Serial.println("MAX17048 fuel gauge detected but version read failed");
        return false;
    }

    Serial.printf("MAX17048 fuel gauge detected at 0x%02X version=0x%04X\n",
                  FUEL_GAUGE_MAX17048_ADDR,
                  version);
    return true;
#else
    return false;
#endif
}

bool BatteryMonitor::readFuelGaugeSnapshot(float& voltage, float& stateOfCharge) {
#if HAS_I2C_FUEL_GAUGE
    uint16_t vcell = 0;
    uint16_t soc = 0;

    if (!readFuelGaugeRegister16(0x02, vcell) ||
        !readFuelGaugeRegister16(0x04, soc)) {
        return false;
    }

    // MAX17048 VCELL is a 12-bit value left-aligned in the 16-bit register.
    // Each 12-bit count is 1.25 mV, equivalently the raw 16-bit register is
    // 78.125 uV/LSB because the low nibble is fractional/unused.
    voltage = static_cast<float>(vcell >> 4) * 0.00125f;
    const uint8_t socInteger = (soc >> 8) & 0xFF;
    const uint8_t socFraction = soc & 0xFF;
    stateOfCharge = static_cast<float>(socInteger) + (static_cast<float>(socFraction) / 256.0f);

    return voltage >= 2.0f && voltage <= 5.0f && stateOfCharge >= 0.0f && stateOfCharge <= 110.0f;
#else
    return false;
#endif
}

bool BatteryMonitor::readFuelGaugeRegister16(uint8_t reg, uint16_t& value) {
#if HAS_I2C_FUEL_GAUGE
    Wire.beginTransmission(FUEL_GAUGE_MAX17048_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    const uint8_t bytesRead = Wire.requestFrom(static_cast<uint8_t>(FUEL_GAUGE_MAX17048_ADDR),
                                               static_cast<uint8_t>(2));
    if (bytesRead != 2 || Wire.available() < 2) {
        return false;
    }

    const uint8_t msb = Wire.read();
    const uint8_t lsb = Wire.read();
    value = (static_cast<uint16_t>(msb) << 8) | lsb;
    return true;
#else
    (void)reg;
    (void)value;
    return false;
#endif
}

bool BatteryMonitor::readUsbPowerPresent() {
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

    return quantizePercentage((int)roundf(smoothedPercentage));
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

    return chargingState;
}

int BatteryMonitor::getEstimatedMinutesTo80() {
    if (!hasReading) {
        update();
    }

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
    float voltage = getBatteryVoltage();
    
    if (voltage >= BATTERY_FULL) {
        return "Full";
    } else if (voltage >= BATTERY_GOOD) {
        return "Good";        // 4.0V+ - Reliable ESP32 operation
    } else if (voltage >= BATTERY_NOMINAL) {
        return "Fair";        // 3.8V+ - Normal operation
    } else if (voltage >= BATTERY_LOW) {
        return "Low";         // 3.6V+ - Consider charging
    } else if (voltage >= BATTERY_CRITICAL) {
        return "Critical";    // 3.4V+ - Charge immediately
    } else {
        return "Empty";       // <3.4V - Below critical threshold
    }
}

bool BatteryMonitor::isCharging() {
    if (!hasReading) {
        update();
    }

    return chargingState == "charging_likely" || chargingState == "usb_present";
}

bool BatteryMonitor::isLowBattery() {
    return getBatteryVoltage() < BATTERY_LOW;
}

bool BatteryMonitor::isCriticalBattery() {
    return getBatteryVoltage() < BATTERY_CRITICAL;
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
    
    Serial.printf("Battery calibrated: offset = %.3fV\n", calibrationOffset);
}

void BatteryMonitor::loadCalibration() {
    calibrationOffset = preferences.getFloat("cal_offset", 0.0f);
    Serial.printf("Battery calibration loaded: offset = %.3fV\n", calibrationOffset);
}

void BatteryMonitor::saveCalibration() {
    preferences.putFloat("cal_offset", calibrationOffset);
    Serial.println("Battery calibration saved");
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
