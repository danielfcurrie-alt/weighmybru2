#ifndef WOKWI_TINY_PERIPHERAL_HARNESS_H
#define WOKWI_TINY_PERIPHERAL_HARNESS_H

#include "BoardConfig.h"

#if defined(BOARD_TINYS3D) && WMBP_WOKWI_RUNTIME_HARNESS && WMBP_TINY_WOKWI_PERIPHERAL_HARNESS

#include "BatteryMonitor.h"
#include "LIS2DW12Driver.h"
#include "MotionAnalyzer.h"

#if WMBP_TINY_WOKWI_COLOR_UI
#include "PourOverSession.h"
#include "Waveshare147Touch.h"
#include "Waveshare147UI.h"
#endif

class WokwiTinyPeripheralHarness {
public:
    void begin(BatteryMonitor& batteryMonitor);
    void update(float weightGrams, float flowGramsPerSecond,
                bool freshScaleSample, BatteryMonitor& batteryMonitor);

private:
    LIS2DW12Driver accelerometer;
    MotionAnalyzer motionAnalyzer;
    bool accelerometerReady = false;
    bool dataReadyReported = false;
    bool quietReported = false;
    bool activeReported = false;
    uint32_t lastAccelerometerPollMs = 0;
    uint32_t lastMotionReportMs = 0;
    uint32_t reportedImpactCount = 0;
    uint32_t reportedTapCount = 0;
    uint32_t reportedDoubleTapCount = 0;
    uint32_t lastBatteryReportMs = 0;
    bool dischargeReported = false;
    bool chargeReported = false;
    bool alertReported = false;
    bool missingGaugeReported = false;

#if WMBP_TINY_WOKWI_COLOR_UI
    JD9853DisplayDriver display;
    AXS5106TouchDriver touch;
    Waveshare147UI ui{display};
    PourOverSession demoPourOver;
    Waveshare147UI::Page selectedPage = Waveshare147UI::Page::Weight;
    bool showPourOver = false;
    bool displayReady = false;
    bool touchReady = false;
    bool touchDown = false;
    uint32_t displayStartedMs = 0;
    uint32_t lastDisplayRenderMs = 0;
    uint32_t lastTouchPollMs = 0;
    uint32_t touchOverrideUntilMs = 0;
    uint8_t displayCheckpointMask = 0;

    void beginColorUi();
    void updateColorUi(uint32_t nowMs, float weightGrams,
                       float flowGramsPerSecond, bool freshScaleSample,
                       BatteryMonitor& batteryMonitor);
    void reportDisplayCheckpoint(const char* name, uint8_t bit, bool presented);
#endif

    bool verifyAccelerationRanges(const LIS2DW12Driver::Config& finalConfig);
    void updateAccelerometer(uint32_t nowMs);
    void reportBattery(uint32_t nowMs, BatteryMonitor& batteryMonitor);
};

#endif  // BOARD_TINYS3D && WMBP_WOKWI_RUNTIME_HARNESS && WMBP_TINY_WOKWI_PERIPHERAL_HARNESS
#endif
