#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_sleep.h>
#ifdef ESP_IDF_VERSION_MAJOR
    #include "esp_wifi.h"
    #include "esp_err.h"
#endif
#include "WebServer.h"
#include "Scale.h"
#include "WiFiManager.h"
#include "FlowRate.h"
#include "Calibration.h"
#include "BluetoothScale.h"
#include "TouchSensor.h"
#include "Display.h"
#include "PowerManager.h"
#include "BatteryMonitor.h"
#include "BoardConfig.h"
#include "Version.h"
#include "SmbComms.h"

// Board-specific pin configuration
uint8_t dataPin = HX711_DATA_PIN;     // HX711 Data pin
uint8_t clockPin = HX711_CLOCK_PIN;   // HX711 Clock pin  
uint8_t touchPin = TOUCH_TARE_PIN;    // Touch sensor for tare
uint8_t sleepTouchPin = TOUCH_SLEEP_PIN;  // Touch sensor for sleep functionality
uint8_t batteryPin = BATTERY_PIN;     // Battery voltage monitoring
uint8_t sdaPin = I2C_SDA_PIN;         // I2C Data pin for display
uint8_t sclPin = I2C_SCL_PIN;         // I2C Clock pin for display
float calibrationFactor = 4195.712891;
Scale scale(dataPin, clockPin, calibrationFactor);
FlowRate flowRate;
BluetoothScale bluetoothScale;
TouchSensor touchSensor(touchPin, &scale);
Display oledDisplay(sdaPin, sclPin, &scale, &flowRate);
PowerManager powerManager(sleepTouchPin, &oledDisplay);
BatteryMonitor batteryMonitor(batteryPin);
SmbComms smbComms;

static constexpr uint32_t BATTERY_BENCH_LOG_INTERVAL_MS = 30000;
static bool batteryBenchLoggingEnabled = true;
static bool usbWeightStreamEnabled = false;
static uint32_t usbWeightDroppedFrames = 0;

enum UsbWeightStatusFlags : uint16_t {
  USB_WEIGHT_STATUS_HX711_CONNECTED = 1U << 0,
  USB_WEIGHT_STATUS_BLE_CONNECTED = 1U << 1,
  USB_WEIGHT_STATUS_RECENT_BUMP = 1U << 2,
  USB_WEIGHT_STATUS_RECENT_GLITCH = 1U << 3,
  USB_WEIGHT_STATUS_ZERO_CLAMPED = 1U << 4,
  USB_WEIGHT_STATUS_AUTO_ZERO_ACTIVE = 1U << 5,
  USB_WEIGHT_STATUS_BATTERY_VALID = 1U << 6,
  USB_WEIGHT_STATUS_CHARGING = 1U << 7,
  USB_WEIGHT_STATUS_WIFI_RADIO_ON = 1U << 8
};

static const char* boolText(bool value) {
  return value ? "true" : "false";
}

static const char* wifiModeName(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_OFF: return "off";
    case WIFI_STA: return "sta";
    case WIFI_AP: return "ap";
    case WIFI_AP_STA: return "ap_sta";
    default: return "unknown";
  }
}

