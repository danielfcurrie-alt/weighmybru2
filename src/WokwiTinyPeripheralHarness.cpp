#include "WokwiTinyPeripheralHarness.h"

#if defined(BOARD_TINYS3D) && WMBP_WOKWI_RUNTIME_HARNESS && WMBP_TINY_WOKWI_PERIPHERAL_HARNESS

#include "Version.h"

#include <math.h>
#include <string.h>

#ifndef WMBP_TINY_WOKWI_LIS_INT_PIN
#define WMBP_TINY_WOKWI_LIS_INT_PIN TINYS3D_LIS_INT_PIN
#endif

#ifndef WMBP_TINY_WOKWI_LIS_USE_INTERRUPT
#define WMBP_TINY_WOKWI_LIS_USE_INTERRUPT 0
#endif

#ifndef WMBP_TINY_WOKWI_LCD_SCLK_PIN
#define WMBP_TINY_WOKWI_LCD_SCLK_PIN TINYS3D_LCD_SCLK_PIN
#define WMBP_TINY_WOKWI_LCD_MOSI_PIN TINYS3D_LCD_MOSI_PIN
#define WMBP_TINY_WOKWI_LCD_MISO_PIN TINYS3D_LCD_MISO_PIN
#define WMBP_TINY_WOKWI_LCD_CS_PIN TINYS3D_LCD_CS_PIN
#define WMBP_TINY_WOKWI_LCD_DC_PIN TINYS3D_LCD_DC_PIN
#define WMBP_TINY_WOKWI_LCD_RST_PIN TINYS3D_LCD_RST_PIN
#define WMBP_TINY_WOKWI_LCD_BL_PIN TINYS3D_LCD_BL_PIN
#define WMBP_TINY_WOKWI_TOUCH_RST_PIN TINYS3D_TOUCH_RST_PIN
#define WMBP_TINY_WOKWI_TOUCH_INT_PIN TINYS3D_TOUCH_INT_PIN
#endif

namespace {
constexpr uint32_t ACCELEROMETER_INTERVAL_MS = 10;
constexpr uint32_t MOTION_REPORT_INTERVAL_MS = 1000;
constexpr uint32_t BATTERY_REPORT_INTERVAL_MS = 1000;

#if WMBP_TINY_WOKWI_COLOR_UI
const char* pageName(Waveshare147UI::Page page) {
    switch (page) {
        case Waveshare147UI::Page::Flow: return "flow";
        case Waveshare147UI::Page::Connect: return "connect";
        case Waveshare147UI::Page::Status: return "status";
        case Waveshare147UI::Page::Weight:
        default: return "weight";
    }
}
#endif
}

void WokwiTinyPeripheralHarness::begin(BatteryMonitor& batteryMonitor) {
    LIS2DW12Driver::Config config;
    config.outputDataRate = LIS2DW12Driver::OutputDataRate::Hz100;
    config.fullScale = LIS2DW12Driver::FullScale::G4;
    config.interruptPin = WMBP_TINY_WOKWI_LIS_INT_PIN;
    config.routeDataReadyToInterrupt =
        WMBP_TINY_WOKWI_LIS_USE_INTERRUPT && WMBP_TINY_WOKWI_LIS_INT_PIN >= 0;
    config.lowNoise = true;

    accelerometerReady = accelerometer.begin(config);
    const bool rangesReady = accelerometerReady && verifyAccelerationRanges(config);
    Serial.printf("WMBP_TINY_LIS,connected=%u,id=0x%02X,reset=%s,ranges=%s,int=%d\n",
                  accelerometerReady ? 1U : 0U,
                  accelerometer.deviceId(),
                  accelerometerReady ? "pass" : "fail",
                  rangesReady ? "pass" : "fail",
                  WMBP_TINY_WOKWI_LIS_INT_PIN);
    if (accelerometerReady) {
        Serial.println("WMBP_TINY_LIS_ID_RESET_OK");
    }
    if (rangesReady) {
        Serial.println("WMBP_TINY_LIS_RANGES_OK");
    }

    reportBattery(millis(), batteryMonitor);

#if WMBP_TINY_WOKWI_COLOR_UI
    beginColorUi();
#endif
}

