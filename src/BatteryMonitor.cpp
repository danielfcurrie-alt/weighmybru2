#include "BatteryMonitor.h"

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
    lastUpdate = 0;
}

void BatteryMonitor::begin() {
    Serial.println("Initializing Battery Monitor...");
    
    // Configure ADC pin and settings
    pinMode(batteryPin, INPUT);
    analogReadResolution(12);  // Use 12-bit resolution (0-4095)
    analogSetAttenuation(ADC_11db);  // 0-3.3V range for better accuracy
    
    // Load calibration from preferences
    preferences.begin("battery", false);
    loadCalibration();
    preferences.end();
    
    // Take initial reading
    update();
    
    Serial.printf("Battery Monitor initialized on GPIO%d\n", batteryPin);
    Serial.printf("Initial voltage: %.2fV (%d%%)\n", getBatteryVoltage(), getBatteryPercentage());
}

void BatteryMonitor::update() {
    unsigned long currentTime = millis();
    
    // Limit update frequency to reduce noise
    if (hasReading && currentTime - lastUpdate < UPDATE_INTERVAL) {
        return;
    }
    
    float newVoltage = readRawVoltage();
    int newRawPercentage = voltageToPercentage(newVoltage);
    
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

float BatteryMonitor::readRawVoltage() {
    // Take multiple readings for accuracy
    int totalReading = 0;
    const int samples = 10;
    
    for (int i = 0; i < samples; i++) {
        totalReading += analogRead(batteryPin);
        delayMicroseconds(100);  // Small delay between readings
    }
    
    int avgReading = totalReading / samples;
    
    // Convert ADC reading to voltage
    float voltage = ((float)avgReading / ADC_RESOLUTION) * ADC_REFERENCE * VOLTAGE_DIVIDER_RATIO;
    
    // Apply calibration offset
    voltage += calibrationOffset;
    
    return voltage;
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

    if (estimatedRuntimeMinutes < 0.0f) {
        return -1;
    }

    return (int)roundf(estimatedRuntimeMinutes);
}

String BatteryMonitor::getRuntimeEstimateConfidence() const {
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

    if (estimatedMinutesTo80 < 0.0f) {
        return -1;
    }

    return (int)roundf(estimatedMinutesTo80);
}

int BatteryMonitor::getEstimatedMinutesTo100() {
    if (!hasReading) {
        update();
    }

    if (estimatedMinutesTo100 < 0.0f) {
        return -1;
    }

    return (int)roundf(estimatedMinutesTo100);
}

String BatteryMonitor::getChargeEstimateConfidence() const {
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

int BatteryMonitor::quantizePercentage(int percentage) const {
    percentage = constrain(percentage, 0, 100);
    return constrain(((percentage + (PERCENT_VISIBLE_STEP / 2)) / PERCENT_VISIBLE_STEP) * PERCENT_VISIBLE_STEP, 0, 100);
}

void BatteryMonitor::updateRuntimeEstimate() {
    const unsigned long now = millis();
    const float currentPercentage = constrain(smoothedPercentage, 0.0f, 100.0f);

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
        chargeEstimateConfidence = "discharging";
        chargingState = "discharging";
        return;
    }

    const unsigned long elapsedMillis = now - chargeStartMillis;
    const float elapsedMinutes = elapsedMillis / 60000.0f;

    if (percentChange < CHARGE_DETECTION_DELTA_PERCENT) {
        estimatedMinutesTo80 = -1.0f;
        estimatedMinutesTo100 = -1.0f;
        chargeRatePercentPerHour = 0.0f;
        chargeEstimateConfidence = "learning";
        chargingState = dischargeRatePercentPerHour > 0.0f ? "discharging" : "unknown";
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

    return chargingState == "charging_likely";
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