static void printBatteryBenchmarkLog(bool force = false) {
  static uint32_t lastLogMillis = 0;
  static uint32_t lastScaleSequence = 0;
  static uint32_t lastExtendedNotifyCount = 0;
  static uint32_t lastFloat32NotifyCount = 0;
  static uint32_t lastBatteryNotifyCount = 0;

  const uint32_t now = millis();
  if (!force && !batteryBenchLoggingEnabled) {
    return;
  }
  if (!force && lastLogMillis != 0 && now - lastLogMillis < BATTERY_BENCH_LOG_INTERVAL_MS) {
    return;
  }
  if (!force && lastLogMillis == 0 && now < BATTERY_BENCH_LOG_INTERVAL_MS) {
    return;
  }

  float elapsedSeconds = lastLogMillis == 0
      ? now / 1000.0f
      : (now - lastLogMillis) / 1000.0f;
  if (elapsedSeconds <= 0.0f) {
    elapsedSeconds = 1.0f;
  }

  const uint32_t scaleSequence = scale.getSampleSequence();
  const uint32_t extendedNotifyCount = bluetoothScale.getExtendedWeightNotifyCount();
  const uint32_t float32NotifyCount = bluetoothScale.getFloat32NotifyCount();
  const uint32_t batteryNotifyCount = bluetoothScale.getBatteryNotifyCount();

  const float scaleHz = (scaleSequence - lastScaleSequence) / elapsedSeconds;
  const float extendedNotifyHz = (extendedNotifyCount - lastExtendedNotifyCount) / elapsedSeconds;
  const float float32NotifyHz = (float32NotifyCount - lastFloat32NotifyCount) / elapsedSeconds;
  const float batteryNotifyHz = (batteryNotifyCount - lastBatteryNotifyCount) / elapsedSeconds;

  const String chargingState = batteryMonitor.getChargingState();
  const String runtimeConfidence = batteryMonitor.getRuntimeEstimateConfidence();
  const String chargeConfidence = batteryMonitor.getChargeEstimateConfidence();
  const String hx711Mode = scale.getDetectedHx711RateMode();
  const bool wifiEnabled = isWiFiEnabled();
  const wifi_mode_t wifiMode = WiFi.getMode();

  Serial.printf(
      "BATTERY_BENCH ms=%lu uptimeMin=%.1f voltage=%.3f percent=%d rawPercent=%d valid=%s "
      "chargingState=%s charging=%s runtimeMin=%d runtimeConfidence=%s observationMin=%d dischargePctPerHour=%.3f "
      "chargeTo80Min=%d chargeTo100Min=%d chargeConfidence=%s chargeObservationMin=%d chargePctPerHour=%.3f "
      "learnedDischargePctPerHour=%.3f learnedChargePctPerHour=%.3f learnedDischargeObs=%u learnedChargeObs=%u learningConfidence=%s "
      "wifiEnabled=%s wifiMode=%s wifiSleep=%s bleConnected=%s display=%s hx711=%s hx711Hz=%.2f hx711Mode=%s "
      "scaleHz=%.2f extendedNotifyHz=%.2f float32NotifyHz=%.2f batteryNotifyHz=%.2f "
      "sampleSequence=%lu extendedNotifies=%lu float32Notifies=%lu batteryNotifies=%lu heap=%lu psram=%lu\n",
      static_cast<unsigned long>(now),
      now / 60000.0f,
      batteryMonitor.getBatteryVoltage(),
      batteryMonitor.getBatteryPercentage(),
      batteryMonitor.getRawBatteryPercentage(),
      boolText(batteryMonitor.hasValidReading()),
      chargingState.c_str(),
      boolText(batteryMonitor.isCharging()),
      batteryMonitor.getEstimatedRuntimeMinutesRemaining(),
      runtimeConfidence.c_str(),
      batteryMonitor.getRuntimeObservationMinutes(),
      batteryMonitor.getDischargeRatePercentPerHour(),
      batteryMonitor.getEstimatedMinutesTo80(),
      batteryMonitor.getEstimatedMinutesTo100(),
      chargeConfidence.c_str(),
      batteryMonitor.getChargeObservationMinutes(),
      batteryMonitor.getChargeRatePercentPerHour(),
      batteryMonitor.getLearnedDischargeRatePercentPerHour(),
      batteryMonitor.getLearnedChargeRatePercentPerHour(),
      batteryMonitor.getLearnedDischargeObservations(),
      batteryMonitor.getLearnedChargeObservations(),
      batteryMonitor.getBatteryLearningConfidence().c_str(),
      boolText(wifiEnabled),
      wifiModeName(wifiMode),
      boolText(WiFi.getSleep()),
      boolText(bluetoothScale.isConnected()),
      boolText(oledDisplay.isConnected()),
      boolText(scale.isHX711Connected()),
      scale.getDetectedSampleRateHz(),
      hx711Mode.c_str(),
      scaleHz,
      extendedNotifyHz,
      float32NotifyHz,
      batteryNotifyHz,
      static_cast<unsigned long>(scaleSequence),
      static_cast<unsigned long>(extendedNotifyCount),
      static_cast<unsigned long>(float32NotifyCount),
      static_cast<unsigned long>(batteryNotifyCount),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getFreePsram()));

  lastLogMillis = now;
  lastScaleSequence = scaleSequence;
  lastExtendedNotifyCount = extendedNotifyCount;
  lastFloat32NotifyCount = float32NotifyCount;
  lastBatteryNotifyCount = batteryNotifyCount;
}

