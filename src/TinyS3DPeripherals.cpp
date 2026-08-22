#include "TinyS3DPeripherals.h"

#if defined(BOARD_TINYS3D)

#include "Version.h"

#include <WiFi.h>

namespace {
const char* pageName(Waveshare147UI::Page page) {
    switch (page) {
        case Waveshare147UI::Page::Flow: return "flow";
        case Waveshare147UI::Page::Connect: return "connect";
        case Waveshare147UI::Page::Status: return "status";
        case Waveshare147UI::Page::Weight:
        default: return "weight";
    }
}

bool pourOverVisible(const PourOverSession& session) {
    return session.hasRecipe() &&
        (session.status() == PourOverStatus::Running ||
         session.status() == PourOverStatus::Paused ||
         session.status() == PourOverStatus::Finished);
}
}

bool TinyS3DPeripherals::begin() {
    startedMs = millis();
    beginAccelerometer();
    beginDisplayAndTouch();
    Serial.printf(
        "TinyS3D peripherals: display=%s touch=%s lis2dw12=%s\n",
        displayReady ? "ready" : "missing",
        touchReady ? "ready" : "missing",
        accelerometerReady ? "ready" : "missing");
    return displayReady;
}

void TinyS3DPeripherals::beginAccelerometer() {
    LIS2DW12Driver::Config config;
    config.outputDataRate = LIS2DW12Driver::OutputDataRate::Hz100;
    config.fullScale = LIS2DW12Driver::FullScale::G4;
    config.interruptPin = TINYS3D_LIS_INT_PIN;
    config.routeDataReadyToInterrupt = false;
    config.lowNoise = true;
    accelerometerReady = accelerometer.begin(config);
    Serial.printf("LIS2DW12 %s at 0x%02X id=0x%02X mode=polling\n",
                  accelerometerReady ? "detected" : "not found",
                  LIS2DW12Driver::DEFAULT_ADDRESS,
                  accelerometer.deviceId());
}

void TinyS3DPeripherals::beginDisplayAndTouch() {
    JD9853DisplayDriver::Config displayConfig;
    displayConfig.sclkPin = TINYS3D_LCD_SCLK_PIN;
    displayConfig.mosiPin = TINYS3D_LCD_MOSI_PIN;
    displayConfig.misoPin = TINYS3D_LCD_MISO_PIN;
    displayConfig.chipSelectPin = TINYS3D_LCD_CS_PIN;
    displayConfig.dataCommandPin = TINYS3D_LCD_DC_PIN;
    displayConfig.resetPin = TINYS3D_LCD_RST_PIN;
    displayConfig.backlightPin = TINYS3D_LCD_BL_PIN;
    displayConfig.spiFrequencyHz = 40000000;
    displayConfig.rotation = 0;
    displayReady = display.begin(displayConfig) &&
                   ui.begin(WEIGHMYBRU_VERSION_STRING);

    AXS5106TouchDriver::Config touchConfig;
    touchConfig.resetPin = TINYS3D_TOUCH_RST_PIN;
    touchConfig.interruptPin = TINYS3D_TOUCH_INT_PIN;
    touchConfig.rotation = 0;
    touchReady = touch.begin(touchConfig);
}

void TinyS3DPeripherals::update(
    float weightGrams, float flowGramsPerSecond, bool scaleConnected,
    bool bluetoothConnected, BatteryMonitor& batteryMonitor,
    const PourOverSession& pourOverSession) {
    const uint32_t nowMs = millis();
    updateAccelerometer(nowMs);
    updateTouch(nowMs);
    updateDisplay(nowMs, weightGrams, flowGramsPerSecond, scaleConnected,
                  bluetoothConnected, batteryMonitor, pourOverSession);
}

