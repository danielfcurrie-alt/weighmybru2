#ifndef TINYS3D_PERIPHERALS_H
#define TINYS3D_PERIPHERALS_H

#include <Arduino.h>

#include "BoardConfig.h"

#if defined(BOARD_TINYS3D)

#include "BatteryMonitor.h"
#include "LIS2DW12Driver.h"
#include "MotionAnalyzer.h"
#include "PourOverSession.h"
#include "Waveshare147Touch.h"
#include "Waveshare147UI.h"

class TinyS3DPeripherals {
public:
    bool begin();
    void update(float weightGrams, float flowGramsPerSecond,
                bool scaleConnected, bool bluetoothConnected,
                BatteryMonitor& batteryMonitor,
                const PourOverSession& pourOverSession);
    void prepareForSleep();

    bool isDisplayReady() const { return displayReady; }
    bool isTouchReady() const { return touchReady; }
    bool isAccelerometerReady() const { return accelerometerReady; }
    const MotionAnalyzer::Diagnostics& motionDiagnostics() const {
        return motionAnalyzer.diagnostics();
    }

private:
    static constexpr uint32_t ACCELEROMETER_INTERVAL_MS = 10;
    static constexpr uint32_t MOTION_REPORT_INTERVAL_MS = 5000;
    static constexpr uint32_t TOUCH_POLL_INTERVAL_MS = 20;
    static constexpr uint32_t DISPLAY_RENDER_INTERVAL_MS = 250;
    static constexpr uint32_t SPLASH_HOLD_MS = 1500;
    static constexpr uint32_t MANUAL_PAGE_HOLD_MS = 5000;

    LIS2DW12Driver accelerometer;
    MotionAnalyzer motionAnalyzer;
    JD9853DisplayDriver display;
    AXS5106TouchDriver touch;
    Waveshare147UI ui{display};

    Waveshare147UI::Page selectedPage = Waveshare147UI::Page::Weight;
    bool accelerometerReady = false;
    bool displayReady = false;
    bool touchReady = false;
    bool touchDown = false;
    uint32_t startedMs = 0;
    uint32_t lastAccelerometerPollMs = 0;
    uint32_t lastMotionReportMs = 0;
    uint32_t lastTouchPollMs = 0;
    uint32_t lastDisplayRenderMs = 0;
    uint32_t manualPageUntilMs = 0;

    void beginAccelerometer();
    void beginDisplayAndTouch();
    void updateAccelerometer(uint32_t nowMs);
    void updateTouch(uint32_t nowMs);
    void updateDisplay(uint32_t nowMs, float weightGrams,
                       float flowGramsPerSecond, bool scaleConnected,
                       bool bluetoothConnected, BatteryMonitor& batteryMonitor,
                       const PourOverSession& pourOverSession);
};

#endif  // BOARD_TINYS3D
#endif  // TINYS3D_PERIPHERALS_H