static void printUsbWeightStreamHeader() {
  Serial.println("WMBP_WEIGHT_V1_HEADER,ms,seq,weight_g,flow_gps,status,quality,battery_pct,hx711_hz,dropped");
}

static uint16_t usbWeightStatusFlags() {
  uint16_t status = 0;

  if (scale.isHX711Connected()) {
    status |= USB_WEIGHT_STATUS_HX711_CONNECTED;
  }
  if (bluetoothScale.isConnected()) {
    status |= USB_WEIGHT_STATUS_BLE_CONNECTED;
  }
  if (scale.hasRecentBump()) {
    status |= USB_WEIGHT_STATUS_RECENT_BUMP;
  }
  if (scale.hasRecentGlitch()) {
    status |= USB_WEIGHT_STATUS_RECENT_GLITCH;
  }
  if (scale.isZeroClamped()) {
    status |= USB_WEIGHT_STATUS_ZERO_CLAMPED;
  }
  if (scale.isAutoZeroActive()) {
    status |= USB_WEIGHT_STATUS_AUTO_ZERO_ACTIVE;
  }
  if (batteryMonitor.hasValidReading()) {
    status |= USB_WEIGHT_STATUS_BATTERY_VALID;
  }
  if (batteryMonitor.isCharging()) {
    status |= USB_WEIGHT_STATUS_CHARGING;
  }
  if (WiFi.getMode() != WIFI_OFF) {
    status |= USB_WEIGHT_STATUS_WIFI_RADIO_ON;
  }

  return status;
}

static void printUsbWeightSample(float weight, bool force = false) {
  if (!force && !usbWeightStreamEnabled) {
    return;
  }

  char line[160];
  const int length = snprintf(
      line,
      sizeof(line),
      "WMBP_WEIGHT_V1,%lu,%lu,%.3f,%.3f,0x%04X,%u,%d,%.2f,%lu\n",
      static_cast<unsigned long>(millis()),
      static_cast<unsigned long>(scale.getSampleSequence()),
      weight,
      flowRate.getFlowRate(),
      usbWeightStatusFlags(),
      scale.getScaleQualityScore(),
      batteryMonitor.getBatteryPercentage(),
      scale.getDetectedSampleRateHz(),
      static_cast<unsigned long>(usbWeightDroppedFrames));

  if (length <= 0 || length >= static_cast<int>(sizeof(line))) {
    usbWeightDroppedFrames++;
    return;
  }

  if (!force && Serial.availableForWrite() < length) {
    usbWeightDroppedFrames++;
    return;
  }

  Serial.write(reinterpret_cast<const uint8_t*>(line), length);
}