void TinyS3DPeripherals::updateAccelerometer(uint32_t nowMs) {
    if (!accelerometerReady ||
        nowMs - lastAccelerometerPollMs < ACCELEROMETER_INTERVAL_MS) {
        return;
    }
    lastAccelerometerPollMs = nowMs;
    if (!accelerometer.dataReady()) {
        return;
    }

    LIS2DW12Driver::Sample acceleration;
    if (!accelerometer.readAcceleration(acceleration)) {
        return;
    }

    MotionAnalyzer::Sample sample;
    sample.xG = acceleration.xG;
    sample.yG = acceleration.yG;
    sample.zG = acceleration.zG;
    sample.timestampMs = acceleration.timestampMs;
    motionAnalyzer.update(sample);

    if (nowMs - lastMotionReportMs >= MOTION_REPORT_INTERVAL_MS) {
        lastMotionReportMs = nowMs;
        const MotionAnalyzer::Diagnostics& diagnostics =
            motionAnalyzer.diagnostics();
        Serial.printf(
            "TINY_MOTION state=%s samples=%lu rms_g=%.4f energy_g2=%.5f "
            "quiet=%.3f impact_g=%.3f roll=%.1f pitch=%.1f impacts=%lu "
            "taps=%lu double=%lu\n",
            MotionAnalyzer::stateName(diagnostics.state),
            static_cast<unsigned long>(diagnostics.sampleCount),
            diagnostics.vibrationRmsG,
            diagnostics.vibrationEnergyG2,
            diagnostics.quietConfidence,
            diagnostics.impactPeakG,
            diagnostics.rollDegrees,
            diagnostics.pitchDegrees,
            static_cast<unsigned long>(diagnostics.impactCount),
            static_cast<unsigned long>(diagnostics.tapCandidateCount),
            static_cast<unsigned long>(diagnostics.doubleTapCandidateCount));
    }
}

void TinyS3DPeripherals::updateTouch(uint32_t nowMs) {
    if (!touchReady || nowMs - lastTouchPollMs < TOUCH_POLL_INTERVAL_MS) {
        return;
    }
    lastTouchPollMs = nowMs;

    AXS5106TouchDriver::Frame frame;
    if (!touch.read(frame)) {
        return;
    }
    if (frame.count == 0) {
        touchDown = false;
        return;
    }
    if (touchDown) {
        return;
    }
    touchDown = true;

    const uint16_t x = frame.points[0].x;
    const uint16_t y = frame.points[0].y;
    if (y < Waveshare147UI::HEIGHT - 48) {
        return;
    }
    if (x < 43) {
        selectedPage = Waveshare147UI::Page::Weight;
    } else if (x < 86) {
        selectedPage = Waveshare147UI::Page::Flow;
    } else if (x < 129) {
        selectedPage = Waveshare147UI::Page::Connect;
    } else {
        selectedPage = Waveshare147UI::Page::Status;
    }
    manualPageUntilMs = nowMs + MANUAL_PAGE_HOLD_MS;
    Serial.printf("Tiny display page=%s touch=%u,%u\n",
                  pageName(selectedPage), x, y);
}

void TinyS3DPeripherals::updateDisplay(
    uint32_t nowMs, float weightGrams, float flowGramsPerSecond,
    bool scaleConnected, bool bluetoothConnected, BatteryMonitor& batteryMonitor,
    const PourOverSession& pourOverSession) {
    if (!displayReady || nowMs - startedMs < SPLASH_HOLD_MS ||
        nowMs - lastDisplayRenderMs < DISPLAY_RENDER_INTERVAL_MS) {
        return;
    }
    lastDisplayRenderMs = nowMs;
    ui.pushFlowSample(flowGramsPerSecond);

    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    String appUrl = "http://192.168.4.1/pourover.html";
    if (wifiConnected) {
        appUrl = "http://" + WiFi.localIP().toString() + "/pourover.html";
    }

    Waveshare147UI::DashboardData data;
    data.weightGrams = weightGrams;
    data.flowGramsPerSecond = flowGramsPerSecond;
    data.timerMillis = pourOverSession.totalElapsedMs();
    data.batteryPercent = batteryMonitor.getBatteryPercentage();
    data.timerRunning = pourOverSession.status() == PourOverStatus::Running;
    data.wifiConnected = wifiConnected;
    data.bluetoothConnected = bluetoothConnected;
    data.scaleConnected = scaleConnected;
    data.fuelGaugeAlert = batteryMonitor.isFuelGaugeAlertAsserted();
    data.fuelGaugeRatePercentPerHour =
        batteryMonitor.getFuelGaugeChargeRatePercentPerHour();
    data.ipAddress = appUrl.c_str();
    data.statusText = MotionAnalyzer::stateName(motionAnalyzer.diagnostics().state);

    const bool manualPageActive =
        static_cast<int32_t>(manualPageUntilMs - nowMs) > 0;
    if (!manualPageActive && pourOverVisible(pourOverSession)) {
        ui.renderPourOver(pourOverSession, data);
    } else {
        ui.render(selectedPage, data, appUrl.c_str());
    }
    if (!ui.present()) {
        Serial.println("WARNING: TinyS3D display frame write failed");
    }
}

void TinyS3DPeripherals::prepareForSleep() {
    if (!display.isInitialized()) {
        return;
    }
    display.setBacklight(false);
    display.setDisplayEnabled(false);
}

#endif  // BOARD_TINYS3D
