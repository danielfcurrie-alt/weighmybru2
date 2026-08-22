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

    void setTinyPeripheralStatus(bool colorDisplay, bool touch,
                                 bool accelerometer);
    void updateTinyMotionDiagnostics(const char* state, float vibrationRmsG,
                                     float vibrationEnergyG2,
                                     float quietConfidence, float impactPeakG,
                                     float rollDegrees, float pitchDegrees,
                                     uint32_t impacts, uint32_t taps,
                                     uint32_t doubleTaps);
    bool hasTinyColorDisplay() const { return tinyColorDisplayAvailable; }
    bool hasTinyTouch() const { return tinyTouchAvailable; }
    bool hasTinyAccelerometer() const { return tinyAccelerometerAvailable; }
    const char* getTinyMotionState() const { return tinyMotionState; }
    float getTinyVibrationRmsG() const { return tinyVibrationRmsG; }
    float getTinyVibrationEnergyG2() const { return tinyVibrationEnergyG2; }
    float getTinyQuietConfidence() const { return tinyQuietConfidence; }
    float getTinyImpactPeakG() const { return tinyImpactPeakG; }
    float getTinyRollDegrees() const { return tinyRollDegrees; }
    float getTinyPitchDegrees() const { return tinyPitchDegrees; }
    uint32_t getTinyImpactCount() const { return tinyImpactCount; }
    uint32_t getTinyTapCount() const { return tinyTapCount; }
    uint32_t getTinyDoubleTapCount() const { return tinyDoubleTapCount; }

    String toJson() const;

private:
    Preferences preferences;
    bool initialized = false;
    bool rgbStatusLedAvailable = false;
    bool rgbStatusLedEnabled = false;
    uint8_t rgbStatusLedBrightness = 8;
    bool antennaSwitchAvailable = false;
    bool externalAntennaSelected = false;
    bool tinyColorDisplayAvailable = false;
    bool tinyTouchAvailable = false;
    bool tinyAccelerometerAvailable = false;
    char tinyMotionState[12] = "unavailable";
    float tinyVibrationRmsG = 0.0f;
    float tinyVibrationEnergyG2 = 0.0f;
    float tinyQuietConfidence = 0.0f;
    float tinyImpactPeakG = 0.0f;
    float tinyRollDegrees = 0.0f;
    float tinyPitchDegrees = 0.0f;
    uint32_t tinyImpactCount = 0;
    uint32_t tinyTapCount = 0;
    uint32_t tinyDoubleTapCount = 0;
    BoardHardwareStatus lastStatus = BoardHardwareStatus::Booting;
    uint32_t lastStatusWriteMillis = 0;

    void loadSettings();
    void saveSettings();
    uint8_t dimBrightness() const;
    void writeStatusLed(uint8_t red, uint8_t green, uint8_t blue);
    const char* statusName(BoardHardwareStatus status) const;
};

#endif