static void printConfigDiagnostics() {
  Serial.printf("========== %s diagnostics ==========\n", WMB_PLUS_FIRMWARE_NAME);
  Serial.printf("Version: %s\n", WEIGHMYBRU_FULL_VERSION);
  Serial.printf("Board: %s\n", WEIGHMYBRU_BOARD_NAME);
  Serial.printf("Flash Size: %dMB\n", FLASH_SIZE_MB);
  Serial.printf("HX711 DOUT GPIO%u, SCK GPIO%u\n", dataPin, clockPin);
  Serial.printf("Touch tare GPIO%u, sleep GPIO%u\n", touchPin, sleepTouchPin);
  Serial.printf("Battery ADC GPIO%u voltage=%.3fV percent=%d rawPercent=%d valid=%s\n",
                batteryPin,
                batteryMonitor.getBatteryVoltage(),
                batteryMonitor.getBatteryPercentage(),
                batteryMonitor.getRawBatteryPercentage(),
                batteryMonitor.hasValidReading() ? "true" : "false");
  Serial.printf("Battery runtime estimate: minutes=%d confidence=%s observation=%dmin discharge=%.3f%%/h\n",
                batteryMonitor.getEstimatedRuntimeMinutesRemaining(),
                batteryMonitor.getRuntimeEstimateConfidence().c_str(),
                batteryMonitor.getRuntimeObservationMinutes(),
                batteryMonitor.getDischargeRatePercentPerHour());
  Serial.printf("Battery charge estimate: state=%s charging=%s to80=%dmin to100=%dmin confidence=%s observation=%dmin charge=%.3f%%/h\n",
                batteryMonitor.getChargingState().c_str(),
                batteryMonitor.isCharging() ? "true" : "false",
                batteryMonitor.getEstimatedMinutesTo80(),
                batteryMonitor.getEstimatedMinutesTo100(),
                batteryMonitor.getChargeEstimateConfidence().c_str(),
                batteryMonitor.getChargeObservationMinutes(),
                batteryMonitor.getChargeRatePercentPerHour());
  Serial.printf("Battery learned profile: confidence=%s discharge=%.3f%%/h (%u obs) charge=%.3f%%/h (%u obs)\n",
                batteryMonitor.getBatteryLearningConfidence().c_str(),
                batteryMonitor.getLearnedDischargeRatePercentPerHour(),
                batteryMonitor.getLearnedDischargeObservations(),
                batteryMonitor.getLearnedChargeRatePercentPerHour(),
                batteryMonitor.getLearnedChargeObservations());
  Serial.printf("Scale connected=%s calibration=%.6f\n",
                scale.isHX711Connected() ? "true" : "false",
                scale.getCalibrationFactor());
  Serial.printf("Scale sampleSequence=%lu lastSampleMs=%lu detectedRate=%.2fHz mode=%s avgInterval=%luus min=%luus max=%luus longGaps=%lu stats=%lu\n",
                static_cast<unsigned long>(scale.getSampleSequence()),
                static_cast<unsigned long>(scale.getLastSampleMillis()),
                scale.getDetectedSampleRateHz(),
                scale.getDetectedHx711RateMode().c_str(),
                static_cast<unsigned long>(scale.getSampleIntervalAverageMicros()),
                static_cast<unsigned long>(scale.getSampleIntervalMinMicros()),
                static_cast<unsigned long>(scale.getSampleIntervalMaxMicros()),
                static_cast<unsigned long>(scale.getSampleIntervalLongGapCount()),
                static_cast<unsigned long>(scale.getSampleIntervalStatsCount()));
  Serial.printf("Scale quality=%u lifetimeQuality=%u bumps=%lu glitches=%lu lastBumpMs=%lu lastBump=%.2fg lastGlitchMs=%lu lastGlitch=%.2fg lifetimeSamples=%lu lifetimeGaps=%lu lifetimeBumps=%lu lifetimeGlitches=%lu\n",
                scale.getScaleQualityScore(),
                scale.getLifetimeQualityScore(),
                static_cast<unsigned long>(scale.getBumpCount()),
                static_cast<unsigned long>(scale.getGlitchCount()),
                static_cast<unsigned long>(scale.getLastBumpMillis()),
                scale.getLastBumpMagnitudeGrams(),
                static_cast<unsigned long>(scale.getLastGlitchMillis()),
                scale.getLastGlitchMagnitudeGrams(),
                static_cast<unsigned long>(scale.getLifetimeSampleCount()),
                static_cast<unsigned long>(scale.getLifetimeLongGapCount()),
                static_cast<unsigned long>(scale.getLifetimeBumpCount()),
                static_cast<unsigned long>(scale.getLifetimeGlitchCount()));
  Serial.printf("Filter state=%s medianSamples=%d averageSamples=%d\n",
                scale.getFilterState().c_str(),
                scale.getMedianSamples(),
                scale.getAverageSamples());
  Serial.printf("Zero stability: clamped=%s autoZero=%s correction=%.3fg\n",
                scale.isZeroClamped() ? "true" : "false",
                scale.isAutoZeroActive() ? "true" : "false",
                scale.getAutoZeroCorrectionGrams());
  bluetoothScale.printDiagnostics();
  Serial.printf("Battery benchmark serial log: enabled=%s interval=%lus\n",
                boolText(batteryBenchLoggingEnabled),
                static_cast<unsigned long>(BATTERY_BENCH_LOG_INTERVAL_MS / 1000));
  Serial.printf("USB weight stream: enabled=%s dropped=%lu format=WMBP_WEIGHT_V1\n",
                boolText(usbWeightStreamEnabled),
                static_cast<unsigned long>(usbWeightDroppedFrames));
  Serial.println("Commands: z=config diagnostics, b=toggle battery benchmark log, B=print battery benchmark now, w=toggle USB weight stream, W=print one USB weight sample");
  Serial.println("============================================");
}

