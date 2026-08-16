#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Update.h>
#include <Ticker.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include "WebServer.h"
#include "Scale.h"
#include "WiFiManager.h"
#include <Preferences.h>
#include "FlowRate.h"
#include "Calibration.h"
#include "BluetoothScale.h"
#include "Version.h"
#include "DiagnosticEventLog.h"
#include "BoardHardware.h"
#include "BatteryDrainSession.h"

Preferences preferences;

// Cache for display settings to avoid repeated slow EEPROM reads
static int cachedDecimals = -1; // -1 indicates not cached yet
static unsigned long lastDecimalCacheTime = 0;
const unsigned long DECIMAL_CACHE_TIMEOUT = 300000; // 5 minutes cache timeout

static Ticker otaRestartTicker;
static bool otaUploadFailed = false;
static bool otaUploadFinished = false;
static bool otaUploadStarted = false;
static bool otaLastSuccess = false;
static size_t otaUploadProgress = 0;
static size_t otaUploadTotal = 0;
static String otaUploadTarget = "none";
static String otaLastMessage = "idle";
static String otaLastFilename;
static String cachedDeviceInfoJson;
static String cachedOtaIdleStatusJson;
static String cachedDashboardJson[2];
static volatile uint8_t cachedDashboardActiveIndex = 0;
static unsigned long lastDashboardCacheUpdateMs = 0;
static constexpr unsigned long DASHBOARD_CACHE_INTERVAL_MS = 1000;

static String jsonEscape(const String& input);
static String jsonNumberOrNull(float value, unsigned int decimals = 3);
static bool parseFiniteFloat(const String& input, float& output);
static bool isSaneScaleCalibrationFactor(float value);

static void restartAfterOta() {
    ESP.restart();
}

static String formatRuntimeEstimate(int minutes) {
    if (minutes < 0) {
        return "Estimating";
    }

    if (minutes >= 1440) {
        int days = minutes / 1440;
        int hours = (minutes % 1440) / 60;
        return String(days) + "d " + String(hours) + "h";
    }

    if (minutes >= 60) {
        int hours = minutes / 60;
        int mins = minutes % 60;
        return String(hours) + "h " + String(mins) + "m";
    }

    return String(minutes) + "m";
}

static bool firmwareOtaSupported() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    return running != nullptr && next != nullptr && next != running;
}

static const esp_partition_t* filesystemPartition() {
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr);
}

static String buildOtaStatusJson() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    const esp_partition_t* fs = filesystemPartition();

    String json = "{";
    json.reserve(512);
    json += "\"firmwareOtaSupported\":" + String(firmwareOtaSupported() ? "true" : "false") + ",";
    json += "\"filesystemOtaSupported\":" + String(fs != nullptr ? "true" : "false") + ",";
    json += "\"runningPartition\":\"" + String(running ? running->label : "unknown") + "\",";
    json += "\"nextPartition\":\"" + String(next ? next->label : "none") + "\",";
    json += "\"firmwareSize\":" + String(ESP.getSketchSize()) + ",";
    json += "\"firmwareFreeSpace\":" + String(ESP.getFreeSketchSpace()) + ",";
    json += "\"filesystemPartition\":\"" + String(fs ? fs->label : "none") + "\",";
    json += "\"filesystemSize\":" + String(fs ? fs->size : 0) + ",";
    json += "\"inProgress\":" + String(otaUploadStarted && !otaUploadFinished ? "true" : "false") + ",";
    json += "\"target\":\"" + jsonEscape(otaUploadTarget) + "\",";
    json += "\"filename\":\"" + jsonEscape(otaLastFilename) + "\",";
    json += "\"progress\":" + String(otaUploadProgress) + ",";
    json += "\"total\":" + String(otaUploadTotal) + ",";
    json += "\"lastSuccess\":" + String(otaLastSuccess ? "true" : "false") + ",";
    json += "\"lastMessage\":\"" + jsonEscape(otaLastMessage) + "\"";
    json += "}";
    return json;
}

static String otaStatusJson() {
    // Normal reads should be cheap on AsyncTCP's request task. Once an upload
    // starts, return live progress/status until the device reboots.
    if (!otaUploadStarted && cachedOtaIdleStatusJson.length() > 0) {
        return cachedOtaIdleStatusJson;
    }
    return buildOtaStatusJson();
}

static String buildDeviceInfoJson() {
    String json = "{";
    json.reserve(768);
    json += "\"version\":\"" + String(WEIGHMYBRU_VERSION_STRING) + "\",";
    json += "\"full_version\":\"" + String(WEIGHMYBRU_FULL_VERSION) + "\",";
    json += "\"commit_hash\":\"" + String(WEIGHMYBRU_COMMIT_HASH) + "\",";
    json += "\"build_number\":" + String(WEIGHMYBRU_BUILD_NUMBER) + ",";
    json += "\"board\":\"" + String(WEIGHMYBRU_BOARD_NAME) + "\",";
    json += "\"build_date\":\"" + String(WEIGHMYBRU_BUILD_DATE) + "\",";
    json += "\"build_time\":\"" + String(WEIGHMYBRU_BUILD_TIME) + "\",";
    json += "\"firmware_size\":" + String(ESP.getSketchSize()) + ",";
    json += "\"free_space\":" + String(ESP.getFreeSketchSpace()) + ",";
    json += "\"chip_model\":\"" + String(ESP.getChipModel()) + "\",";
    json += "\"chip_revision\":" + String(ESP.getChipRevision()) + ",";
    json += "\"cpu_frequency\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"flash_size\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"heap_size\":" + String(ESP.getHeapSize()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"free_heap_at_boot\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"psram_size\":" + String(ESP.getPsramSize()) + ",";
    json += "\"free_psram\":" + String(ESP.getFreePsram()) + ",";
    json += "\"free_psram_at_boot\":" + String(ESP.getFreePsram()) + ",";
    json += "\"sdk_version\":\"" + String(ESP.getSdkVersion()) + "\"";
    json += "}";
    return json;
}
static void handleOtaUpload(AsyncWebServerRequest *request,
                            const String& filename,
                            size_t index,
                            uint8_t *data,
                            size_t len,
                            bool final,
                            int command,
                            const char* targetName) {
    if (index == 0) {
        otaUploadStarted = true;
        otaUploadFinished = false;
        otaUploadFailed = false;
        otaLastSuccess = false;
        otaUploadProgress = 0;
        otaUploadTotal = request->contentLength();
        otaUploadTarget = targetName ? targetName : "unknown";
        otaLastFilename = filename;
        otaLastMessage = "starting";

        Serial.printf("OTA %s start: %s total=%u\n",
                      otaUploadTarget.c_str(),
                      filename.c_str(),
                      static_cast<unsigned>(otaUploadTotal));

        if (command == U_FLASH && !firmwareOtaSupported()) {
            otaUploadFailed = true;
            otaLastMessage = "Firmware OTA requires a dual-OTA partition table. Flash the factory image once over USB first.";
            Serial.println("OTA firmware blocked: no alternate OTA partition");
            return;
        }

        if (command == U_SPIFFS && filesystemPartition() == nullptr) {
            otaUploadFailed = true;
            otaLastMessage = "Filesystem OTA partition not found";
            Serial.println("OTA filesystem blocked: filesystem partition not found");
            return;
        }

        // AsyncWebServer multipart uploads report the full HTTP body length,
        // not just the uploaded .bin file length. LittleFS images may exactly
        // fill the filesystem partition, so using request->contentLength()
        // can make Update.begin() reject a valid file as "Bad Size Given".
        // Let the Update library use the actual target partition size.
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
            otaUploadFailed = true;
            otaLastMessage = String("Update begin failed: ") + Update.errorString();
            Update.printError(Serial);
            return;
        }
    }

    if (otaUploadFailed || Update.hasError()) {
        return;
    }

    if (len > 0) {
        const size_t written = Update.write(data, len);
        otaUploadProgress = index + written;
        if (written != len) {
            otaUploadFailed = true;
            otaLastMessage = String("Update write failed: ") + Update.errorString();
            Update.printError(Serial);
            return;
        }
    }

    if (final) {
        otaUploadFinished = true;
        if (Update.end(true)) {
            otaUploadProgress = index + len;
            otaLastSuccess = true;
            otaLastMessage = String(targetName) + " OTA successful; restarting";
            Serial.printf("OTA %s success: %u bytes\n",
                          otaUploadTarget.c_str(),
                          static_cast<unsigned>(index + len));
        } else {
            otaUploadFailed = true;
            otaLastSuccess = false;
            otaLastMessage = String("Update end failed: ") + Update.errorString();
            Update.printError(Serial);
        }
    }
}

static void sendOtaUploadResponse(AsyncWebServerRequest *request) {
    const bool ok = otaUploadStarted && otaUploadFinished && !otaUploadFailed && !Update.hasError() && otaLastSuccess;
    if (!ok && !otaUploadFailed && Update.hasError()) {
        otaLastMessage = String("Update failed: ") + Update.errorString();
    }

    AsyncWebServerResponse *response = request->beginResponse(
        ok ? 200 : 500,
        "application/json",
        otaStatusJson());
    response->addHeader("Connection", "close");
    request->send(response);

    if (ok) {
        otaRestartTicker.once(1.5f, restartAfterOta);
    }
}

