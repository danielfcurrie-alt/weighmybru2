#ifndef BOARD_HARDWARE_H
#define BOARD_HARDWARE_H

#include <Arduino.h>
#include <Preferences.h>

enum class BoardHardwareStatus : uint8_t {
    Booting,
    Idle,
    Connected,
    Charging,
    LowBattery,
    CriticalBattery,
    Error
};

class BoardHardware {
public:
    void begin();
    void updateStatus(BoardHardwareStatus status);
    void prepareForSleep();

    bool hasRgbStatusLed() const { return rgbStatusLedAvailable; }
    bool isRgbStatusLedEnabled() const { return rgbStatusLedEnabled; }
    uint8_t getRgbStatusLedBrightness() const { return rgbStatusLedBrightness; }
    void setRgbStatusLedEnabled(bool enabled);
    void setRgbStatusLedBrightness(uint8_t brightness);

    bool hasAntennaSwitch() const { return antennaSwitchAvailable; }
    bool isExternalAntennaSelected() const { return externalAntennaSelected; }
    void setExternalAntenna(bool external);

    String toJson() const;

private:
    Preferences preferences;
    bool initialized = false;
    bool rgbStatusLedAvailable = false;
    bool rgbStatusLedEnabled = false;
    uint8_t rgbStatusLedBrightness = 8;
    bool antennaSwitchAvailable = false;
    bool externalAntennaSelected = false;
    BoardHardwareStatus lastStatus = BoardHardwareStatus::Booting;
    uint32_t lastStatusWriteMillis = 0;

    void loadSettings();
    void saveSettings();
    uint8_t dimBrightness() const;
    void writeStatusLed(uint8_t red, uint8_t green, uint8_t blue);
    const char* statusName(BoardHardwareStatus status) const;
};

#endif
