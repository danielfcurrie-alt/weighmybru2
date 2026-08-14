#include "BoardHardware.h"
#include "BoardConfig.h"

namespace {
#if defined(BOARD_TYPE_TINYS3D)
constexpr uint8_t MIN_STATUS_LED_BRIGHTNESS = 1;
constexpr uint8_t MAX_STATUS_LED_BRIGHTNESS = 64;
#endif
}

void BoardHardware::begin() {
    loadSettings();

#if defined(BOARD_TYPE_TINYS3D)
    antennaSwitchAvailable = true;
    pinMode(RF_ANTENNA_SWITCH_PIN, OUTPUT);
    digitalWrite(RF_ANTENNA_SWITCH_PIN, externalAntennaSelected ? HIGH : LOW);

    rgbStatusLedAvailable = true;
    pinMode(RGB_PWR, OUTPUT);
    digitalWrite(RGB_PWR, LOW);
    if (rgbStatusLedEnabled) {
        writeStatusLed(dimBrightness(), dimBrightness(), rgbStatusLedBrightness);
    }
#else
    antennaSwitchAvailable = false;
    rgbStatusLedAvailable = false;
#endif

    initialized = true;
    Serial.printf("Board hardware: rgbStatusLed=%s enabled=%s brightness=%u antennaSwitch=%s externalAntenna=%s\n",
                  rgbStatusLedAvailable ? "true" : "false",
                  rgbStatusLedEnabled ? "true" : "false",
                  rgbStatusLedBrightness,
                  antennaSwitchAvailable ? "true" : "false",
                  externalAntennaSelected ? "true" : "false");
}

void BoardHardware::updateStatus(BoardHardwareStatus status) {
    if (!initialized) {
        return;
    }

    const uint32_t now = millis();
    if (status == lastStatus && now - lastStatusWriteMillis < 5000) {
        return;
    }

    lastStatus = status;
    lastStatusWriteMillis = now;

    if (!rgbStatusLedAvailable || !rgbStatusLedEnabled) {
        return;
    }

#if defined(BOARD_TYPE_TINYS3D)
    const uint8_t dim = dimBrightness();
    const uint8_t med = rgbStatusLedBrightness;
    switch (status) {
        case BoardHardwareStatus::Booting:
            writeStatusLed(0, 0, med);
            break;
        case BoardHardwareStatus::Idle:
            writeStatusLed(dim, dim, dim);
            break;
        case BoardHardwareStatus::Connected:
            writeStatusLed(0, med, 0);
            break;
        case BoardHardwareStatus::Charging:
            writeStatusLed(0, dim, med);
            break;
        case BoardHardwareStatus::LowBattery:
            writeStatusLed(med, dim, 0);
            break;
        case BoardHardwareStatus::CriticalBattery:
        case BoardHardwareStatus::Error:
            writeStatusLed(med, 0, 0);
            break;
    }
#endif
}

void BoardHardware::prepareForSleep() {
    if (rgbStatusLedAvailable) {
        writeStatusLed(0, 0, 0);
#if defined(BOARD_TYPE_TINYS3D)
        digitalWrite(RGB_PWR, LOW);
#endif
    }
}

void BoardHardware::setRgbStatusLedEnabled(bool enabled) {
    rgbStatusLedEnabled = enabled;
    saveSettings();
    if (!enabled) {
        writeStatusLed(0, 0, 0);
#if defined(BOARD_TYPE_TINYS3D)
        digitalWrite(RGB_PWR, LOW);
#endif
    } else {
        lastStatusWriteMillis = 0;
        updateStatus(lastStatus);
    }
}

void BoardHardware::setRgbStatusLedBrightness(uint8_t brightness) {
#if defined(BOARD_TYPE_TINYS3D)
    const uint8_t constrainedBrightness = constrain(brightness, MIN_STATUS_LED_BRIGHTNESS, MAX_STATUS_LED_BRIGHTNESS);
#else
    const uint8_t constrainedBrightness = brightness;
#endif
    if (constrainedBrightness == rgbStatusLedBrightness) {
        return;
    }

    rgbStatusLedBrightness = constrainedBrightness;
    saveSettings();
    lastStatusWriteMillis = 0;
    updateStatus(lastStatus);
}