static String jsonEscape(const String& input) {
    String escaped;
    escaped.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        if (c == '\\' || c == '"') {
            escaped += '\\';
            escaped += c;
        } else if (c == '\n') {
            escaped += "\\n";
        } else if (c == '\r') {
            escaped += "\\r";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

static String jsonNumberOrNull(float value, unsigned int decimals) {
    if (!isfinite(value)) {
        return "null";
    }
    return String(value, decimals);
}

static bool parseFiniteFloat(const String& input, float& output) {
    String trimmed = input;
    trimmed.trim();
    if (trimmed.length() == 0) {
        return false;
    }

    char* end = nullptr;
    const char* start = trimmed.c_str();
    const float parsed = strtof(start, &end);
    if (end == start) {
        return false;
    }

    while (end != nullptr && *end != '\0') {
        if (!isspace(static_cast<unsigned char>(*end))) {
            return false;
        }
        ++end;
    }

    if (!isfinite(parsed)) {
        return false;
    }

    output = parsed;
    return true;
}

static bool isSaneScaleCalibrationFactor(float value) {
    // Current supported WMB-style HX711/load-cell builds are normally in the
    // hundreds to low thousands of counts/g. Keep the acceptance window wide so
    // unusual cells are not blocked, but reject values that would obviously
    // brick the next boot into raw-count output or nonsense scaling.
    return isfinite(value) && value >= 10.0f && value <= 100000.0f;
}

int getCachedDecimals() {
    // Fast path - return immediately if already cached and recent
    if (cachedDecimals != -1 && (millis() - lastDecimalCacheTime < DECIMAL_CACHE_TIMEOUT)) {
        return cachedDecimals;
    }
    
    unsigned long startTime = millis();
    
    if (preferences.begin("display", false)) {
        cachedDecimals = preferences.getInt("decimals", 1);
        preferences.end();
        lastDecimalCacheTime = millis();
        Serial.printf("Display: OK in %lums\n", millis() - startTime);
    } else {
        cachedDecimals = 1; // Use default
        lastDecimalCacheTime = millis();
        Serial.println("Display: FAIL");
    }
    
    return cachedDecimals;
}

void setCachedDecimals(int decimals) {
    Serial.println("Saving decimal setting...");
    unsigned long startTime = millis();
    
    if (preferences.begin("display", false)) {
        preferences.putInt("decimals", decimals);
        preferences.end();
        cachedDecimals = decimals; // Update cache
        lastDecimalCacheTime = millis();
        Serial.printf("Decimal setting saved in %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Failed to save decimal setting to EEPROM");
    }
}

void diagnoseEEPROMPerformance() {
    Serial.println("=== EEPROM Performance Diagnostics ===");
    
    // Test WiFi preferences
    unsigned long startTime = millis();
    Preferences testPrefs;
    if (testPrefs.begin("test", false)) {
        testPrefs.putInt("testkey", 42);
        int val = testPrefs.getInt("testkey", 0);
        testPrefs.end();
        Serial.printf("EEPROM test write/read took: %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Cannot open test preferences namespace");
    }
    
    // Test existing namespaces
    startTime = millis();
    if (testPrefs.begin("wifi", true)) {
        String ssid = testPrefs.getString("ssid", "");
        testPrefs.end();
        Serial.printf("WiFi namespace read took: %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Cannot open wifi preferences namespace");
    }
    
    startTime = millis();
    if (testPrefs.begin("display", true)) {
        int decimals = testPrefs.getInt("decimals", 1);
        testPrefs.end();
        Serial.printf("Display namespace read took: %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Cannot open display preferences namespace");
    }
    
    Serial.println("=== End Diagnostics ===");
}

AsyncWebServer server(80);

/*
 * API Endpoints for External Brewing Systems (e.g., GaggiMate):
 * 
 * Ultra-fast weight reading (minimal latency):
 * GET /api/brew/weight
 * Response: "45.2" (weight in grams, 1 decimal)
 * 
 * Fast brewing status:
 * GET /api/brew/status  
 * Response: {"w":45.2,"f":2.1} (weight and flowrate)
 * 
 * Standard dashboard:
 * GET /api/dashboard
 * Response: {"weight":45.23,"flowrate":2.15}
 */

static String buildDashboardJson(Scale &scale,
                                 FlowRate &flowRate,
                                 BluetoothScale &bluetoothScale,
                                 Display &display,
                                 BatteryMonitor &battery,
                                 DiagnosticEventLog &diagnosticEvents,
                                 BoardHardware &boardHardware) {
  // Built from loopTask through updateDashboardCache(). AsyncTCP request
  // handlers serve the cached string and avoid touching live acquisition state
  // while the HX711 is running at 80 SPS.
  String json;
  json.reserve(3000);
  json += "{";

  const float currentWeight = scale.getCurrentWeight();
  const float currentFlow = flowRate.getFlowRate();
  const bool hx711Connected = scale.isHX711Connected();
  const float hx711RateHz = scale.getDetectedSampleRateHz();
  const String hx711RateMode = scale.getDetectedHx711RateMode();
  const int scaleQuality = scale.getScaleQualityScore();
  const int lifetimeQuality = scale.getLifetimeQualityScore();

  json += "\"weight\":" + String(currentWeight, 2) + ",";
  json += "\"flowrate\":" + String(currentFlow, 1) + ",";
  json += "\"scale_connected\":" + String(hx711Connected ? "true" : "false") + ",";
  json += "\"hx711_connected\":" + String(hx711Connected ? "true" : "false") + ",";
  json += "\"filter_state\":\"" + scale.getFilterState() + "\",";
  json += "\"scale_sample_sequence\":" + String(scale.getSampleSequence()) + ",";
  json += "\"scale_last_sample_ms\":" + String(scale.getLastSampleMillis()) + ",";
  json += "\"scale_detected_rate_hz\":" + String(hx711RateHz, 2) + ",";
  json += "\"hx711_rate_hz\":" + String(hx711RateHz, 2) + ",";
  json += "\"scale_detected_rate_mode\":\"" + hx711RateMode + "\",";
  json += "\"hx711_rate_mode\":\"" + hx711RateMode + "\",";
  json += "\"scale_avg_interval_us\":" + String(scale.getSampleIntervalAverageMicros()) + ",";
  json += "\"scale_min_interval_us\":" + String(scale.getSampleIntervalMinMicros()) + ",";
  json += "\"scale_max_interval_us\":" + String(scale.getSampleIntervalMaxMicros()) + ",";
  json += "\"scale_long_gap_count\":" + String(scale.getSampleIntervalLongGapCount()) + ",";
  json += "\"scale_cadence_stats_count\":" + String(scale.getSampleIntervalStatsCount()) + ",";
  json += "\"acquisition_model\":\"" + String(scale.getAcquisitionModel()) + "\",";
  json += "\"acquisition_poll_count\":" + String(scale.getAcquisitionPollCount()) + ",";
  json += "\"acquisition_ready_count\":" + String(scale.getAcquisitionReadyCount()) + ",";
  json += "\"acquisition_not_ready_count\":" + String(scale.getAcquisitionNotReadyCount()) + ",";
  json += "\"acquisition_accepted_count\":" + String(scale.getAcquisitionAcceptedCount()) + ",";
  json += "\"acquisition_rejected_count\":" + String(scale.getAcquisitionRejectedCount()) + ",";
  json += "\"acquisition_read_error_count\":" + String(scale.getAcquisitionReadErrorCount()) + ",";
  json += "\"acquisition_disconnected_count\":" + String(scale.getAcquisitionDisconnectedCount()) + ",";
  json += "\"acquisition_data_ready_notifications\":" + String(scale.getAcquisitionDataReadyNotificationCount()) + ",";
  json += "\"acquisition_busy_skip_count\":" + String(scale.getAcquisitionBusySkipCount()) + ",";
  json += "\"acquisition_timeout_count\":" + String(scale.getAcquisitionTimeoutCount()) + ",";
  json += "\"scale_quality_score\":" + String(scaleQuality) + ",";
  json += "\"firmware_quality\":" + String(scaleQuality) + ",";
  json += "\"scale_lifetime_quality_score\":" + String(lifetimeQuality) + ",";
  json += "\"scale_bump_count\":" + String(scale.getBumpCount()) + ",";
  json += "\"scale_recent_bump\":" + String(scale.hasRecentBump() ? "true" : "false") + ",";
  json += "\"scale_glitch_count\":" + String(scale.getGlitchCount()) + ",";
  json += "\"scale_recent_glitch\":" + String(scale.hasRecentGlitch() ? "true" : "false") + ",";
  json += "\"mode\":\"UNIFIED\",";

  const unsigned long elapsedTime = display.getElapsedTime();
  const unsigned long minutes = elapsedTime / 60000;
  const unsigned long seconds = (elapsedTime % 60000) / 1000;
  const unsigned long milliseconds = elapsedTime % 1000;
  json += "\"timer_running\":" + String(display.isTimerRunning() ? "true" : "false") + ",";
  json += "\"timer_elapsed\":" + String(elapsedTime) + ",";
  json += "\"timer_display\":\"" + String(minutes) + ":" +
          (seconds < 10 ? "0" : "") + String(seconds) + "." +
          (milliseconds < 100 ? (milliseconds < 10 ? "00" : "0") : "") + String(milliseconds) + "\",";
  if (flowRate.hasTimerAverage()) {
    json += "\"timer_avg_flowrate\":" + String(flowRate.getTimerAverageFlowRate(), 2);
  } else {
    json += "\"timer_avg_flowrate\":null";
  }

  json += ",\"battery_voltage\":" + String(battery.getBatteryVoltage(), 2);
  json += ",\"battery_percentage\":" + String(battery.getBatteryPercentage());
  json += ",\"battery_raw_percentage\":" + String(battery.getRawBatteryPercentage());
  json += ",\"battery_capacity_mah\":" + String(battery.getBatteryCapacityMah());
  json += ",\"battery_backend\":\"" + battery.getBatteryBackend() + "\"";
  json += ",\"battery_fuel_gauge\":" + String(battery.hasFuelGauge() ? "true" : "false");
  json += ",\"usb_power_present\":" + String(battery.isUsbPowerPresent() ? "true" : "false");
  json += ",\"usb_present\":" + String(battery.isUsbPowerPresent() ? "true" : "false");
  json += ",\"usb_only_power\":" + String(battery.isUsbOnlyPower() ? "true" : "false");
  json += ",\"battery_status\":\"" + battery.getBatteryStatus() + "\"";
  json += ",\"battery_segments\":" + String(battery.getBatterySegments());
  json += ",\"battery_low\":" + String(battery.isLowBattery() ? "true" : "false");
  json += ",\"battery_critical\":" + String(battery.isCriticalBattery() ? "true" : "false");
  json += ",\"battery_charging\":" + String(battery.isCharging() ? "true" : "false");
  json += ",\"battery_charging_state\":\"" + battery.getChargingState() + "\"";

  json += ",\"diagnostic_event_count\":" + String(diagnosticEvents.count());
  json += ",\"diagnostic_event_capacity\":" + String(diagnosticEvents.capacity());
  json += ",\"diagnostic_event_log_psram\":" + String(diagnosticEvents.isPsramBacked() ? "true" : "false");
  json += ",\"board_rgb_status_led_available\":" + String(boardHardware.hasRgbStatusLed() ? "true" : "false");
  json += ",\"board_antenna_switch_available\":" + String(boardHardware.hasAntennaSwitch() ? "true" : "false");

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  json += ",\"wifi_connected\":" + String(wifiConnected ? "true" : "false");
  json += ",\"wifi_signal_strength\":" + String(wifiConnected ? WiFi.RSSI() : 0);
  json += ",\"wifi_signal_quality\":\"" + getWiFiSignalQuality() + "\"";
  json += ",\"bluetooth_connected\":" + String(bluetoothScale.isConnected() ? "true" : "false");
  json += ",\"bluetooth_signal_strength\":" + String(bluetoothScale.getBluetoothSignalStrength());

  json += ",\"device_version\":\"" + String(WEIGHMYBRU_VERSION_STRING) + "\"";
  json += ",\"device_board\":\"" + String(WEIGHMYBRU_BOARD_NAME) + "\"";
  json += ",\"device_build_date\":\"" + String(WEIGHMYBRU_BUILD_DATE) + "\"";
  json += ",\"device_build_time\":\"" + String(WEIGHMYBRU_BUILD_TIME) + "\"";
  json += ",\"device_build_number\":" + String(WEIGHMYBRU_BUILD_NUMBER);
  json += ",\"device_commit_hash\":\"" + String(WEIGHMYBRU_COMMIT_HASH) + "\"";
  json += ",\"device_full_version\":\"" + String(WEIGHMYBRU_FULL_VERSION) + "\"";

  json += "}";
  return json;
}

void updateDashboardCache(Scale &scale,
                          FlowRate &flowRate,
                          BluetoothScale &bluetoothScale,
                          Display &display,
                          BatteryMonitor &battery,
                          DiagnosticEventLog &diagnosticEvents,
                          BoardHardware &boardHardware) {
  const unsigned long now = millis();
  if (cachedDashboardJson[cachedDashboardActiveIndex].length() > 0 &&
      now - lastDashboardCacheUpdateMs < DASHBOARD_CACHE_INTERVAL_MS) {
    return;
  }

  const uint8_t inactiveIndex = cachedDashboardActiveIndex == 0 ? 1 : 0;
  cachedDashboardJson[inactiveIndex] = buildDashboardJson(
      scale,
      flowRate,
      bluetoothScale,
      display,
      battery,
      diagnosticEvents,
      boardHardware);
  cachedDashboardActiveIndex = inactiveIndex;
  lastDashboardCacheUpdateMs = now;
}

void setupWebServer(Scale &scale, FlowRate &flowRate, BluetoothScale &bluetoothScale, Display &display, BatteryMonitor &battery, SmbComms &smb, PowerManager &powerManager, DiagnosticEventLog &diagnosticEvents, BoardHardware &boardHardware, BatteryDrainSession &batteryDrainSession, TouchSensor &touchSensor, ScaleCommandQueue &scaleCommandQueue) {
  if (!LittleFS.begin()) {
    Serial.println();
    Serial.println("=====================================");
    Serial.println("FILESYSTEM NOT FOUND!");
    Serial.println("=====================================");
    Serial.println("The LittleFS filesystem failed to mount.");
    Serial.println("This means the web interface files are missing.");
    Serial.println();
    Serial.println("To fix this, please run:");
    Serial.println("  pio run -t uploadfs");
    Serial.println();
    Serial.println("Or in PlatformIO IDE:");
    Serial.println("  Project Tasks → Platform → Upload Filesystem Image");
    Serial.println();
    Serial.println("The scale will continue to work, but the web interface will be unavailable.");
    Serial.println("=====================================");
    Serial.println();
    return;
  }

  // Run EEPROM diagnostics
  diagnoseEEPROMPerformance();

  // Pre-cache settings to avoid delays on first page load
  Serial.println("Pre-caching settings for faster page loads...");
  getCachedDecimals();        // This will cache the decimal setting
  getStoredSSID();            // This will cache WiFi credentials

  updateDashboardCache(scale, flowRate, bluetoothScale, display, battery, diagnosticEvents, boardHardware);

  // Register API route first. Serve a loop-owned cached dashboard snapshot so
  // browser/PWA polling cannot make AsyncTCP walk live HX711/battery state.
  server.on("/api/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
    const String json = cachedDashboardJson[cachedDashboardActiveIndex].length() > 0
        ? cachedDashboardJson[cachedDashboardActiveIndex]
        : String("{\"status\":\"warming\"}");
    request->send(200, "application/json", json);
  });

  // Timer control endpoints
  server.on("/api/timer/start", HTTP_POST, [&display, &smb](AsyncWebServerRequest *request) {
    display.startTimer();
    smb.sendRelayOn();
    request->send(200, "text/plain", "Timer started");
  });

  server.on("/api/timer/stop", HTTP_POST, [&display, &smb](AsyncWebServerRequest *request) {
    display.stopTimer();
    smb.sendRelayOff();
    request->send(200, "text/plain", "Timer stopped");
  });

  server.on("/api/timer/reset", HTTP_POST, [&display, &smb](AsyncWebServerRequest *request) {
    display.resetTimer();
    smb.sendRelayOff();
    request->send(200, "text/plain", "Timer reset");
  });

  server.on("/api/weight", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(scale.getCurrentWeight()));
  });

  // Lightweight weight-only endpoint for brewing applications
  server.on("/api/weight-fast", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    // Minimal processing for fastest response
    request->send(200, "text/plain", String(scale.getCurrentWeight(), 2));
  });

  // Brewing mode endpoints for external devices like GaggiMate
  server.on("/api/brew/weight", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    // Ultra-fast response for brewing systems
    float weight = scale.getCurrentWeight();
    request->send(200, "text/plain", String(weight, 1)); // 1 decimal for speed
  });
  
  server.on("/api/brew/status", HTTP_GET, [&scale, &flowRate](AsyncWebServerRequest *request) {
    // Minimal JSON for brewing systems
    String json = "{\"w\":" + String(scale.getCurrentWeight(), 1) + 
                  ",\"f\":" + String(flowRate.getFlowRate(), 1) + "}";
    request->send(200, "application/json", json);
  });

  // Battery calibration endpoints (must be before general /api/battery route)
  server.on("/api/battery/calibrate", HTTP_POST, [&battery](AsyncWebServerRequest *request) {
    if (request->hasParam("actualVoltage", true)) {
      String value = request->getParam("actualVoltage", true)->value();
      float actualVoltage = value.toFloat();
      if (actualVoltage > 0.0f && actualVoltage <= 5.0f) {
        battery.calibrateVoltage(actualVoltage);
        String json = "{";
        json += "\"status\":\"success\",";
        json += "\"message\":\"Battery calibrated to " + String(actualVoltage, 3) + "V\",";
        json += "\"new_voltage\":" + String(battery.getBatteryVoltage(), 3) + ",";
        json += "\"new_percentage\":" + String(battery.getBatteryPercentage()) + ",";
        json += "\"calibration_offset\":" + String(battery.getCalibrationOffset(), 3);
        json += "}";
        request->send(200, "application/json", json);
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid voltage. Must be between 0.1V and 5.0V\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'actualVoltage' parameter\"}");
    }
  });

  // GET version for easy browser access
  server.on("/api/battery/calibrate", HTTP_GET, [&battery](AsyncWebServerRequest *request) {
    if (request->hasParam("voltage")) {
      String value = request->getParam("voltage")->value();
      float actualVoltage = value.toFloat();
      if (actualVoltage > 0.0f && actualVoltage <= 5.0f) {
        float beforeVoltage = battery.getBatteryVoltage();
        int beforePercentage = battery.getBatteryPercentage();
        
        battery.calibrateVoltage(actualVoltage);
        
        float afterVoltage = battery.getBatteryVoltage();
        int afterPercentage = battery.getBatteryPercentage();
        
        String json = "{";
        json += "\"status\":\"success\",";
        json += "\"message\":\"Battery calibrated successfully\",";
        json += "\"before_voltage\":" + String(beforeVoltage, 3) + ",";
        json += "\"before_percentage\":" + String(beforePercentage) + ",";
        json += "\"after_voltage\":" + String(afterVoltage, 3) + ",";
        json += "\"after_percentage\":" + String(afterPercentage) + ",";
        json += "\"target_voltage\":" + String(actualVoltage, 3) + ",";
        json += "\"calibration_offset\":" + String(battery.getCalibrationOffset(), 3);
        json += "}";
        request->send(200, "application/json", json);
        Serial.printf("Battery calibrated via GET: %.3fV (was %.3fV, now %.3fV)\n", actualVoltage, beforeVoltage, afterVoltage);
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid voltage. Must be between 0.1V and 5.0V\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'voltage' parameter. Use ?voltage=4.30\"}");
    }
  });

  // Battery monitoring endpoint (general status)
  server.on("/api/battery", HTTP_GET, [&battery](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"voltage\":" + String(battery.getBatteryVoltage(), 3);
    json += ",\"percentage\":" + String(battery.getBatteryPercentage());
    json += ",\"raw_percentage\":" + String(battery.getRawBatteryPercentage());
    json += ",\"capacity_mah\":" + String(battery.getBatteryCapacityMah());
    json += ",\"backend\":\"" + battery.getBatteryBackend() + "\"";
    json += ",\"fuel_gauge\":" + String(battery.hasFuelGauge() ? "true" : "false");
    json += ",\"fuel_gauge_soc\":" + String(battery.getFuelGaugeStateOfCharge(), 2);
    json += ",\"usb_power_present\":" + String(battery.isUsbPowerPresent() ? "true" : "false");
    json += ",\"usb_only_power\":" + String(battery.isUsbOnlyPower() ? "true" : "false");
    json += ",\"status\":\"" + battery.getBatteryStatus() + "\"";
    json += ",\"segments\":" + String(battery.getBatterySegments());
    json += ",\"low_battery\":" + String(battery.isLowBattery() ? "true" : "false");
    json += ",\"critical_battery\":" + String(battery.isCriticalBattery() ? "true" : "false");
    json += ",\"charging\":" + String(battery.isCharging() ? "true" : "false");
    json += ",\"charging_state\":\"" + battery.getChargingState() + "\"";
    json += ",\"critical_shutdown_enabled\":" + String(battery.isCriticalShutdownEnabled() ? "true" : "false");
    json += ",\"critical_shutdown_voltage\":" + String(battery.getCriticalShutdownVoltage(), 2);
    json += ",\"critical_shutdown_percent\":" + String(static_cast<unsigned int>(battery.getCriticalShutdownPercent()));
    json += ",\"critical_recovery_voltage\":" + String(battery.getCriticalRecoveryVoltage(), 2);
    json += ",\"critical_recovery_percent\":" + String(static_cast<unsigned int>(battery.getCriticalRecoveryPercent()));
    json += ",\"critical_shutdown_active\":" + String(battery.shouldForceCriticalSleep() ? "true" : "false");
    int runtimeMinutes = battery.getEstimatedRuntimeMinutesRemaining();
    json += ",\"runtime_estimate_available\":" + String(runtimeMinutes >= 0 ? "true" : "false");
    json += ",\"runtime_minutes_remaining\":";
    json += runtimeMinutes >= 0 ? String(runtimeMinutes) : "null";
    json += ",\"runtime_display\":\"" + formatRuntimeEstimate(runtimeMinutes) + "\"";
    json += ",\"runtime_confidence\":\"" + battery.getRuntimeEstimateConfidence() + "\"";
    json += ",\"runtime_observation_minutes\":" + String(battery.getRuntimeObservationMinutes());
    json += ",\"discharge_rate_percent_per_hour\":" + String(battery.getDischargeRatePercentPerHour(), 3);
    json += ",\"discharge_current_ma\":" + String(battery.getEstimatedDischargeCurrentMa(), 2);
    json += ",\"learned_discharge_rate_percent_per_hour\":" + String(battery.getLearnedDischargeRatePercentPerHour(), 3);
    json += ",\"learned_discharge_current_ma\":" + String(battery.getLearnedDischargeCurrentMa(), 2);
    json += ",\"learned_discharge_observations\":" + String(battery.getLearnedDischargeObservations());
    int minutesTo80 = battery.getEstimatedMinutesTo80();
    int minutesTo100 = battery.getEstimatedMinutesTo100();
    json += ",\"charge_estimate_available\":" + String((minutesTo80 >= 0 || minutesTo100 >= 0) ? "true" : "false");
    json += ",\"minutes_to_80\":";
    json += minutesTo80 >= 0 ? String(minutesTo80) : "null";
    json += ",\"minutes_to_80_display\":\"" + formatRuntimeEstimate(minutesTo80) + "\"";
    json += ",\"minutes_to_100\":";
    json += minutesTo100 >= 0 ? String(minutesTo100) : "null";
    json += ",\"minutes_to_100_display\":\"" + formatRuntimeEstimate(minutesTo100) + "\"";
    json += ",\"charge_confidence\":\"" + battery.getChargeEstimateConfidence() + "\"";
    json += ",\"charge_observation_minutes\":" + String(battery.getChargeObservationMinutes());
    json += ",\"charge_rate_percent_per_hour\":" + String(battery.getChargeRatePercentPerHour(), 3);
    json += ",\"charge_current_ma\":" + String(battery.getEstimatedChargeCurrentMa(), 2);
    json += ",\"learned_charge_rate_percent_per_hour\":" + String(battery.getLearnedChargeRatePercentPerHour(), 3);
    json += ",\"learned_charge_current_ma\":" + String(battery.getLearnedChargeCurrentMa(), 2);
    json += ",\"learned_charge_observations\":" + String(battery.getLearnedChargeObservations());
    json += ",\"battery_learning_confidence\":\"" + battery.getBatteryLearningConfidence() + "\"";
    json += ",\"calibration_offset\":" + String(battery.getCalibrationOffset(), 3);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/battery/capacity", HTTP_POST, [&battery](AsyncWebServerRequest *request) {
    String value;
    if (request->hasParam("capacityMah", true)) {
      value = request->getParam("capacityMah", true)->value();
    } else if (request->hasParam("capacity_mah", true)) {
      value = request->getParam("capacity_mah", true)->value();
    } else if (request->hasParam("capacityMah")) {
      value = request->getParam("capacityMah")->value();
    } else if (request->hasParam("capacity_mah")) {
      value = request->getParam("capacity_mah")->value();
    }

    const int requestedCapacity = value.toInt();
    if (requestedCapacity < 100 || requestedCapacity > 5000) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"capacityMah must be 100-5000 mAh\"}");
      return;
    }

    battery.setBatteryCapacityMah(static_cast<uint16_t>(requestedCapacity));
    String json = "{";
    json += "\"status\":\"success\",";
    json += "\"message\":\"Battery capacity saved\",";
    json += "\"capacity_mah\":" + String(battery.getBatteryCapacityMah());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/battery/settings", HTTP_POST, [&battery](AsyncWebServerRequest *request) {
    bool hasCapacity = false;
    bool hasCriticalEnabled = false;
    bool hasCriticalVoltage = false;
    bool hasCriticalPercent = false;
    int capacity = battery.getBatteryCapacityMah();
    bool criticalEnabled = battery.isCriticalShutdownEnabled();
    float criticalVoltage = battery.getCriticalShutdownVoltage();
    int criticalPercent = battery.getCriticalShutdownPercent();

    if (request->hasParam("capacityMah", true)) {
      hasCapacity = true;
      capacity = request->getParam("capacityMah", true)->value().toInt();
      if (capacity < 100 || capacity > 5000) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"capacityMah must be 100-5000 mAh\"}");
        return;
      }
    }

    if (request->hasParam("criticalShutdownEnabled", true)) {
      hasCriticalEnabled = true;
      const String value = request->getParam("criticalShutdownEnabled", true)->value();
      criticalEnabled = value == "true" || value == "1" || value == "on";
    }

    if (request->hasParam("criticalShutdownVoltage", true)) {
      hasCriticalVoltage = true;
      criticalVoltage = request->getParam("criticalShutdownVoltage", true)->value().toFloat();
      if (criticalVoltage < 3.20f || criticalVoltage > 3.80f) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"criticalShutdownVoltage must be 3.20-3.80V\"}");
        return;
      }
    }

    if (request->hasParam("criticalShutdownPercent", true)) {
      hasCriticalPercent = true;
      criticalPercent = request->getParam("criticalShutdownPercent", true)->value().toInt();
      if (criticalPercent < 1 || criticalPercent > 20) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"criticalShutdownPercent must be 1-20\"}");
        return;
      }
    }

    if (hasCapacity) {
      battery.setBatteryCapacityMah(static_cast<uint16_t>(capacity));
    }
    if (hasCriticalEnabled) {
      battery.setCriticalShutdownEnabled(criticalEnabled);
    }
    if (hasCriticalVoltage) {
      battery.setCriticalShutdownVoltage(criticalVoltage);
    }
    if (hasCriticalPercent) {
      battery.setCriticalShutdownPercent(static_cast<uint8_t>(criticalPercent));
    }

    String json = "{";
    json += "\"status\":\"success\",";
    json += "\"message\":\"Battery settings saved\",";
    json += "\"capacity_mah\":" + String(battery.getBatteryCapacityMah()) + ",";
    json += "\"critical_shutdown_enabled\":" + String(battery.isCriticalShutdownEnabled() ? "true" : "false") + ",";
    json += "\"critical_shutdown_voltage\":" + String(battery.getCriticalShutdownVoltage(), 2) + ",";
    json += "\"critical_shutdown_percent\":" + String(static_cast<unsigned int>(battery.getCriticalShutdownPercent())) + ",";
    json += "\"critical_recovery_voltage\":" + String(battery.getCriticalRecoveryVoltage(), 2) + ",";
    json += "\"critical_recovery_percent\":" + String(static_cast<unsigned int>(battery.getCriticalRecoveryPercent()));
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/battery/benchmark", HTTP_GET, [&battery, &batteryDrainSession, &scale, &display, &bluetoothScale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"label\":\"" + String(batteryDrainSession.getLabel()) + "\"";
    json += ",\"capacity_mah\":" + String(battery.getBatteryCapacityMah());
    json += ",\"elapsed_minutes\":" + String(batteryDrainSession.getElapsedMinutes(), 3);
    json += ",\"samples\":" + String(batteryDrainSession.getSamples());
    json += ",\"invalid_samples\":" + String(batteryDrainSession.getInvalidSamples());
    json += ",\"start_millis\":" + String(batteryDrainSession.getStartMillis());
    json += ",\"last_millis\":" + String(batteryDrainSession.getLastMillis());
    json += ",\"start_voltage\":" + jsonNumberOrNull(batteryDrainSession.getStartVoltage(), 3);
    json += ",\"last_voltage\":" + jsonNumberOrNull(batteryDrainSession.getLastVoltage(), 3);
    json += ",\"start_percent\":" + String(batteryDrainSession.getStartPercent());
    json += ",\"last_percent\":" + String(batteryDrainSession.getLastPercent());
    json += ",\"start_raw_percent\":" + String(batteryDrainSession.getStartRawPercent());
    json += ",\"last_raw_percent\":" + String(batteryDrainSession.getLastRawPercent());
    json += ",\"delta_voltage\":" + jsonNumberOrNull(batteryDrainSession.getDeltaVoltage(), 3);
    json += ",\"delta_percent\":" + String(batteryDrainSession.getDeltaPercent());
    json += ",\"delta_raw_percent\":" + String(batteryDrainSession.getDeltaRawPercent());
    json += ",\"voltage_mv_per_hour\":" + jsonNumberOrNull(batteryDrainSession.getVoltageMillivoltsPerHour(), 2);
    json += ",\"raw_percent_per_hour\":" + String(batteryDrainSession.getRawPercentPerHour(), 3);
    json += ",\"trend\":\"" + String(batteryDrainSession.getTrend()) + "\"";
    json += ",\"confidence\":\"" + String(batteryDrainSession.getConfidence()) + "\"";
    json += ",\"battery_backend\":\"" + battery.getBatteryBackend() + "\"";
    json += ",\"battery_valid\":" + String(battery.hasValidReading() ? "true" : "false");
    json += ",\"current_voltage\":" + String(battery.getBatteryVoltage(), 3);
    json += ",\"current_percent\":" + String(battery.getBatteryPercentage());
    json += ",\"current_raw_percent\":" + String(battery.getRawBatteryPercentage());
    json += ",\"usb_power_present\":" + String(battery.isUsbPowerPresent() ? "true" : "false");
    json += ",\"charging\":" + String(battery.isCharging() ? "true" : "false");
    json += ",\"charging_state\":\"" + battery.getChargingState() + "\"";
    json += ",\"critical_shutdown_enabled\":" + String(battery.isCriticalShutdownEnabled() ? "true" : "false");
    json += ",\"critical_shutdown_voltage\":" + String(battery.getCriticalShutdownVoltage(), 2);
    json += ",\"critical_shutdown_percent\":" + String(static_cast<unsigned int>(battery.getCriticalShutdownPercent()));
    json += ",\"critical_recovery_voltage\":" + String(battery.getCriticalRecoveryVoltage(), 2);
    json += ",\"critical_recovery_percent\":" + String(static_cast<unsigned int>(battery.getCriticalRecoveryPercent()));
    json += ",\"critical_shutdown_active\":" + String(battery.shouldForceCriticalSleep() ? "true" : "false");
    json += ",\"runtime_minutes_remaining\":";
    int runtimeMinutes = battery.getEstimatedRuntimeMinutesRemaining();
    json += runtimeMinutes >= 0 ? String(runtimeMinutes) : "null";
    json += ",\"minutes_to_80\":";
    int minutesTo80 = battery.getEstimatedMinutesTo80();
    json += minutesTo80 >= 0 ? String(minutesTo80) : "null";
    json += ",\"minutes_to_100\":";
    int minutesTo100 = battery.getEstimatedMinutesTo100();
    json += minutesTo100 >= 0 ? String(minutesTo100) : "null";
    json += ",\"runtime_confidence\":\"" + battery.getRuntimeEstimateConfidence() + "\"";
    json += ",\"charge_confidence\":\"" + battery.getChargeEstimateConfidence() + "\"";
    json += ",\"learned_discharge_rate_percent_per_hour\":" + String(battery.getLearnedDischargeRatePercentPerHour(), 3);
    json += ",\"learned_charge_rate_percent_per_hour\":" + String(battery.getLearnedChargeRatePercentPerHour(), 3);
    json += ",\"discharge_current_ma\":" + String(battery.getEstimatedDischargeCurrentMa(), 2);
    json += ",\"charge_current_ma\":" + String(battery.getEstimatedChargeCurrentMa(), 2);
    json += ",\"learned_discharge_current_ma\":" + String(battery.getLearnedDischargeCurrentMa(), 2);
    json += ",\"learned_charge_current_ma\":" + String(battery.getLearnedChargeCurrentMa(), 2);
    json += ",\"learning_confidence\":\"" + battery.getBatteryLearningConfidence() + "\"";
    json += ",\"cpu_mhz\":" + String(ESP.getCpuFreqMHz());
    json += ",\"wifi_mode\":" + String(static_cast<int>(WiFi.getMode()));
    json += ",\"wifi_radio_on\":" + String(WiFi.getMode() != WIFI_OFF ? "true" : "false");
    json += ",\"wifi_sleep\":" + String(WiFi.getSleep() ? "true" : "false");
    json += ",\"ble_connected\":" + String(bluetoothScale.isConnected() ? "true" : "false");
    json += ",\"display_connected\":" + String(display.isConnected() ? "true" : "false");
    json += ",\"hx711_connected\":" + String(scale.isHX711Connected() ? "true" : "false");
    json += ",\"hx711_rate_hz\":" + String(scale.getDetectedSampleRateHz(), 2);
    json += ",\"hx711_rate_mode\":\"" + scale.getDetectedHx711RateMode() + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/battery/benchmark/reset", HTTP_POST, [&battery, &batteryDrainSession](AsyncWebServerRequest *request) {
    String label = "web";
    if (request->hasParam("label", true)) {
      label = request->getParam("label", true)->value();
    } else if (request->hasParam("label")) {
      label = request->getParam("label")->value();
    }
    if (label.length() == 0) {
      label = "web";
    }
    batteryDrainSession.reset(millis(),
                              battery.getBatteryVoltage(),
                              battery.getBatteryPercentage(),
                              battery.getRawBatteryPercentage(),
                              battery.hasValidReading(),
                              label.c_str());
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Battery benchmark session reset\"}");
  });

  // Battery debug endpoint for troubleshooting
  server.on("/api/battery/debug", HTTP_GET, [&battery](AsyncWebServerRequest *request) {
    // We need to expose the raw ADC reading for debugging
    // Let's create a temporary battery instance to get raw data
    int rawADC = battery.hasFuelGauge() ? -1 : analogRead(7); // GPIO7 battery pin on ADC-backed boards
    float rawVoltage = battery.hasFuelGauge() ? 0.0f : (((float)rawADC / 4095.0f) * 3.3f);
    float dividedVoltage = battery.hasFuelGauge() ? 0.0f : (rawVoltage * 2.0f); // Apply voltage divider ratio
    
    String json = "{";
    json += "\"raw_adc\":" + String(rawADC) + ",";
    json += "\"raw_voltage\":" + String(rawVoltage, 3) + ",";
    json += "\"divided_voltage\":" + String(dividedVoltage, 3) + ",";
    json += "\"calibrated_voltage\":" + String(battery.getBatteryVoltage(), 3) + ",";
    json += "\"backend\":\"" + battery.getBatteryBackend() + "\",";
    json += "\"fuel_gauge\":" + String(battery.hasFuelGauge() ? "true" : "false") + ",";
    json += "\"fuel_gauge_soc\":" + String(battery.getFuelGaugeStateOfCharge(), 2) + ",";
    json += "\"usb_power_present\":" + String(battery.isUsbPowerPresent() ? "true" : "false") + ",";
    json += "\"calibration_offset\":" + String(battery.getCalibrationOffset(), 3) + ",";
    json += "\"capacity_mah\":" + String(battery.getBatteryCapacityMah()) + ",";
    json += "\"percentage\":" + String(battery.getBatteryPercentage()) + ",";
    json += "\"raw_percentage\":" + String(battery.getRawBatteryPercentage()) + ",";
    json += "\"charging\":" + String(battery.isCharging() ? "true" : "false") + ",";
    json += "\"charging_state\":\"" + battery.getChargingState() + "\",";
    json += "\"critical_shutdown_enabled\":" + String(battery.isCriticalShutdownEnabled() ? "true" : "false") + ",";
    json += "\"critical_shutdown_voltage\":" + String(battery.getCriticalShutdownVoltage(), 2) + ",";
    json += "\"critical_shutdown_percent\":" + String(static_cast<unsigned int>(battery.getCriticalShutdownPercent())) + ",";
    json += "\"critical_recovery_voltage\":" + String(battery.getCriticalRecoveryVoltage(), 2) + ",";
    json += "\"critical_recovery_percent\":" + String(static_cast<unsigned int>(battery.getCriticalRecoveryPercent())) + ",";
    json += "\"critical_shutdown_active\":" + String(battery.shouldForceCriticalSleep() ? "true" : "false") + ",";
    int runtimeMinutes = battery.getEstimatedRuntimeMinutesRemaining();
    json += "\"runtime_estimate_available\":" + String(runtimeMinutes >= 0 ? "true" : "false") + ",";
    json += "\"runtime_minutes_remaining\":";
    json += runtimeMinutes >= 0 ? String(runtimeMinutes) : "null";
    json += ",\"runtime_display\":\"" + formatRuntimeEstimate(runtimeMinutes) + "\",";
    json += "\"runtime_confidence\":\"" + battery.getRuntimeEstimateConfidence() + "\",";
    json += "\"runtime_observation_minutes\":" + String(battery.getRuntimeObservationMinutes()) + ",";
    json += "\"discharge_rate_percent_per_hour\":" + String(battery.getDischargeRatePercentPerHour(), 3);
    int debugMinutesTo80 = battery.getEstimatedMinutesTo80();
    int debugMinutesTo100 = battery.getEstimatedMinutesTo100();
    json += ",\"charge_estimate_available\":" + String((debugMinutesTo80 >= 0 || debugMinutesTo100 >= 0) ? "true" : "false");
    json += ",\"minutes_to_80\":";
    json += debugMinutesTo80 >= 0 ? String(debugMinutesTo80) : "null";
    json += ",\"minutes_to_80_display\":\"" + formatRuntimeEstimate(debugMinutesTo80) + "\"";
    json += ",\"minutes_to_100\":";
    json += debugMinutesTo100 >= 0 ? String(debugMinutesTo100) : "null";
    json += ",\"minutes_to_100_display\":\"" + formatRuntimeEstimate(debugMinutesTo100) + "\"";
    json += ",\"charge_confidence\":\"" + battery.getChargeEstimateConfidence() + "\"";
    json += ",\"charge_observation_minutes\":" + String(battery.getChargeObservationMinutes());
    json += ",\"charge_rate_percent_per_hour\":" + String(battery.getChargeRatePercentPerHour(), 3);
    json += ",\"discharge_current_ma\":" + String(battery.getEstimatedDischargeCurrentMa(), 2);
    json += ",\"charge_current_ma\":" + String(battery.getEstimatedChargeCurrentMa(), 2);
    json += ",\"learned_discharge_rate_percent_per_hour\":" + String(battery.getLearnedDischargeRatePercentPerHour(), 3);
    json += ",\"learned_charge_rate_percent_per_hour\":" + String(battery.getLearnedChargeRatePercentPerHour(), 3);
    json += ",\"learned_discharge_current_ma\":" + String(battery.getLearnedDischargeCurrentMa(), 2);
    json += ",\"learned_charge_current_ma\":" + String(battery.getLearnedChargeCurrentMa(), 2);
    json += ",\"learned_discharge_observations\":" + String(battery.getLearnedDischargeObservations());
    json += ",\"learned_charge_observations\":" + String(battery.getLearnedChargeObservations());
    json += ",\"battery_learning_confidence\":\"" + battery.getBatteryLearningConfidence() + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/tare", HTTP_POST, [&touchSensor](AsyncWebServerRequest *request){
    touchSensor.requestTare("web");
    request->send(202, "text/plain", "Tare scheduled through scale command path.");
  });

  server.on("/api/set-calibrationfactor", HTTP_POST, [&scaleCommandQueue](AsyncWebServerRequest *request){
    if (!request->hasParam("calibrationfactor", true)) {
      request->send(400, "text/plain", "Missing 'calibrationfactor' parameter");
      return;
    }

    String value = request->getParam("calibrationfactor", true)->value();
    float requestedCalibrationFactor = 0.0f;
    if (!parseFiniteFloat(value, requestedCalibrationFactor) ||
        !isSaneScaleCalibrationFactor(requestedCalibrationFactor)) {
      Serial.printf("Rejected invalid calibration factor input: %s\n", value.c_str());
      request->send(400, "text/plain", "Invalid calibration factor; value must be finite and between 10 and 100000");
      return;
    }

    if (!scaleCommandQueue.requestSetCalibrationFactor(requestedCalibrationFactor, "web")) {
      request->send(400, "text/plain", "Invalid calibration factor; value must be finite and between 10 and 100000");
      return;
    }

    Serial.printf("Queued calibration factor update: %.6f\n", requestedCalibrationFactor);
    request->send(202, "text/plain", "Calibration factor queued: " + String(requestedCalibrationFactor, 6));
  });

  server.on("/api/calibrate", HTTP_POST, [&scale, &scaleCommandQueue](AsyncWebServerRequest *request){
    if (request->hasParam("knownWeight", true)) {
      String value = request->getParam("knownWeight", true)->value();
      float knownWeight = 0.0f;
      if (!parseFiniteFloat(value, knownWeight) || knownWeight <= 0.0f) {
        request->send(400, "text/plain", "Invalid known weight");
        return;
      }
      // Use the cached raw value from the loop-owned acquisition path. Do not
      // clock HX711 from the AsyncTCP web callback.
      const bool hasCachedRaw = scale.hasLastRawValue();
      long raw = scale.getLastRawValue();
      if (hasCachedRaw && raw != 0) {
        float newCalibrationFactor = (float)raw / knownWeight;
        if (!isSaneScaleCalibrationFactor(newCalibrationFactor)) {
          Serial.printf("Rejected derived calibration factor %.6f from raw=%ld knownWeight=%.3f\n",
                        newCalibrationFactor,
                        raw,
                        knownWeight);
          request->send(400, "text/plain", "Derived calibration factor out of supported range");
          return;
        }
        if (!scaleCommandQueue.requestSetCalibrationFactor(newCalibrationFactor, "web-calibrate")) {
          request->send(400, "text/plain", "Derived calibration factor out of supported range");
          return;
        }
        Serial.printf("Calibration queued. New factor: %.6f\n", newCalibrationFactor);
        request->send(202, "text/plain", "Scale calibration queued! New factor: " + String(newCalibrationFactor, 6));
      } else {
        request->send(400, "text/plain", "No cached scale reading available yet");
      }
    } else {
      request->send(400, "text/plain", "Missing 'knownWeight' parameter");
    }
  });

  server.on("/api/calibrationfactor", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(scale.getCalibrationFactor(), 6));
  });

  // Scale connection status endpoint
  server.on("/api/scale/status", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"connected\":" + String(scale.isHX711Connected() ? "true" : "false") + ",";
    json += "\"weight\":" + String(scale.getCurrentWeight(), 2) + ",";
    json += "\"raw_value\":" + String(scale.getLastRawValue()) + ",";
    json += "\"raw_value_cached\":" + String(scale.hasLastRawValue() ? "true" : "false") + ",";
    json += "\"calibration_factor\":" + String(scale.getCalibrationFactor(), 6) + ",";
    json += "\"sample_sequence\":" + String(scale.getSampleSequence()) + ",";
    json += "\"last_sample_ms\":" + String(scale.getLastSampleMillis()) + ",";
    json += "\"detected_rate_hz\":" + String(scale.getDetectedSampleRateHz(), 2) + ",";
    json += "\"detected_rate_mode\":\"" + scale.getDetectedHx711RateMode() + "\",";
    json += "\"avg_interval_us\":" + String(scale.getSampleIntervalAverageMicros()) + ",";
    json += "\"min_interval_us\":" + String(scale.getSampleIntervalMinMicros()) + ",";
    json += "\"max_interval_us\":" + String(scale.getSampleIntervalMaxMicros()) + ",";
    json += "\"long_gap_count\":" + String(scale.getSampleIntervalLongGapCount()) + ",";
    json += "\"cadence_stats_count\":" + String(scale.getSampleIntervalStatsCount()) + ",";
    json += "\"acquisition_model\":\"" + String(scale.getAcquisitionModel()) + "\",";
    json += "\"acquisition_poll_count\":" + String(scale.getAcquisitionPollCount()) + ",";
    json += "\"acquisition_ready_count\":" + String(scale.getAcquisitionReadyCount()) + ",";
    json += "\"acquisition_not_ready_count\":" + String(scale.getAcquisitionNotReadyCount()) + ",";
    json += "\"acquisition_accepted_count\":" + String(scale.getAcquisitionAcceptedCount()) + ",";
    json += "\"acquisition_rejected_count\":" + String(scale.getAcquisitionRejectedCount()) + ",";
    json += "\"acquisition_read_error_count\":" + String(scale.getAcquisitionReadErrorCount()) + ",";
    json += "\"acquisition_disconnected_count\":" + String(scale.getAcquisitionDisconnectedCount()) + ",";
    json += "\"acquisition_data_ready_notifications\":" + String(scale.getAcquisitionDataReadyNotificationCount()) + ",";
    json += "\"acquisition_busy_skip_count\":" + String(scale.getAcquisitionBusySkipCount()) + ",";
    json += "\"acquisition_timeout_count\":" + String(scale.getAcquisitionTimeoutCount()) + ",";
    json += "\"quality_score\":" + String(scale.getScaleQualityScore()) + ",";
    json += "\"lifetime_quality_score\":" + String(scale.getLifetimeQualityScore()) + ",";
    json += "\"bump_count\":" + String(scale.getBumpCount()) + ",";
    json += "\"recent_bump\":" + String(scale.hasRecentBump() ? "true" : "false") + ",";
    json += "\"last_bump_ms\":" + String(scale.getLastBumpMillis()) + ",";
    json += "\"last_bump_magnitude_g\":" + String(scale.getLastBumpMagnitudeGrams(), 2) + ",";
    json += "\"glitch_count\":" + String(scale.getGlitchCount()) + ",";
    json += "\"recent_glitch\":" + String(scale.hasRecentGlitch() ? "true" : "false") + ",";
    json += "\"last_glitch_ms\":" + String(scale.getLastGlitchMillis()) + ",";
    json += "\"last_glitch_magnitude_g\":" + String(scale.getLastGlitchMagnitudeGrams(), 2) + ",";
    json += "\"lifetime_samples\":" + String(scale.getLifetimeSampleCount()) + ",";
    json += "\"lifetime_long_gaps\":" + String(scale.getLifetimeLongGapCount()) + ",";
    json += "\"lifetime_bumps\":" + String(scale.getLifetimeBumpCount()) + ",";
    json += "\"lifetime_glitches\":" + String(scale.getLifetimeGlitchCount());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/wifi-creds", HTTP_GET, [](AsyncWebServerRequest *request) {
    String ssid = getStoredSSID();
    const bool hasPassword = getStoredPassword().length() > 0;
    String json = "{\"ssid\":\"" + ssid + "\",\"password_configured\":" + String(hasPassword ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/wifi-creds", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();
      
      Serial.println("New WiFi credentials received via web interface");
      
      // Save credentials first
      saveWiFiCredentials(ssid.c_str(), password.c_str());
      
      // Attempt immediate STA connection to avoid needing a reboot
      bool connected = attemptSTAConnection(ssid.c_str(), password.c_str());
      
      if (connected) {
        request->send(200, "application/json", 
          "{\"status\":\"success\",\"message\":\"Connected successfully! AP mode disabled for power savings.\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
      } else {
        // Connection failed - switch back to AP mode
        switchToAPMode();
        request->send(200, "application/json", 
          "{\"status\":\"failed\",\"message\":\"Connection failed. Check credentials and try again. AP mode restored.\"}");
      }
    } else {
      request->send(400, "text/plain", "Missing SSID or password");
    }
  });

  server.on("/api/wifi-creds", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    clearWiFiCredentials();
    request->send(200, "text/plain", "WiFi credentials cleared. Reboot to apply changes.");
  });

  // WiFi Power Management endpoints
  server.on("/api/wifi-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"enabled\":" + String(isWiFiEnabled() ? "true" : "false") + ",";
    json += "\"connected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false");
    if (WiFi.status() == WL_CONNECTED) {
      json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    }
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/wifi-toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool currentlyEnabled = isWiFiEnabled() && WiFi.getMode() != WIFI_OFF;
    
    if (currentlyEnabled) {
      // Send response before disabling WiFi
      request->send(200, "text/plain", "WiFi disabled for battery saving. Device will be inaccessible until WiFi is re-enabled.");
      // Add delay to allow response to be sent, then disable WiFi
      delay(100);
      disableWiFi();
    } else {
      enableWiFi();
      request->send(200, "text/plain", "WiFi enabled");
    }
  });

  server.on("/api/wifi-enable", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("enabled", true)) {
      bool enabled = request->getParam("enabled", true)->value() == "true";
      if (enabled) {
        enableWiFi();
        request->send(200, "text/plain", "WiFi enabled");
      } else {
        // Send response before disabling WiFi
        request->send(200, "text/plain", "WiFi disabled for battery saving. Device will be inaccessible until WiFi is re-enabled.");
        // Add delay to allow response to be sent, then disable WiFi
        delay(100);
        disableWiFi();
      }
    } else {
      request->send(400, "text/plain", "Missing enabled parameter");
    }
  });

  // WiFi Network Scanning
  server.on("/api/wifi-scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    String scanResult = scanWiFiNetworks();
    request->send(200, "application/json", scanResult);
  });

  // Device information endpoint. This endpoint is polled by UI code as
  // static build/board metadata; dynamic heap and diagnostics live on
  // /api/diagnostics/self-test. Keep request handling cheap so web polling does
  // not create 80 SPS acquisition gaps.
  cachedDeviceInfoJson = buildDeviceInfoJson();
  cachedOtaIdleStatusJson = buildOtaStatusJson();

  server.on("/api/device/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedDeviceInfoJson);
  });

  server.on("/api/diagnostics/self-test", HTTP_GET, [&scale, &display, &battery, &bluetoothScale, &diagnosticEvents, &boardHardware](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"firmware\":\"" + String(WMB_PLUS_FIRMWARE_NAME) + "\",";
    json += "\"version\":\"" + String(WEIGHMYBRU_VERSION_STRING) + "\",";
    json += "\"full_version\":\"" + String(WEIGHMYBRU_FULL_VERSION) + "\",";
    json += "\"board\":\"" + String(WEIGHMYBRU_BOARD_NAME) + "\",";
    json += "\"chip_model\":\"" + String(ESP.getChipModel()) + "\",";
    json += "\"cpu_frequency_mhz\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"heap_size\":" + String(ESP.getHeapSize()) + ",";
    json += "\"free_psram\":" + String(ESP.getFreePsram()) + ",";
    json += "\"psram_size\":" + String(ESP.getPsramSize()) + ",";
    json += "\"scale_connected\":" + String(scale.isHX711Connected() ? "true" : "false") + ",";
    json += "\"display_connected\":" + String(display.isConnected() ? "true" : "false") + ",";
    json += "\"ble_connected\":" + String(bluetoothScale.isConnected() ? "true" : "false") + ",";
    json += "\"battery_valid\":" + String(battery.hasValidReading() ? "true" : "false") + ",";
    json += "\"battery_backend\":\"" + battery.getBatteryBackend() + "\",";
    json += "\"battery_percentage\":" + String(battery.getBatteryPercentage()) + ",";
    json += "\"battery_voltage\":" + String(battery.getBatteryVoltage(), 3) + ",";
    json += "\"fuel_gauge\":" + String(battery.hasFuelGauge() ? "true" : "false") + ",";
    json += "\"usb_power_present\":" + String(battery.isUsbPowerPresent() ? "true" : "false") + ",";
    json += "\"usb_only_power\":" + String(battery.isUsbOnlyPower() ? "true" : "false") + ",";
    json += "\"hx711_rate_hz\":" + String(scale.getDetectedSampleRateHz(), 2) + ",";
    json += "\"hx711_rate_mode\":\"" + scale.getDetectedHx711RateMode() + "\",";
    json += "\"scale_quality\":" + String(scale.getScaleQualityScore()) + ",";
    json += "\"lifetime_quality\":" + String(scale.getLifetimeQualityScore()) + ",";
    json += "\"diagnostic_events\":" + diagnosticEvents.toJson(8) + ",";
    json += "\"board_hardware\":" + boardHardware.toJson();
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/diagnostics/events", HTTP_GET, [&diagnosticEvents](AsyncWebServerRequest *request) {
    size_t limit = 64;
    if (request->hasParam("limit")) {
      limit = static_cast<size_t>(request->getParam("limit")->value().toInt());
      if (limit == 0) {
        limit = 64;
      }
      if (limit > 256) {
        limit = 256;
      }
    }
    request->send(200, "application/json", diagnosticEvents.toJson(limit));
  });

  server.on("/api/diagnostics/events/clear", HTTP_POST, [&diagnosticEvents](AsyncWebServerRequest *request) {
    diagnosticEvents.clear();
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Diagnostic event log cleared\"}");
  });

  server.on("/api/board/hardware", HTTP_GET, [&boardHardware](AsyncWebServerRequest *request) {
    request->send(200, "application/json", boardHardware.toJson());
  });

  server.on("/api/board/status-led", HTTP_POST, [&boardHardware](AsyncWebServerRequest *request) {
    if (!request->hasParam("enabled", true) && !request->hasParam("brightness", true)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing enabled or brightness parameter\"}");
      return;
    }
    if (request->hasParam("brightness", true)) {
      const int brightness = request->getParam("brightness", true)->value().toInt();
      if (brightness < 1 || brightness > 64) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"brightness must be 1-64\"}");
        return;
      }
      boardHardware.setRgbStatusLedBrightness(static_cast<uint8_t>(brightness));
    }
    if (request->hasParam("enabled", true)) {
      const String enabledValue = request->getParam("enabled", true)->value();
      const bool enabled = enabledValue == "1" || enabledValue == "true" || enabledValue == "on";
      boardHardware.setRgbStatusLedEnabled(enabled);
    }
    request->send(200, "application/json", boardHardware.toJson());
  });

  server.on("/api/board/antenna", HTTP_POST, [&boardHardware](AsyncWebServerRequest *request) {
    if (!boardHardware.hasAntennaSwitch()) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Antenna switch is not available on this board\"}");
      return;
    }
    if (!request->hasParam("external", true)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing external parameter\"}");
      return;
    }
    const String externalValue = request->getParam("external", true)->value();
    const bool external = externalValue == "1" || externalValue == "true" || externalValue == "on";
    boardHardware.setExternalAntenna(external);
    request->send(200, "application/json", boardHardware.toJson());
  });

  // Web OTA status and upload endpoints
  server.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", otaStatusJson());
  });

  server.on("/api/ota/firmware", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      sendOtaUploadResponse(request);
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      handleOtaUpload(request, filename, index, data, len, final, U_FLASH, "firmware");
    });

  server.on("/api/ota/filesystem", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      sendOtaUploadResponse(request);
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      handleOtaUpload(request, filename, index, data, len, final, U_SPIFFS, "filesystem");
    });

  // Signal strength endpoint for WiFi and Bluetooth monitoring
  server.on("/api/signal-strength", HTTP_GET, [&bluetoothScale](AsyncWebServerRequest *request) {
    String json = "{";
    
    // WiFi signal strength
    json += "\"wifi\":" + getWiFiConnectionInfo() + ",";
    
    // Bluetooth signal strength
    json += "\"bluetooth\":" + bluetoothScale.getBluetoothConnectionInfo();
    
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/decimal-setting", HTTP_GET, [](AsyncWebServerRequest *request) {
    int decimals = getCachedDecimals();
    String json = "{\"decimals\":" + String(decimals) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/decimal-setting", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("decimals", true)) {
      int decimals = request->getParam("decimals", true)->value().toInt();
      if (decimals < 0) decimals = 0;
      if (decimals > 2) decimals = 2;
      setCachedDecimals(decimals);
      request->send(200, "text/plain", "Decimal setting saved.");
    } else {
      request->send(400, "text/plain", "Missing decimals parameter");
    }
  });

  server.on("/api/flowrate", HTTP_GET, [&flowRate](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(flowRate.getFlowRate(), 1));
  });

  // Bluetooth status API
  server.on("/api/bluetooth/status", HTTP_GET, [&bluetoothScale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"connected\":" + String(bluetoothScale.isConnected() ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Filter settings API endpoints
  server.on("/api/filter-settings", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"brewingThreshold\":" + String(scale.getBrewingThreshold(), 2) + ",";
    json += "\"stabilityTimeout\":" + String(scale.getStabilityTimeout()) + ",";
    json += "\"medianSamples\":" + String(scale.getMedianSamples()) + ",";
    json += "\"averageSamples\":" + String(scale.getAverageSamples());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/filter-settings", HTTP_POST, [&scaleCommandQueue](AsyncWebServerRequest *request) {
    String response = "{\"status\":\"success\",\"message\":\"";
    bool updated = false;
    bool updateBrewingThreshold = false;
    float brewingThreshold = 0.0f;
    bool updateStabilityTimeout = false;
    unsigned long stabilityTimeout = 0;
    bool updateMedianSamples = false;
    int medianSamples = 0;
    bool updateAverageSamples = false;
    int averageSamples = 0;
    
    if (request->hasParam("brewingThreshold", true)) {
      if (!parseFiniteFloat(request->getParam("brewingThreshold", true)->value(), brewingThreshold)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid brewing threshold\"}");
        return;
      }
      updateBrewingThreshold = true;
      response += "Brewing threshold updated. ";
      updated = true;
    }
    if (request->hasParam("stabilityTimeout", true)) {
      String timeoutValue = request->getParam("stabilityTimeout", true)->value();
      float parsedTimeout = 0.0f;
      if (!parseFiniteFloat(timeoutValue, parsedTimeout) || parsedTimeout < 0.0f) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid stability timeout\"}");
        return;
      }
      stabilityTimeout = static_cast<unsigned long>(parsedTimeout);
      updateStabilityTimeout = true;
      response += "Stability timeout updated. ";
      updated = true;
    }
    if (request->hasParam("medianSamples", true)) {
      String samplesValue = request->getParam("medianSamples", true)->value();
      float parsedSamples = 0.0f;
      if (!parseFiniteFloat(samplesValue, parsedSamples)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid median sample count\"}");
        return;
      }
      medianSamples = static_cast<int>(parsedSamples);
      updateMedianSamples = true;
      response += "Median samples updated. ";
      updated = true;
    }
    if (request->hasParam("averageSamples", true)) {
      String samplesValue = request->getParam("averageSamples", true)->value();
      float parsedSamples = 0.0f;
      if (!parseFiniteFloat(samplesValue, parsedSamples)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid average sample count\"}");
        return;
      }
      averageSamples = static_cast<int>(parsedSamples);
      updateAverageSamples = true;
      response += "Average samples updated. ";
      updated = true;
    }
    
    if (updated) {
      if (!scaleCommandQueue.requestFilterSettings(updateBrewingThreshold,
                                                   brewingThreshold,
                                                   updateStabilityTimeout,
                                                   stabilityTimeout,
                                                   updateMedianSamples,
                                                   medianSamples,
                                                   updateAverageSamples,
                                                   averageSamples,
                                                   "web-filter")) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid filter setting range\"}");
        return;
      }
      response += "\"}";
      request->send(200, "application/json", response);
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No valid parameters provided\"}");
    }
  });

  // Filter debug endpoint - shows current filter state
  server.on("/api/filter-debug", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"filterState\":\"" + scale.getFilterState() + "\",";
    json += "\"brewingThreshold\":" + String(scale.getBrewingThreshold(), 2) + ",";
    json += "\"stabilityTimeout\":" + String(scale.getStabilityTimeout()) + ",";
    json += "\"medianSamples\":" + String(scale.getMedianSamples()) + ",";
    json += "\"averageSamples\":" + String(scale.getAverageSamples()) + ",";
    json += "\"zeroClamped\":" + String(scale.isZeroClamped() ? "true" : "false") + ",";
    json += "\"autoZeroActive\":" + String(scale.isAutoZeroActive() ? "true" : "false") + ",";
    json += "\"autoZeroCorrection\":" + String(scale.getAutoZeroCorrectionGrams(), 3) + ",";
    json += "\"recentGlitch\":" + String(scale.hasRecentGlitch() ? "true" : "false") + ",";
    json += "\"glitchCount\":" + String(scale.getGlitchCount()) + ",";
    json += "\"currentWeight\":" + String(scale.getCurrentWeight(), 1);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Combined settings endpoint for faster loading
  server.on("/api/settings", HTTP_GET, [&powerManager, &battery, &boardHardware](AsyncWebServerRequest *request) {
    // Get WiFi credentials (from cache)
    String ssid = getStoredSSID();
    const bool hasPassword = getStoredPassword().length() > 0;
    
    // Get decimal setting (from cache)
    int decimals = getCachedDecimals();
    
    // Combine into single JSON response including auto-sleep settings
    String json = "{";
    json += "\"ssid\":\"" + ssid + "\",";
    json += "\"password_configured\":" + String(hasPassword ? "true" : "false") + ",";
    json += "\"decimals\":" + String(decimals) + ",";
    json += "\"autoSleepEnabled\":" + String(powerManager.getAutoSleepEnabled() ? "true" : "false") + ",";
    json += "\"autoSleepTime\":" + String(powerManager.getAutoSleepTime()) + ",";
    json += "\"autoSleepDrift\":" + String(powerManager.getAutoSleepDrift(), 1) + ",";
    json += "\"autoSleepInhibited\":" + String(powerManager.getAutoSleepInhibited() ? "true" : "false") + ",";
    json += "\"autoSleepInhibitReason\":\"" + powerManager.getAutoSleepInhibitReason() + "\",";
    json += "\"usbPowerPresent\":" + String(battery.isUsbPowerPresent() ? "true" : "false") + ",";
    json += "\"usbOnlyPower\":" + String(battery.isUsbOnlyPower() ? "true" : "false") + ",";
    json += "\"batteryCapacityMah\":" + String(battery.getBatteryCapacityMah()) + ",";
    json += "\"criticalShutdownEnabled\":" + String(battery.isCriticalShutdownEnabled() ? "true" : "false") + ",";
    json += "\"criticalShutdownVoltage\":" + String(battery.getCriticalShutdownVoltage(), 2) + ",";
    json += "\"criticalShutdownPercent\":" + String(static_cast<unsigned int>(battery.getCriticalShutdownPercent())) + ",";
    json += "\"criticalRecoveryVoltage\":" + String(battery.getCriticalRecoveryVoltage(), 2) + ",";
    json += "\"criticalRecoveryPercent\":" + String(static_cast<unsigned int>(battery.getCriticalRecoveryPercent())) + ",";
    json += "\"boardRgbStatusLedAvailable\":" + String(boardHardware.hasRgbStatusLed() ? "true" : "false") + ",";
    json += "\"boardRgbStatusLedEnabled\":" + String(boardHardware.isRgbStatusLedEnabled() ? "true" : "false") + ",";
    json += "\"boardRgbStatusLedBrightness\":" + String(boardHardware.getRgbStatusLedBrightness()) + ",";
    json += "\"boardAntennaSwitchAvailable\":" + String(boardHardware.hasAntennaSwitch() ? "true" : "false") + ",";
    json += "\"boardExternalAntennaSelected\":" + String(boardHardware.isExternalAntennaSelected() ? "true" : "false");
    json += "}";
    
    request->send(200, "application/json", json);
  });

  // Emergency NVS reset endpoint (use with caution)
  server.on("/api/reset-nvs", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("confirm", true) && request->getParam("confirm", true)->value() == "yes") {
      Serial.println("Resetting NVS storage...");
      
      // Clear all preferences
      Preferences clearPrefs;
      clearPrefs.begin("wifi", false);
      clearPrefs.clear();
      clearPrefs.end();
      
      clearPrefs.begin("display", false);
      clearPrefs.clear();
      clearPrefs.end();
      
      clearPrefs.begin("scale", false);
      clearPrefs.clear();
      clearPrefs.end();
      
      request->send(200, "text/plain", "NVS storage reset. Device will restart in 3 seconds.");
      
      // Restart the ESP32 after a short delay
      delay(3000);
      ESP.restart();
    } else {
      request->send(400, "text/plain", "Missing confirmation parameter. Use 'confirm=yes' to reset NVS.");
    }
  });

  // ── Auto-sleep API ────────────────────────────────────────────────────────

  server.on("/api/auto-sleep", HTTP_GET, [&powerManager](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"enabled\":"       + String(powerManager.getAutoSleepEnabled() ? "true" : "false") + ",";
    json += "\"timeToSleep\":"   + String(powerManager.getAutoSleepTime()) + ",";
    json += "\"driftIgnore\":"   + String(powerManager.getAutoSleepDrift(), 1) + ",";
    json += "\"inhibited\":"     + String(powerManager.getAutoSleepInhibited() ? "true" : "false") + ",";
    json += "\"inhibitReason\":\"" + powerManager.getAutoSleepInhibitReason() + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/auto-sleep", HTTP_POST, [&powerManager](AsyncWebServerRequest *request) {
    bool updated = false;
    if (request->hasParam("enabled", true)) {
      powerManager.setAutoSleepEnabled(request->getParam("enabled", true)->value() == "true");
      updated = true;
    }
    if (request->hasParam("timeToSleep", true)) {
      int t = request->getParam("timeToSleep", true)->value().toInt();
      if (t >= 10) powerManager.setAutoSleepTime(t);
      updated = true;
    }
    if (request->hasParam("driftIgnore", true)) {
      float d = request->getParam("driftIgnore", true)->value().toFloat();
      if (d >= 0.0f) powerManager.setAutoSleepDrift(d);
      updated = true;
    }
    if (updated) {
      powerManager.saveAutoSleepSettings();
      request->send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No valid parameters provided\"}");
    }
  });

  // ── StopMyBru (SMB) API ──────────────────────────────────────────────────

  server.on("/api/smb/status", HTTP_GET, [&smb](AsyncWebServerRequest *request) {
    const uint8_t *mac = smb.getSmbMac();
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String json = "{";
    json += "\"paired\":" + String(smb.isPaired() ? "true" : "false") + ",";
    json += "\"pairingState\":" + String((int)smb.getPairingState()) + ",";
    json += "\"smbMac\":\"" + String(macStr) + "\",";
    json += "\"relayOn\":" + String(smb.getSmbRelayOn() ? "true" : "false") + ",";
    json += "\"setpoint\":" + String(smb.getSetpoint(), 1) + ",";
    json += "\"effectiveSetpoint\":" + String(smb.getEffectiveSetpoint(), 2) + ",";
    json += "\"stopLearningEnabled\":" + String(smb.getStopLearningEnabled() ? "true" : "false") + ",";
    json += "\"learnedStopOffset\":" + String(smb.getLearnedStopOffset(), 2) + ",";
    json += "\"stopLearningObservations\":" + String(smb.getStopLearningObservations()) + ",";
    json += "\"lastStopError\":" + String(smb.getLastStopError(), 2) + ",";
    json += "\"lastStopFinalWeight\":" + String(smb.getLastStopFinalWeight(), 2) + ",";
    json += "\"lastStopCutWeight\":" + String(smb.getLastStopCutWeight(), 2) + ",";
    json += "\"stopLearningAwaitingSettle\":" + String(smb.getStopLearningAwaitingSettle() ? "true" : "false") + ",";
    json += "\"invert\":" + String(smb.isInverted() ? "true" : "false") + ",";
    json += "\"lastWeight\":" + String(smb.getSmbLastWeight(), 1) + ",";
    json += "\"lastSeenMs\":" + String(smb.getLastSeenMs()) + ",";
    json += "\"webhookEnabled\":" + String(smb.getWebhookEnabled() ? "true" : "false") + ",";
    json += "\"webhookProfile\":\"" + jsonEscape(smb.getWebhookProfile()) + "\",";
    json += "\"webhookOnUrl\":\"" + jsonEscape(smb.getWebhookOnUrl()) + "\",";
    json += "\"webhookOffUrl\":\"" + jsonEscape(smb.getWebhookOffUrl()) + "\",";
    json += "\"webhookRelayOn\":" + String(smb.getWebhookRelayOn() ? "true" : "false") + ",";
    json += "\"webhookTargetCutSent\":" + String(smb.getWebhookTargetCutSent() ? "true" : "false") + ",";
    json += "\"webhookWiFiReady\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"webhookLastHttpCode\":" + String(smb.getWebhookLastHttpCode()) + ",";
    json += "\"webhookLastAttemptMs\":" + String(smb.getWebhookLastAttemptMs()) + ",";
    json += "\"webhookLastMessage\":\"" + jsonEscape(smb.getWebhookLastMessage()) + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/smb/pair/start", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    smb.startPairing();
    request->send(200, "text/plain", "Pairing started");
  });

  server.on("/api/smb/pair/status", HTTP_GET, [&smb](AsyncWebServerRequest *request) {
    String state;
    switch (smb.getPairingState()) {
      case SmbPairingState::IDLE:              state = "idle";     break;
      case SmbPairingState::BROADCASTING:      state = "scanning"; break;
      case SmbPairingState::AWAITING_CONFIRM:  state = "found";    break;
      case SmbPairingState::PAIRED:            state = "paired";   break;
      default:                                 state = "unknown";  break;
    }
    const uint8_t *mac = smb.getSmbMac();
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String json = "{\"state\":\"" + state + "\",\"smbMac\":\"" + String(macStr) + "\"}";
    request->send(200, "application/json", json);
  });

  server.on("/api/smb/setpoint", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    if (request->hasParam("value", true)) {
      float val = request->getParam("value", true)->value().toFloat();
      smb.setSetpoint(val);
      request->send(200, "text/plain", "Setpoint updated");
    } else {
      request->send(400, "text/plain", "Missing 'value' parameter");
    }
  });

  server.on("/api/smb/learning", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    if (request->hasParam("enabled", true)) {
      const String value = request->getParam("enabled", true)->value();
      smb.setStopLearningEnabled(value == "true" || value == "1" || value == "on");
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stop target learning updated\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'enabled' parameter\"}");
    }
  });

  server.on("/api/smb/learning/reset", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    smb.resetStopLearning();
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stop target learning reset\"}");
  });

  server.on("/api/smb/relay/on", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    smb.sendRelayOn();
    request->send(200, "text/plain", "Relay on sent");
  });

  server.on("/api/smb/relay/off", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    smb.sendRelayOff();
    request->send(200, "text/plain", "Relay off sent");
  });

  server.on("/api/smb/invert", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    if (request->hasParam("invert", true)) {
      bool inv = request->getParam("invert", true)->value() == "true";
      smb.setInvert(inv);
      request->send(200, "text/plain", "Invert updated");
    } else {
      request->send(400, "text/plain", "Missing 'invert' parameter");
    }
  });

  server.on("/api/smb/webhook", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    const bool enabled =
      request->hasParam("enabled", true) &&
      (request->getParam("enabled", true)->value() == "true" ||
       request->getParam("enabled", true)->value() == "1" ||
       request->getParam("enabled", true)->value() == "on");
    String profile = request->hasParam("profile", true) ? request->getParam("profile", true)->value() : "custom";
    String onUrl = request->hasParam("onUrl", true) ? request->getParam("onUrl", true)->value() : "";
    String offUrl = request->hasParam("offUrl", true) ? request->getParam("offUrl", true)->value() : "";

    profile.trim();
    onUrl.trim();
    offUrl.trim();

    if (profile.length() == 0) profile = "custom";

    if (onUrl.length() > 220 || offUrl.length() > 220) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Webhook URLs must be 220 characters or less\"}");
      return;
    }

    if ((onUrl.length() > 0 && !onUrl.startsWith("http://")) ||
        (offUrl.length() > 0 && !offUrl.startsWith("http://"))) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Use local http:// webhook URLs\"}");
      return;
    }

    smb.setWebhookConfig(enabled, profile, onUrl, offUrl);
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Webhook settings saved\"}");
  });

  server.on("/api/smb/webhook/test/on", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    const bool ok = smb.triggerWebhookRelayOn("manual_test");
    String json = "{";
    json += "\"status\":\"" + String(ok ? "success" : "error") + "\",";
    json += "\"httpCode\":" + String(smb.getWebhookLastHttpCode()) + ",";
    json += "\"message\":\"" + jsonEscape(smb.getWebhookLastMessage()) + "\"";
    json += "}";
    request->send(ok ? 200 : 502, "application/json", json);
  });

  server.on("/api/smb/webhook/test/off", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    const bool ok = smb.triggerWebhookRelayOff("manual_test");
    String json = "{";
    json += "\"status\":\"" + String(ok ? "success" : "error") + "\",";
    json += "\"httpCode\":" + String(smb.getWebhookLastHttpCode()) + ",";
    json += "\"message\":\"" + jsonEscape(smb.getWebhookLastMessage()) + "\"";
    json += "}";
    request->send(ok ? 200 : 502, "application/json", json);
  });

  server.on("/api/smb/unpair", HTTP_POST, [&smb](AsyncWebServerRequest *request) {
    smb.unpair();
    request->send(200, "text/plain", "Unpaired");
  });

  // Explicit MIME handlers for PWA/install metadata. Some browsers are strict
  // about manifest and service worker content types.
  server.on("/manifest.webmanifest", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/manifest.webmanifest", "application/manifest+json");
  });

  server.on("/sw.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/sw.js", "application/javascript");
    response->addHeader("Service-Worker-Allowed", "/");
    request->send(response);
  });

  server.on("/stopmybrew.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/stopmybrew.html", "text/html");
  });

  server.on("/stopmybru.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/stopmybrew.html", "text/html");
  });

  // Serve static files for non-API paths
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // 404 Not Found handler for unmatched routes
  server.onNotFound([](AsyncWebServerRequest *request) {
    String path = request->url();
    // If the request is for an API endpoint that doesn't exist, return 404
    if (path.startsWith("/api/")) {
      request->send(404, "text/plain", "API endpoint not found");
      return;
    }
    // For all other unmatched paths, serve index.html (SPA fallback)
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Add explicit MIME type handlers for font files
  server.on("/css/all.min.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/css/all.min.css", "text/css");
  });
  
  server.on("/js/alpine.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/js/alpine.min.js", "application/javascript");
  });
  
  server.on("/webfonts/fa-solid-900.woff2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/webfonts/fa-solid-900.woff2", "font/woff2");
  });
  
  server.on("/webfonts/fa-regular-400.woff2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/webfonts/fa-regular-400.woff2", "font/woff2");
  });

  // Only start the web server if WiFi is enabled
  if (isWiFiEnabled()) {
    server.begin();
    Serial.println("Web server started - accessible via WiFi");
  } else {
    Serial.println("Web server NOT started - WiFi is disabled for battery saving");
  }
}

void startWebServer() {
  if (isWiFiEnabled()) {
    server.begin();
    Serial.println("Web server started");
  }
}

void stopWebServer() {
  server.end();
  Serial.println("Web server stopped");
}