void WokwiTinyPeripheralHarness::update(float weightGrams,
                                        float flowGramsPerSecond,
                                        bool freshScaleSample,
                                        BatteryMonitor& batteryMonitor) {
    const uint32_t nowMs = millis();
    updateAccelerometer(nowMs);
    reportBattery(nowMs, batteryMonitor);
#if WMBP_TINY_WOKWI_COLOR_UI
    updateColorUi(nowMs, weightGrams, flowGramsPerSecond,
                  freshScaleSample, batteryMonitor);
#else
    (void)weightGrams;
    (void)flowGramsPerSecond;
    (void)freshScaleSample;
#endif
}

bool WokwiTinyPeripheralHarness::verifyAccelerationRanges(
    const LIS2DW12Driver::Config& finalConfig) {
    const LIS2DW12Driver::FullScale ranges[] = {
        LIS2DW12Driver::FullScale::G2,
        LIS2DW12Driver::FullScale::G4,
        LIS2DW12Driver::FullScale::G8,
        LIS2DW12Driver::FullScale::G16,
    };
    bool passed = true;
    for (const LIS2DW12Driver::FullScale range : ranges) {
        LIS2DW12Driver::Config config = finalConfig;
        config.fullScale = range;
        LIS2DW12Driver::Sample sample;
        if (!accelerometer.configure(config) || !accelerometer.readAcceleration(sample) ||
            fabsf(sample.zG - 1.0f) > 0.03f) {
            passed = false;
        }
    }
    return accelerometer.configure(finalConfig) && passed;
}