static void handleSerialCommands() {
  while (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'z' || command == 'Z') {
      printConfigDiagnostics();
    } else if (command == 'b') {
      batteryBenchLoggingEnabled = !batteryBenchLoggingEnabled;
      Serial.printf("Battery benchmark serial log %s\n", batteryBenchLoggingEnabled ? "enabled" : "disabled");
    } else if (command == 'B') {
      printBatteryBenchmarkLog(true);
    } else if (command == 'w') {
      usbWeightStreamEnabled = !usbWeightStreamEnabled;
      Serial.printf("USB weight stream %s\n", usbWeightStreamEnabled ? "enabled" : "disabled");
      if (usbWeightStreamEnabled) {
        printUsbWeightStreamHeader();
      }
    } else if (command == 'W') {
      printUsbWeightStreamHeader();
      printUsbWeightSample(scale.getCurrentWeight(), true);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Set CPU frequency explicitly for power optimization
  setCpuFrequencyMhz(80);
  Serial.printf("CPU frequency set to: %dMHz for power optimization\n", getCpuFrequencyMhz());
  
  // Version and board identification
  Serial.println("=================================");
  Serial.printf("%s v%s\n", WMB_PLUS_FIRMWARE_NAME, WEIGHMYBRU_VERSION_STRING);
  Serial.printf("Board: %s\n", WEIGHMYBRU_BOARD_NAME);
  Serial.printf("Build: %s %s\n", WEIGHMYBRU_BUILD_DATE, WEIGHMYBRU_BUILD_TIME);
  Serial.printf("Full Version: %s\n", WEIGHMYBRU_FULL_VERSION);
  Serial.printf("Flash Size: %dMB\n", FLASH_SIZE_MB);
  Serial.printf("CPU Frequency: %dMHz (Power Optimized)\n", getCpuFrequencyMhz());
  Serial.println("=================================");
  
  // Link scale and flow rate for tare operation coordination
  scale.setFlowRatePtr(&flowRate);
  bluetoothScale.setTouchSensor(&touchSensor);
  bluetoothScale.setFlowRate(&flowRate);
  
  // Check for factory reset request (hold touch pin during boot)
  pinMode(touchPin, INPUT_PULLDOWN);
  if (digitalRead(touchPin) == HIGH) {
    Serial.println("FACTORY RESET: Touch pin held during boot - clearing WiFi credentials");
    clearWiFiCredentials();
    delay(1000);
  }

  // Initialize battery before BLE so the standard Battery Service can publish
  // a real value instead of a fake default.
  batteryMonitor.begin();
  bluetoothScale.setBatteryMonitor(&batteryMonitor);
  
  // CRITICAL: Initialize BLE FIRST before WiFi to prevent radio conflicts
  Serial.println("Initializing BLE FIRST for GaggiMate compatibility...");
  Serial.printf("Free heap before BLE init: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM before BLE init: %u bytes\n", ESP.getFreePsram());
  
  try {
    bluetoothScale.begin();  // Initialize BLE without scale reference
    Serial.println("BLE initialized successfully - GaggiMate should be able to connect");
    Serial.printf("Free heap after BLE init: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM after BLE init: %u bytes\n", ESP.getFreePsram());
  } catch (...) {
    Serial.println("BLE initialization failed - continuing without Bluetooth");
    Serial.printf("Free heap after BLE fail: %u bytes\n", ESP.getFreeHeap());
  }
  
  // Initialize display with error handling - don't block if display fails
  Serial.println("Initializing display...");
  bool displayAvailable = oledDisplay.begin();
  
  if (!displayAvailable) {
    Serial.println("WARNING: Display initialization failed!");
    Serial.println("System will continue in headless mode without display.");
    Serial.println("All functionality remains available via web interface.");
  } else {
    Serial.println("Display initialized - ready for visual feedback");
    // Set reduced brightness for power optimization
    oledDisplay.setBrightness(128);  // 50% brightness vs 255 max
    Serial.println("Display brightness set to 50% for power optimization");
  }
  
  // Check wake-up reason and show appropriate message
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal (touch sensor)");
      // Show the same starting message as normal boot for consistency
      delay(1500);
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup caused by external signal using RTC_CNTL");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("Wakeup caused by touchpad");
      break;
    default:
      Serial.println("Wakeup was not caused by deep sleep: " + String(wakeup_reason));
      // For normal startup, the begin() method already shows a startup message
      delay(1000);
      break;
  }
  //Wait for BLE to finish intitalizing before starting WiFi
  delay(1500); 
  
  // Initialize WiFi power management BEFORE any WiFi operations
  Serial.println("Initializing WiFi power management...");
  
  // CRITICAL: Force WiFi completely off first to ensure clean state
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000); // Allow hardware to fully reset
  
  // Debug: Uncomment the next line to force reset WiFi state for testing
  // resetWiFiEnabledState(); // DISABLED - state has been cleared
  
  // ALWAYS enable WiFi power management for optimal battery life
  WiFi.setSleep(true);
  Serial.println("WiFi power management enabled for battery optimization");
  
  // CRITICAL: Always setup WiFi first (like tare button scenario)
  // This ensures all WiFi subsystems are properly initialized
  // Then disable it cleanly if needed (replicating tare button sequence)
  Serial.println("FORCING WiFi initialization to replicate tare button scenario...");
  setupWiFiForced(); // Use forced setup to bypass state checks

  // Wait for WiFi to fully stabilize after BLE is already running
  delay(1500);
  Serial.printf("Version: %s\n", ESP.getSdkVersion());
  // Initialize scale with error handling - don't block web server if HX711 fails
  Serial.println("Initializing scale...");
  if (!scale.begin()) {
    Serial.println("WARNING: Scale (HX711) initialization failed!");
    Serial.println("Web server will continue to run, but scale readings will not be available.");
    Serial.println("Check HX711 wiring and connections.");
  } else {
    Serial.println("Scale initialized successfully");
    // Now that scale is ready, set the reference in BluetoothScale
    bluetoothScale.setScale(&scale);
  }
  
  // BLE was initialized earlier - no need to initialize again
  // bluetoothScale.begin(&scale);
  
  // Set bluetooth reference in display for status indicator (if display available)
  if (oledDisplay.isConnected()) {
    oledDisplay.setBluetoothScale(&bluetoothScale);
  }
  
  // Set display reference in bluetooth for timer control
  bluetoothScale.setDisplay(&oledDisplay);
  
  // Set power manager reference in display for timer state synchronization (if display available)
  if (oledDisplay.isConnected()) {
    oledDisplay.setPowerManager(&powerManager);
  }
  
  // Set battery monitor reference in display for battery status (if display available)
  if (oledDisplay.isConnected()) {
    oledDisplay.setBatteryMonitor(&batteryMonitor);
  }

  // Initialize touch sensor
  touchSensor.begin();

  // Initialize power manager
  powerManager.begin();
  powerManager.loadAutoSleepSettings();
  powerManager.setBeforeSleepCallback([]() {
    Serial.println("Preparing peripherals for deep sleep...");

    if (scale.isHX711Connected()) {
      scale.powerDown();
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    #ifdef ESP_IDF_VERSION_MAJOR
      esp_err_t sleepWifiStopResult = esp_wifi_stop();
      if (sleepWifiStopResult == ESP_OK) {
        Serial.println("WiFi subsystem stopped for deep sleep");
      }
    #endif
  });

  // Check for low battery - prevent boot if voltage too low
  float batteryVoltage = batteryMonitor.getBatteryVoltage();
  if (batteryVoltage < 3.2f && batteryVoltage > 0.1f) { // > 0.1f to avoid false readings
    Serial.printf("CRITICAL: Battery voltage too low (%.2fV) - entering sleep\n", batteryVoltage);
    
    // Show battery low message on display with large, centered formatting
    if (oledDisplay.isConnected()) {
      oledDisplay.showBatteryLowMessage(batteryVoltage, 3000);
    }
    
    delay(3000); // Show message for 3 seconds
    
    // Force clear any display state and sleep immediately
    if (oledDisplay.isConnected()) {
      oledDisplay.clear();
    }
    
    Serial.println("Forcing deep sleep now...");
    esp_deep_sleep_start();
  }
  
  Serial.printf("Battery voltage OK (%.2fV) - continuing boot\n", batteryVoltage);

  // Show IP addresses and welcome message if display is available
  delay(100); // Small delay to ensure WiFi is fully initialized
  if (oledDisplay.isConnected()) {
    oledDisplay.showIPAddresses();
  }

  // Link display to touch sensor for tare feedback (if display available)
  if (oledDisplay.isConnected()) {
    touchSensor.setDisplay(&oledDisplay);
  }
  
  // Link flow rate to touch sensor for averaging reset on tare
  touchSensor.setFlowRate(&flowRate);

  // Initialise StopMyBru ESP-NOW link (after WiFi is up)
  smbComms.begin();

  // Wire timer start/stop events to StopMyBru relay
  powerManager.setRelayOnCallback( [](){ smbComms.sendRelayOn();  });
  powerManager.setRelayOffCallback([](){ smbComms.sendRelayOff(); });

  setupWebServer(scale, flowRate, bluetoothScale, oledDisplay, batteryMonitor, smbComms, powerManager);
  
  // CRITICAL: After full initialization, check if WiFi should be disabled
  // This exactly replicates the tare button scenario: WiFi started, then disabled
  Serial.println("=== POST-INITIALIZATION WiFi STATE CHECK ===");
  if (!loadWiFiEnabledState()) {
    Serial.println("WiFi should be disabled - applying clean shutdown like tare button");
    Serial.println("(WiFi was initialized first, now disabling cleanly)");
    
    // Small delay to ensure all systems are stable (like tare button timing)
    delay(100);
    
    // Now call disableWiFi() exactly like tare button does
    disableWiFi();
    
    Serial.println("WiFi cleanly disabled - 0.05A power consumption expected");
  } else {
    Serial.println("WiFi should remain enabled - no action needed");
  }
}