void BoardHardware::setExternalAntenna(bool external) {
    if (!antennaSwitchAvailable) {
        return;
    }

    externalAntennaSelected = external;
#if defined(BOARD_TYPE_TINYS3D)
    digitalWrite(RF_ANTENNA_SWITCH_PIN, externalAntennaSelected ? HIGH : LOW);
#endif
    saveSettings();
}

String BoardHardware::toJson() const {
    String json = "{";
    json += "\"board\":\"" + String(BOARD_NAME) + "\"";
    json += ",\"rgb_status_led_available\":" + String(rgbStatusLedAvailable ? "true" : "false");
    json += ",\"rgb_status_led_enabled\":" + String(rgbStatusLedEnabled ? "true" : "false");
    json += ",\"rgb_status_led_brightness\":" + String(rgbStatusLedBrightness);
    json += ",\"antenna_switch_available\":" + String(antennaSwitchAvailable ? "true" : "false");
    json += ",\"external_antenna_selected\":" + String(externalAntennaSelected ? "true" : "false");
    json += ",\"status\":\"" + String(statusName(lastStatus)) + "\"";
    json += "}";
    return json;
}

void BoardHardware::loadSettings() {
    if (!preferences.begin("boardhw", true)) {
        return;
    }
    rgbStatusLedEnabled = preferences.getBool("rgb_led", false);
    rgbStatusLedBrightness = preferences.getUChar("rgb_bright", 8);
#if defined(BOARD_TYPE_TINYS3D)
    rgbStatusLedBrightness = constrain(rgbStatusLedBrightness, MIN_STATUS_LED_BRIGHTNESS, MAX_STATUS_LED_BRIGHTNESS);
#endif
    externalAntennaSelected = preferences.getBool("ext_ant", false);
    preferences.end();
}

void BoardHardware::saveSettings() {
    if (!preferences.begin("boardhw", false)) {
        return;
    }
    preferences.putBool("rgb_led", rgbStatusLedEnabled);
    preferences.putUChar("rgb_bright", rgbStatusLedBrightness);
    preferences.putBool("ext_ant", externalAntennaSelected);
    preferences.end();
}

uint8_t BoardHardware::dimBrightness() const {
#if defined(BOARD_TYPE_TINYS3D)
    return max<uint8_t>(MIN_STATUS_LED_BRIGHTNESS, rgbStatusLedBrightness / 3);
#else
    return 0;
#endif
}

void BoardHardware::writeStatusLed(uint8_t red, uint8_t green, uint8_t blue) {
#if defined(BOARD_TYPE_TINYS3D)
    if (!rgbStatusLedAvailable) {
        return;
    }
    if (red == 0 && green == 0 && blue == 0) {
        neopixelWrite(RGB_BUILTIN, 0, 0, 0);
        digitalWrite(RGB_PWR, LOW);
        return;
    }

    digitalWrite(RGB_PWR, HIGH);
    delayMicroseconds(350);
    neopixelWrite(RGB_BUILTIN, red, green, blue);
#else
    (void)red;
    (void)green;
    (void)blue;
#endif
}

const char* BoardHardware::statusName(BoardHardwareStatus status) const {
    switch (status) {
        case BoardHardwareStatus::Booting: return "booting";
        case BoardHardwareStatus::Idle: return "idle";
        case BoardHardwareStatus::Connected: return "connected";
        case BoardHardwareStatus::Charging: return "charging";
        case BoardHardwareStatus::LowBattery: return "low_battery";
        case BoardHardwareStatus::CriticalBattery: return "critical_battery";
        case BoardHardwareStatus::Error: return "error";
        default: return "unknown";
    }
}