void WokwiTinyPeripheralHarness::updateAccelerometer(uint32_t nowMs) {
    if (!accelerometerReady || nowMs - lastAccelerometerPollMs < ACCELEROMETER_INTERVAL_MS) {
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
    if (!dataReadyReported) {
        dataReadyReported = true;
        Serial.println("WMBP_TINY_LIS_DATA_READY_OK");
    }

    MotionAnalyzer::Sample motionSample;
    motionSample.xG = acceleration.xG;
    motionSample.yG = acceleration.yG;
    motionSample.zG = acceleration.zG;
    motionSample.timestampMs = acceleration.timestampMs;
    motionAnalyzer.update(motionSample);
    const MotionAnalyzer::Diagnostics& diagnostics = motionAnalyzer.diagnostics();

    if (!quietReported && diagnostics.state == MotionAnalyzer::State::Quiet &&
        diagnostics.sampleCount >= 25 && diagnostics.quietConfidence >= 0.75f) {
        quietReported = true;
        Serial.println("WMBP_TINY_MOTION_QUIET_OK");
    }
    if (!activeReported && diagnostics.state == MotionAnalyzer::State::Active &&
        diagnostics.vibrationRmsG >= 0.10f) {
        activeReported = true;
        Serial.println("WMBP_TINY_MOTION_VIBRATION_OK");
    }
    if (diagnostics.impactCount > reportedImpactCount) {
        reportedImpactCount = diagnostics.impactCount;
        Serial.println("WMBP_TINY_MOTION_IMPACT_OK");
    }
    if (diagnostics.tapCandidateCount > reportedTapCount) {
        reportedTapCount = diagnostics.tapCandidateCount;
        Serial.println("WMBP_TINY_MOTION_KNOCK_OK");
    }
    if (diagnostics.doubleTapCandidateCount > reportedDoubleTapCount) {
        reportedDoubleTapCount = diagnostics.doubleTapCandidateCount;
        Serial.println("WMBP_TINY_MOTION_DOUBLE_TAP_OK");
    }

    if (nowMs - lastMotionReportMs >= MOTION_REPORT_INTERVAL_MS) {
        lastMotionReportMs = nowMs;
        Serial.printf(
            "WMBP_TINY_MOTION,state=%s,samples=%lu,rms_g=%.4f,energy_g2=%.5f,"
            "quiet=%.3f,impact_g=%.3f,roll=%.2f,pitch=%.2f,impacts=%lu,taps=%lu,double=%lu\n",
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

void WokwiTinyPeripheralHarness::reportBattery(
    uint32_t nowMs, BatteryMonitor& batteryMonitor) {
    if (lastBatteryReportMs != 0 && nowMs - lastBatteryReportMs < BATTERY_REPORT_INTERVAL_MS) {
        return;
    }
    lastBatteryReportMs = nowMs;

    if (!batteryMonitor.hasFuelGauge()) {
        if (!missingGaugeReported) {
            missingGaugeReported = true;
            Serial.println("WMBP_TINY_MAX17048_MISSING_OK");
        }
        Serial.printf("WMBP_TINY_BATTERY,backend=%s,gauge=0\n",
                      batteryMonitor.getBatteryBackend().c_str());
        return;
    }

    const float rate = batteryMonitor.getFuelGaugeChargeRatePercentPerHour();
    const bool alert = batteryMonitor.isFuelGaugeAlertAsserted();
    Serial.printf(
        "WMBP_TINY_BATTERY,backend=%s,gauge=1,version=0x%04X,voltage=%.3f,soc=%.2f,"
        "rate_pct_hr=%.2f,alert=%u,status=0x%04X,errors=%lu\n",
        batteryMonitor.getBatteryBackend().c_str(),
        batteryMonitor.getFuelGaugeVersion(),
        batteryMonitor.getBatteryVoltage(),
        batteryMonitor.getFuelGaugeStateOfCharge(),
        rate,
        alert ? 1U : 0U,
        batteryMonitor.getFuelGaugeStatus(),
        static_cast<unsigned long>(batteryMonitor.getFuelGaugeCommunicationErrors()));

    if (!dischargeReported && rate <= -0.5f) {
        dischargeReported = true;
        Serial.println("WMBP_TINY_MAX17048_DISCHARGE_OK");
    }
    if (!chargeReported && rate >= 0.5f) {
        chargeReported = true;
        Serial.println("WMBP_TINY_MAX17048_CHARGE_OK");
    }
    if (!alertReported && alert) {
        alertReported = true;
        Serial.println("WMBP_TINY_MAX17048_ALERT_OK");
    }
}

#if WMBP_TINY_WOKWI_COLOR_UI

void WokwiTinyPeripheralHarness::beginColorUi() {
    JD9853DisplayDriver::Config displayConfig;
    displayConfig.sclkPin = WMBP_TINY_WOKWI_LCD_SCLK_PIN;
    displayConfig.mosiPin = WMBP_TINY_WOKWI_LCD_MOSI_PIN;
    displayConfig.misoPin = WMBP_TINY_WOKWI_LCD_MISO_PIN;
    displayConfig.chipSelectPin = WMBP_TINY_WOKWI_LCD_CS_PIN;
    displayConfig.dataCommandPin = WMBP_TINY_WOKWI_LCD_DC_PIN;
    displayConfig.resetPin = WMBP_TINY_WOKWI_LCD_RST_PIN;
    displayConfig.backlightPin = WMBP_TINY_WOKWI_LCD_BL_PIN;
    displayConfig.spiFrequencyHz = 20000000;

    displayReady = display.begin(displayConfig) && ui.begin(WEIGHMYBRU_VERSION_STRING);
    displayStartedMs = millis();
    if (displayReady) {
        Serial.printf("WMBP_TINY_DISPLAY_SPLASH_OK,checksum=0x%08lX,psram=%u\n",
                      static_cast<unsigned long>(ui.frameChecksum()),
                      ui.isUsingPsram() ? 1U : 0U);
    } else {
        Serial.println("WMBP_TINY_DISPLAY_INIT_FAIL");
    }

    AXS5106TouchDriver::Config touchConfig;
    touchConfig.resetPin = WMBP_TINY_WOKWI_TOUCH_RST_PIN;
    touchConfig.interruptPin = WMBP_TINY_WOKWI_TOUCH_INT_PIN;
    touchReady = touch.begin(touchConfig);
    Serial.printf("WMBP_TINY_TOUCH,connected=%u\n", touchReady ? 1U : 0U);

    PourOverRecipe recipe;
    strncpy(recipe.name, "Wokwi three-stage", sizeof(recipe.name) - 1);
    recipe.stageCount = 3;
    recipe.stages[0].type = PourOverStageType::Pour;
    strncpy(recipe.stages[0].name, "Bloom", sizeof(recipe.stages[0].name) - 1);
    recipe.stages[0].targetGrams = 45.0f;
    recipe.stages[0].flowMin = 2.0f;
    recipe.stages[0].flowMax = 6.0f;
    recipe.stages[0].autoAdvance = false;
    recipe.stages[1].type = PourOverStageType::Pause;
    strncpy(recipe.stages[1].name, "Bloom pause", sizeof(recipe.stages[1].name) - 1);
    recipe.stages[1].durationMs = 30000;
    recipe.stages[2].type = PourOverStageType::Pour;
    strncpy(recipe.stages[2].name, "Main pour", sizeof(recipe.stages[2].name) - 1);
    recipe.stages[2].targetGrams = 180.0f;
    recipe.stages[2].flowMin = 3.0f;
    recipe.stages[2].flowMax = 7.0f;
    demoPourOver.setRecipe(recipe);
    demoPourOver.start(millis(), 0.0f);
}

void WokwiTinyPeripheralHarness::updateColorUi(
    uint32_t nowMs, float weightGrams, float flowGramsPerSecond,
    bool freshScaleSample, BatteryMonitor& batteryMonitor) {
    if (!displayReady) {
        return;
    }

    if (touchReady && nowMs - lastTouchPollMs >= 20) {
        lastTouchPollMs = nowMs;
        AXS5106TouchDriver::Frame frame;
        if (touch.read(frame)) {
            if (frame.count > 0 && !touchDown) {
                touchDown = true;
                const uint16_t x = frame.points[0].x;
                showPourOver = false;
                if (x < 43) {
                    selectedPage = Waveshare147UI::Page::Weight;
                } else if (x < 86) {
                    selectedPage = Waveshare147UI::Page::Flow;
                } else if (x < 129) {
                    selectedPage = Waveshare147UI::Page::Connect;
                } else {
                    selectedPage = Waveshare147UI::Page::Status;
                }
                touchOverrideUntilMs = nowMs + 3000;
                Serial.printf("WMBP_TINY_TOUCH_PAGE,%s,x=%u,y=%u\n",
                              pageName(selectedPage), x, frame.points[0].y);
            } else if (frame.count == 0) {
                touchDown = false;
            }
        }
    }

    demoPourOver.update(nowMs, weightGrams, flowGramsPerSecond, freshScaleSample);
    if (nowMs - lastDisplayRenderMs < 1000) {
        return;
    }
    lastDisplayRenderMs = nowMs;
    ui.pushFlowSample(flowGramsPerSecond);

    if (static_cast<int32_t>(nowMs - touchOverrideUntilMs) >= 0) {
        const uint32_t ageMs = nowMs - displayStartedMs;
        showPourOver = ageMs >= 12000;
        if (ageMs < 3000) {
            selectedPage = Waveshare147UI::Page::Weight;
        } else if (ageMs < 6000) {
            selectedPage = Waveshare147UI::Page::Flow;
        } else if (ageMs < 9000) {
            selectedPage = Waveshare147UI::Page::Connect;
        } else if (ageMs < 12000) {
            selectedPage = Waveshare147UI::Page::Status;
        }
    }

    Waveshare147UI::DashboardData data;
    data.weightGrams = weightGrams;
    data.flowGramsPerSecond = flowGramsPerSecond;
    data.timerMillis = nowMs - displayStartedMs;
    data.batteryPercent = batteryMonitor.getBatteryPercentage();
    data.timerRunning = true;
    data.scaleConnected = true;
    data.fuelGaugeAlert = batteryMonitor.isFuelGaugeAlertAsserted();
    data.fuelGaugeRatePercentPerHour =
        batteryMonitor.getFuelGaugeChargeRatePercentPerHour();
    data.ipAddress = "http://192.168.4.1/pourover.html";
    data.statusText = "WOKWI TINY PROXY";

    const char* checkpoint = nullptr;
    uint8_t checkpointBit = 0;
    if (showPourOver) {
        ui.renderPourOver(demoPourOver, data);
        checkpoint = "pourover";
        checkpointBit = 1U << 4;
    } else {
        ui.render(selectedPage, data, data.ipAddress);
        checkpoint = pageName(selectedPage);
        checkpointBit = 1U << static_cast<uint8_t>(selectedPage);
    }
    reportDisplayCheckpoint(checkpoint, checkpointBit, ui.present());
}

void WokwiTinyPeripheralHarness::reportDisplayCheckpoint(
    const char* name, uint8_t bit, bool presented) {
    if ((displayCheckpointMask & bit) != 0) {
        return;
    }
    displayCheckpointMask |= bit;
    Serial.printf("WMBP_TINY_DISPLAY_%s_%s,checksum=0x%08lX\n",
                  name,
                  presented ? "OK" : "FAIL",
                  static_cast<unsigned long>(ui.frameChecksum()));
}

#endif  // WMBP_TINY_WOKWI_COLOR_UI
#endif  // BOARD_TINYS3D && WMBP_WOKWI_RUNTIME_HARNESS && WMBP_TINY_WOKWI_PERIPHERAL_HARNESS