void loop() {
  static uint32_t lastProcessedScaleSequence = 0;
  static unsigned long lastWiFiCheck = 0;
  static unsigned long lastDisplayUpdate = 0;

  handleSerialCommands();

  // Let the HX711 ready signal define acquisition cadence. Downstream consumers
  // are updated exactly once per accepted public scale sample.
  float weight = scale.getWeight();
  const uint32_t scaleSequence = scale.getSampleSequence();
  bool freshScaleSample = false;
  if (scaleSequence != lastProcessedScaleSequence) {
    flowRate.update(weight);
    // Broadcast live weight to StopMyBru relay module (no-op if not paired)
    smbComms.sendWeightUpdate(weight);
    // Notify auto-sleep timer of current weight
    powerManager.notifyWeight(weight);
    lastProcessedScaleSequence = scaleSequence;
    freshScaleSample = true;
  }
  
  // Check WiFi status every 30 seconds for debugging
  if (millis() - lastWiFiCheck >= 30000) {
    printWiFiStatus();
    lastWiFiCheck = millis();
  }
  
  // Maintain WiFi AP stability
  maintainWiFi();
  
  // Service BLE every loop. BluetoothScale only emits weight notifications when
  // Scale advances its fresh-sample sequence, so this does not create duplicate
  // timer-driven weight packets.
  bluetoothScale.update();
  
  // Drive StopMyBru pairing state machine
  smbComms.update();

  // Update touch sensor
  touchSensor.update();
  
  // Update power manager
  powerManager.update();
  
  // Update battery monitor
  batteryMonitor.update();

  // Emit lightweight battery/runtime benchmark telemetry for old-vs-new
  // drain comparisons. This is serial-only and does not write persistent state.
  printBatteryBenchmarkLog();

  if (freshScaleSample) {
    printUsbWeightSample(weight);
  }
  
  // Update display less frequently for power saving
  if (millis() - lastDisplayUpdate >= 100) { // Reduced display refresh rate to 10Hz
    oledDisplay.update();
    lastDisplayUpdate = millis();
  }
  
  // Increased delay for better power efficiency while maintaining responsiveness
  delay(10); // Optimized delay: 10ms for good responsiveness with power savings
}
