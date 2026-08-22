#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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
static bool otaRemoteUpdateAvailable = false;
static bool otaRemotePendingInstall = false;
static bool otaRemoteTaskRunning = false;
static bool otaRemoteLastCheckSuccess = false;
static size_t otaRemoteProgress = 0;
static size_t otaRemoteTotal = 0;
static unsigned long otaRemoteLastCheckedMillis = 0;
static String otaRemoteStatus = "idle";
static String otaRemoteLatestVersion;
static String otaRemoteReleaseUrl;
static String otaRemoteFirmwareName;
static String otaRemoteFirmwareUrl;
static String otaRemoteLittlefsName;
static String otaRemoteLittlefsUrl;
static String otaRemoteMessage = "idle";
static String cachedDeviceInfoJson;
static String cachedOtaIdleStatusJson;
static String cachedDashboardJson[2];
static String cachedBatteryJson[2];
static String cachedDiagnosticsSelfTestJson[2];
static String cachedFilterDebugJson[2];
static String cachedSettingsJson[2];
static String cachedWeightText[2];
static String cachedWeightFastText[2];
static String cachedBrewWeightText[2];
static String cachedBrewStatusJson[2];
static String cachedLiveSnapshotJson[2];
static String cachedPourOverJson[2];
static String cachedScaleStatusJson[2];
static volatile uint8_t cachedDashboardActiveIndex = 0;
static volatile uint8_t cachedRuntimeApiActiveIndex = 0;
static volatile uint8_t cachedLiveApiActiveIndex = 0;
static volatile uint8_t cachedPourOverActiveIndex = 0;
static unsigned long lastDashboardCacheUpdateMs = 0;
static unsigned long lastLiveApiCacheUpdateMs = 0;
static unsigned long lastLiveSseSendMs = 0;
static constexpr unsigned long DASHBOARD_CACHE_INTERVAL_MS = 1000;
static constexpr unsigned long LIVE_API_CACHE_INTERVAL_MS = 50;
static constexpr unsigned long LIVE_SSE_INTERVAL_MS = 100;

static String jsonEscape(const String& input);
static String jsonNumberOrNull(float value, unsigned int decimals = 3);
static bool parseFiniteFloat(const String& input, float& output);
static bool isSaneScaleCalibrationFactor(float value);
static bool firmwareOtaSupported();
static const esp_partition_t* filesystemPartition();

static String buildPourOverJson(const PourOverSession& session) {
  String json;
  json.reserve(2600);
  const PourOverRecipe& recipe = session.recipe();
  const PourOverStage* current = session.currentStage();
  json += "{\"supported\":true";
  json += ",\"status\":\"" + String(PourOverSession::statusName(session.status())) + "\"";
  json += ",\"transition_count\":" + String(session.transitionCount());
  json += ",\"current_stage_index\":" + String(session.currentStageIndex());
  json += ",\"stage_elapsed_ms\":" + String(session.stageElapsedMs());
  json += ",\"total_elapsed_ms\":" + String(session.totalElapsedMs());
  json += ",\"stage_baseline_weight_g\":" + String(session.stageBaselineWeightGrams(), 2);
  json += ",\"current_added_g\":" + String(session.currentAddedGrams(), 2);
  json += ",\"weight_g\":" + String(session.currentWeightGrams(), 2);
  json += ",\"flow_gps\":" + String(session.currentFlowGramsPerSecond(), 2);
  json += ",\"peak_flow_gps\":" + String(session.peakFlowGramsPerSecond(), 2);
  json += ",\"average_stage_flow_gps\":" + String(session.averageStageFlowGramsPerSecond(), 2);
  json += ",\"recipe\":{\"name\":\"" + jsonEscape(String(recipe.name)) + "\"";
  json += ",\"stage_count\":" + String(recipe.stageCount) + ",\"stages\":[";
  for (uint8_t i = 0; i < recipe.stageCount; i++) {
    if (i > 0) json += ',';
    const PourOverStage& stage = recipe.stages[i];
    const PourOverStageResult& result = session.result(i);
    json += "{\"type\":\"" + String(PourOverSession::stageTypeName(stage.type)) + "\"";
    json += ",\"name\":\"" + jsonEscape(String(stage.name)) + "\"";
    json += ",\"target_g\":" + String(stage.targetGrams, 2);
    json += ",\"duration_ms\":" + String(stage.durationMs);
    json += ",\"flow_min_gps\":" + String(stage.flowMin, 2);
    json += ",\"flow_max_gps\":" + String(stage.flowMax, 2);
    json += ",\"auto_advance\":" + String(stage.autoAdvance ? "true" : "false");
    json += ",\"result\":";
    if (!result.complete) {
      json += "null";
    } else {
      json += "{\"actual_added_g\":" + String(result.actualAddedGrams, 2);
      json += ",\"duration_ms\":" + String(result.durationMs);
      json += ",\"average_flow_gps\":" + String(result.averageFlow, 2);
      json += ",\"completed_at_ms\":" + String(result.completedAtMs) + "}";
    }
    json += "}";
  }
  json += "]}";
  if (current != nullptr) {
    json += ",\"current_stage_type\":\"" + String(PourOverSession::stageTypeName(current->type)) + "\"";
    json += ",\"current_stage_name\":\"" + jsonEscape(String(current->name)) + "\"";
    json += ",\"current_stage_target_g\":" + String(current->targetGrams, 2);
    json += ",\"current_stage_duration_ms\":" + String(current->durationMs);
  }
  json += "}";
  return json;
}

static String buildPourOverLiveJson(const PourOverSession& session) {
  String json;
  json.reserve(520);
  const PourOverStage* current = session.currentStage();
  json += "{\"supported\":true";
  json += ",\"status\":\"" + String(PourOverSession::statusName(session.status())) + "\"";
  json += ",\"transition_count\":" + String(session.transitionCount());
  json += ",\"current_stage_index\":" + String(session.currentStageIndex());
  json += ",\"stage_elapsed_ms\":" + String(session.stageElapsedMs());
  json += ",\"total_elapsed_ms\":" + String(session.totalElapsedMs());
  json += ",\"stage_baseline_weight_g\":" + String(session.stageBaselineWeightGrams(), 2);
  json += ",\"current_added_g\":" + String(session.currentAddedGrams(), 2);
  json += ",\"peak_flow_gps\":" + String(session.peakFlowGramsPerSecond(), 2);
  if (current != nullptr) {
    json += ",\"current_stage_type\":\"" + String(PourOverSession::stageTypeName(current->type)) + "\"";
    json += ",\"current_stage_name\":\"" + jsonEscape(String(current->name)) + "\"";
    json += ",\"current_stage_target_g\":" + String(current->targetGrams, 2);
    json += ",\"current_stage_duration_ms\":" + String(current->durationMs);
  }
  json += "}";
  return json;
}

static void restartAfterOta() {
    ESP.restart();
}

struct ParsedVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int beta = -1;
    int betaPatch = 0;
    bool valid = false;
};

struct OtaReleaseInfo {
    String version;
    String releaseUrl;
    String firmwareName;
    String firmwareUrl;
    String littlefsName;
    String littlefsUrl;
};

struct OtaDownloadRequest {
    String url;
    String version;
    String filename;
};

static String boardReleaseSuffix() {
#if defined(BOARD_XIAO)
    return "xiao";
#elif defined(BOARD_SUPERMINI)
    return "supermini";
#elif defined(BOARD_TINYS3D)
    return "tinys3d";
#else
    return "";
#endif
}

static ParsedVersion parseVersionKey(String version) {
    ParsedVersion parsed;
    version.trim();
    if (version.startsWith("v") || version.startsWith("V")) {
        version.remove(0, 1);
    }

    int firstDash = version.indexOf('-');
    String core = firstDash >= 0 ? version.substring(0, firstDash) : version;
    int firstDot = core.indexOf('.');
    int secondDot = firstDot >= 0 ? core.indexOf('.', firstDot + 1) : -1;
    if (firstDot < 0 || secondDot < 0) {
        return parsed;
    }

    parsed.major = core.substring(0, firstDot).toInt();
    parsed.minor = core.substring(firstDot + 1, secondDot).toInt();
    parsed.patch = core.substring(secondDot + 1).toInt();
    parsed.beta = 999999;
    parsed.valid = true;

    const int betaPos = version.indexOf("beta");
    if (betaPos >= 0) {
        parsed.beta = 0;
        int numberStart = betaPos + 4;
        while (numberStart < static_cast<int>(version.length()) &&
               (version[numberStart] == '.' || version[numberStart] == '-' || version[numberStart] == '_')) {
            numberStart++;
        }
        int numberEnd = numberStart;
        while (numberEnd < static_cast<int>(version.length()) && isdigit(version[numberEnd])) {
            numberEnd++;
        }
        if (numberEnd > numberStart) {
            parsed.beta = version.substring(numberStart, numberEnd).toInt();
        }
        int tailPos = numberEnd;
        while (tailPos < static_cast<int>(version.length()) && !isdigit(version[tailPos])) {
            tailPos++;
        }
        int tailEnd = tailPos;
        while (tailEnd < static_cast<int>(version.length()) && isdigit(version[tailEnd])) {
            tailEnd++;
        }
        if (tailEnd > tailPos) {
            parsed.betaPatch = version.substring(tailPos, tailEnd).toInt();
        }
    }

    return parsed;
}

static bool isRemoteVersionNewer(const String& remoteVersion, const String& currentVersion) {
    ParsedVersion remote = parseVersionKey(remoteVersion);
    ParsedVersion current = parseVersionKey(currentVersion);
    if (!remote.valid || !current.valid) {
        return remoteVersion != currentVersion && remoteVersion.length() > 0;
    }

    if (remote.major != current.major) return remote.major > current.major;
    if (remote.minor != current.minor) return remote.minor > current.minor;
    if (remote.patch != current.patch) return remote.patch > current.patch;
    if (remote.beta != current.beta) return remote.beta > current.beta;
    if (remote.betaPatch != current.betaPatch) return remote.betaPatch > current.betaPatch;
    return false;
}

static String extractJsonStringAfter(const String& text, int start, const char* key) {
    const int keyPos = text.indexOf(key, start);
    if (keyPos < 0) {
        return "";
    }
    const int colonPos = text.indexOf(':', keyPos);
    if (colonPos < 0) {
        return "";
    }
    int quotePos = text.indexOf('"', colonPos + 1);
    if (quotePos < 0) {
        return "";
    }
    String value;
    value.reserve(64);
    bool escaped = false;
    for (int i = quotePos + 1; i < static_cast<int>(text.length()); i++) {
        const char c = text[i];
        if (escaped) {
            value += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            value += c;
        }
    }
    return value;
}

static bool parseLatestRelease(const String& payload, OtaReleaseInfo& info, String& error) {
    const String suffix = boardReleaseSuffix();
    if (suffix.length() == 0) {
        error = "Unknown board; cannot choose a release asset";
        return false;
    }

    String version = extractJsonStringAfter(payload, 0, "\"version\"");
    String tag = extractJsonStringAfter(payload, 0, "\"tag\"");
    if (version.length() == 0 && tag.length() > 0) {
        version = tag.startsWith("v") || tag.startsWith("V") ? tag.substring(1) : tag;
    }
    if (tag.length() == 0 && version.length() > 0) {
        tag = "v" + version;
    }

    const bool wmbBeta = version.startsWith("0.2.0-beta.") || tag.startsWith("v0.2.0-beta.");
    if (!wmbBeta) {
        error = "No WMB+ beta release found";
        return false;
    }

    String releaseUrl = extractJsonStringAfter(payload, 0, "\"release_url\"");
    if (releaseUrl.length() == 0) {
        releaseUrl = "https://github.com/danielfcurrie-alt/weighmybru2/releases/tag/" + tag;
    }
    String assetsBaseUrl = extractJsonStringAfter(payload, 0, "\"assets_base_url\"");
    if (assetsBaseUrl.length() == 0) {
        assetsBaseUrl = "https://github.com/danielfcurrie-alt/weighmybru2/releases/download/" + tag;
    }
    if (assetsBaseUrl.endsWith("/")) {
        assetsBaseUrl.remove(assetsBaseUrl.length() - 1);
    }

    info.version = version;
    info.releaseUrl = releaseUrl;
    info.firmwareName = "wmb-plus-" + version + "-" + suffix + "-app.bin";
    info.firmwareUrl = assetsBaseUrl + "/" + info.firmwareName;
    info.littlefsName = "wmb-plus-" + version + "-" + suffix + "-littlefs.bin";
    info.littlefsUrl = assetsBaseUrl + "/" + info.littlefsName;
    return true;
}

static String updateCheckEndpoint() {
    return "https://raw.githubusercontent.com/danielfcurrie-alt/weighmybru2/wmb-plus/beta-0.2.0/ota/wmb-plus-beta-latest.json?_=" + String(millis());
}

static bool fetchLatestOtaRelease(OtaReleaseInfo& info, String& error) {
    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi is not connected";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(12000);
    http.useHTTP10(true);
    if (!http.begin(client, updateCheckEndpoint())) {
        error = "Could not start release check";
        return false;
    }
    http.addHeader("User-Agent", "WMBPlus-OTA");
    http.addHeader("Accept", "application/json");
    http.addHeader("Cache-Control", "no-cache");
    http.addHeader("Pragma", "no-cache");

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = "Release check failed: HTTP " + String(code);
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    if (contentLength > 0 && contentLength > 8192) {
        error = "Release check response too large";
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    if (payload.length() == 0) {
        error = "Release check returned an empty response";
        return false;
    }

    return parseLatestRelease(payload, info, error);
}

static String performOtaReleaseCheck() {
    otaRemoteStatus = "checking";
    otaRemoteMessage = "Checking WMB+ releases";
    otaRemoteLastCheckSuccess = false;
    otaRemoteUpdateAvailable = false;
    otaRemoteProgress = 0;
    otaRemoteTotal = 0;

    OtaReleaseInfo info;
    String error;
    if (!fetchLatestOtaRelease(info, error)) {
        otaRemoteStatus = "error";
        otaRemoteMessage = error;
        return error;
    }

    otaRemoteLastCheckedMillis = millis();
    otaRemoteLastCheckSuccess = true;
    otaRemoteLatestVersion = info.version;
    otaRemoteReleaseUrl = info.releaseUrl;
    otaRemoteFirmwareName = info.firmwareName;
    otaRemoteFirmwareUrl = info.firmwareUrl;
    otaRemoteLittlefsName = info.littlefsName;
    otaRemoteLittlefsUrl = info.littlefsUrl;
    otaRemoteUpdateAvailable = isRemoteVersionNewer(info.version, WEIGHMYBRU_VERSION_STRING);
    otaRemoteStatus = otaRemoteUpdateAvailable ? "available" : "current";
    otaRemoteMessage = otaRemoteUpdateAvailable
        ? "Update available: " + info.version
        : "No newer WMB+ firmware found";
    return "";
}

static void remoteReleaseCheckTask(void* parameter) {
    (void)parameter;
    Serial.println("OTA self-update check start");
    const String error = performOtaReleaseCheck();
    if (error.length() > 0) {
        Serial.println("OTA self-update check failed: " + error);
    } else {
        Serial.println("OTA self-update check complete: " + otaRemoteMessage);
    }
    otaRemoteTaskRunning = false;
    vTaskDelete(nullptr);
}

static bool startRemoteReleaseCheck(String& error) {
    if (otaRemoteTaskRunning) {
        error = "Another OTA task is already running";
        return false;
    }

    otaRemoteTaskRunning = true;
    otaRemoteStatus = "checking";
    otaRemoteMessage = "Checking WMB+ releases";
    otaRemoteLastCheckSuccess = false;
    otaRemoteUpdateAvailable = false;
    otaRemoteProgress = 0;
    otaRemoteTotal = 0;

    BaseType_t taskStarted = xTaskCreatePinnedToCore(
      remoteReleaseCheckTask,
      "ota-check",
      12288,
      nullptr,
      1,
      nullptr,
      1);
    if (taskStarted != pdPASS) {
      otaRemoteTaskRunning = false;
      otaRemoteStatus = "error";
      otaRemoteMessage = "Could not start OTA check task";
      error = otaRemoteMessage;
      return false;
    }

    return true;
}

static void clearUpdateErrorOnManualUploadStart() {
    if (otaRemoteStatus == "error") {
        otaRemoteStatus = otaRemotePendingInstall ? "pending_install" : "idle";
    }
}

static bool beginRemoteFirmwareUpdate(size_t contentLength) {
    if (!firmwareOtaSupported()) {
        otaRemoteMessage = "Firmware OTA requires a dual-OTA partition table";
        return false;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        otaRemoteMessage = String("Update begin failed: ") + Update.errorString();
        Update.printError(Serial);
        return false;
    }
    otaRemoteProgress = 0;
    otaRemoteTotal = contentLength;
    return true;
}

static bool streamRemoteFirmware(const String& url) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, url)) {
        otaRemoteMessage = "Could not start firmware download";
        return false;
    }
    http.addHeader("User-Agent", "WMBPlus-OTA");

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        otaRemoteMessage = "Firmware download failed: HTTP " + String(code);
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    if (!beginRemoteFirmwareUpdate(contentLength > 0 ? static_cast<size_t>(contentLength) : 0)) {
        http.end();
        return false;
    }

    uint8_t buffer[1024];
    WiFiClient* stream = http.getStreamPtr();
    uint32_t lastProgressLog = millis();
    while (http.connected()) {
        size_t available = stream->available();
        if (available == 0) {
            if (contentLength > 0 && otaRemoteProgress >= static_cast<size_t>(contentLength)) {
                break;
            }
            delay(1);
            continue;
        }

        if (available > sizeof(buffer)) {
            available = sizeof(buffer);
        }
        const int bytesRead = stream->readBytes(buffer, available);
        if (bytesRead <= 0) {
            delay(1);
            continue;
        }

        const size_t written = Update.write(buffer, static_cast<size_t>(bytesRead));
        otaRemoteProgress += written;
        if (written != static_cast<size_t>(bytesRead)) {
            otaRemoteMessage = String("Update write failed: ") + Update.errorString();
            Update.printError(Serial);
            http.end();
            return false;
        }

        const uint32_t now = millis();
        if (now - lastProgressLog >= 2000) {
            Serial.printf("OTA download progress: %u/%u bytes\n",
                          static_cast<unsigned>(otaRemoteProgress),
                          static_cast<unsigned>(otaRemoteTotal));
            lastProgressLog = now;
        }
        delay(1);
    }

    http.end();
    if (!Update.end(true)) {
        otaRemoteMessage = String("Update end failed: ") + Update.errorString();
        Update.printError(Serial);
        return false;
    }

    return true;
}

static void remoteFirmwareDownloadTask(void* parameter) {
    OtaDownloadRequest* request = static_cast<OtaDownloadRequest*>(parameter);
    otaRemoteStatus = "downloading";
    otaRemoteMessage = "Downloading " + request->filename;
    otaRemoteProgress = 0;
    otaRemoteTotal = 0;
    otaRemotePendingInstall = false;
    otaRemoteTaskRunning = true;

    Serial.printf("OTA self-update download start: version=%s file=%s\n",
                  request->version.c_str(),
                  request->filename.c_str());

    const bool ok = streamRemoteFirmware(request->url);
    if (ok) {
        otaRemotePendingInstall = true;
        otaRemoteUpdateAvailable = false;
        otaRemoteStatus = "pending_install";
        otaRemoteMessage = "Firmware downloaded; install pending";
        otaLastSuccess = true;
        otaLastMessage = otaRemoteMessage;
        otaUploadTarget = "firmware-download";
        otaLastFilename = request->filename;
        otaUploadProgress = otaRemoteProgress;
        otaUploadTotal = otaRemoteTotal;
        Serial.println("OTA self-update staged; reboot pending");
    } else {
        Update.abort();
        otaRemoteStatus = "error";
        otaLastSuccess = false;
        otaLastMessage = otaRemoteMessage;
        Serial.println("OTA self-update download failed: " + otaRemoteMessage);
    }

    otaRemoteTaskRunning = false;
    delete request;
    vTaskDelete(nullptr);
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
    json.reserve(1400);
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
    json += "\"lastMessage\":\"" + jsonEscape(otaLastMessage) + "\",";
    json += "\"remoteStatus\":\"" + jsonEscape(otaRemoteStatus) + "\",";
    json += "\"remoteMessage\":\"" + jsonEscape(otaRemoteMessage) + "\",";
    json += "\"remoteTaskRunning\":" + String(otaRemoteTaskRunning ? "true" : "false") + ",";
    json += "\"remoteLastCheckSuccess\":" + String(otaRemoteLastCheckSuccess ? "true" : "false") + ",";
    json += "\"remoteLastCheckedMs\":" + String(otaRemoteLastCheckedMillis) + ",";
    json += "\"updateAvailable\":" + String(otaRemoteUpdateAvailable ? "true" : "false") + ",";
    json += "\"pendingInstall\":" + String(otaRemotePendingInstall ? "true" : "false") + ",";
    json += "\"latestVersion\":\"" + jsonEscape(otaRemoteLatestVersion) + "\",";
    json += "\"releaseUrl\":\"" + jsonEscape(otaRemoteReleaseUrl) + "\",";
    json += "\"firmwareAssetName\":\"" + jsonEscape(otaRemoteFirmwareName) + "\",";
    json += "\"firmwareAssetUrl\":\"" + jsonEscape(otaRemoteFirmwareUrl) + "\",";
    json += "\"littlefsAssetName\":\"" + jsonEscape(otaRemoteLittlefsName) + "\",";
    json += "\"littlefsAssetUrl\":\"" + jsonEscape(otaRemoteLittlefsUrl) + "\",";
    json += "\"remoteProgress\":" + String(otaRemoteProgress) + ",";
    json += "\"remoteTotal\":" + String(otaRemoteTotal);
    json += "}";
    return json;
}

static String otaStatusJson() {
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
        clearUpdateErrorOnManualUploadStart();
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
AsyncEventSource liveEvents("/api/live");
static bool webServerRunning = false;

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
                                 BoardHardware &boardHardware,
                                 PourOverSession &pourOverSession) {
  // Built from loopTask through updateDashboardCache(). AsyncTCP request
  // handlers serve the cached string and avoid touching live acquisition state
  // while the HX711 is running at 80 SPS.
  String json;
  json.reserve(5000);
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
  json += "\"scale_expected_interval_us\":" + String(scale.getSampleIntervalExpectedMicros()) + ",";
  json += "\"scale_min_interval_us\":" + String(scale.getSampleIntervalMinMicros()) + ",";
  json += "\"scale_max_interval_us\":" + String(scale.getSampleIntervalMaxMicros()) + ",";
  json += "\"scale_long_gap_count\":" + String(scale.getSampleIntervalLongGapCount()) + ",";
  json += "\"scale_estimated_lost_cadence_slots\":" + String(scale.getSampleIntervalEstimatedLostCadenceSlots()) + ",";
  json += "\"scale_cadence_stats_count\":" + String(scale.getSampleIntervalStatsCount()) + ",";
  json += "\"acquisition_model\":\"" + String(scale.getAcquisitionModel()) + "\",";
  json += "\"acquisition_poll_count\":" + String(scale.getAcquisitionPollCount()) + ",";
  json += "\"acquisition_ready_count\":" + String(scale.getAcquisitionReadyCount()) + ",";
  json += "\"acquisition_not_ready_count\":" + String(scale.getAcquisitionNotReadyCount()) + ",";
  json += "\"acquisition_accepted_count\":" + String(scale.getAcquisitionAcceptedCount()) + ",";
  json += "\"acquisition_raw_read_count\":" + String(scale.getAcquisitionRawReadCount()) + ",";
  json += "\"acquisition_estimated_lost_cadence_slots\":" + String(scale.getAcquisitionEstimatedLostCadenceSlots()) + ",";
  json += "\"raw_read_sequence\":" + String(scale.getRawReadSequence()) + ",";
  json += "\"raw_read_count\":" + String(scale.getRawReadCount()) + ",";
  json += "\"raw_input_count\":" + String(scale.getRawInputSampleCount()) + ",";
  json += "\"qualified_input_count\":" + String(scale.getQualifiedInputSampleCount()) + ",";
  json += "\"plausibility_rejected_count\":" + String(scale.getPlausibilityRejectedSampleCount()) + ",";
  json += "\"last_raw_input_sequence\":" + String(scale.getLastRawInputSequence()) + ",";
  json += "\"last_qualified_input_sequence\":" + String(scale.getLastQualifiedInputSequence()) + ",";
  json += "\"last_public_raw_input_sequence\":" + String(scale.getLastPublicRawInputSequence()) + ",";
  json += "\"raw_to_public_sequence_gap\":" + String(scale.getRawToPublicSequenceGap()) + ",";
  json += "\"raw_to_public_sequence_lag\":" + String(scale.getRawToPublicSequenceGap()) + ",";
  json += "\"last_raw_input_ms\":" + String(scale.getLastRawInputMillis()) + ",";
  json += "\"last_qualified_input_ms\":" + String(scale.getLastQualifiedInputMillis()) + ",";
  json += "\"last_public_raw_input_ms\":" + String(scale.getLastPublicRawInputMillis()) + ",";
  json += "\"last_raw_input_g\":" + (scale.hasLastRawInputWeight() ? String(scale.getLastRawInputWeightGrams(), 3) : String("null")) + ",";
  json += "\"last_qualified_input_g\":" + (scale.hasLastQualifiedInputWeight() ? String(scale.getLastQualifiedInputWeightGrams(), 3) : String("null")) + ",";
  json += "\"raw_read_avg_interval_us\":" + String(scale.getRawReadIntervalAverageMicros()) + ",";
  json += "\"raw_read_expected_interval_us\":" + String(scale.getRawReadIntervalExpectedMicros()) + ",";
  json += "\"raw_read_min_interval_us\":" + String(scale.getRawReadIntervalMinMicros()) + ",";
  json += "\"raw_read_max_interval_us\":" + String(scale.getRawReadIntervalMaxMicros()) + ",";
  json += "\"raw_read_long_gap_count\":" + String(scale.getRawReadIntervalLongGapCount()) + ",";
  json += "\"raw_read_stats_count\":" + String(scale.getRawReadIntervalStatsCount()) + ",";
  json += "\"raw_read_estimated_lost_cadence_slots\":" + String(scale.getRawReadEstimatedLostCadenceSlots()) + ",";
  json += "\"raw_read_last_duration_us\":" + String(scale.getLastRawReadDurationMicros()) + ",";
  json += "\"raw_read_max_duration_us\":" + String(scale.getMaxRawReadDurationMicros()) + ",";
  json += "\"acquisition_rejected_count\":" + String(scale.getAcquisitionRejectedCount()) + ",";
  json += "\"acquisition_read_error_count\":" + String(scale.getAcquisitionReadErrorCount()) + ",";
  json += "\"acquisition_disconnected_count\":" + String(scale.getAcquisitionDisconnectedCount()) + ",";
  json += "\"acquisition_data_ready_notifications\":" + String(scale.getAcquisitionDataReadyNotificationCount()) + ",";
  json += "\"acquisition_task_wake_count\":" + String(scale.getAcquisitionTaskWakeCount()) + ",";
  json += "\"acquisition_ready_recovered_by_level_poll_count\":" + String(scale.getAcquisitionReadyRecoveredByLevelPollCount()) + ",";
  json += "\"acquisition_dout_high_timeout_count\":" + String(scale.getAcquisitionDoutHighTimeoutCount()) + ",";
  json += "\"acquisition_spurious_ready_count\":" + String(scale.getAcquisitionSpuriousReadyCount()) + ",";
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
#if HAS_I2C_FUEL_GAUGE
  json += ",\"battery_gauge_rate_percent_per_hour\":" + String(battery.getFuelGaugeChargeRatePercentPerHour(), 3);
  json += ",\"battery_gauge_alert\":" + String(battery.isFuelGaugeAlertAsserted() ? "true" : "false");
#endif
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
  json += ",\"tiny_color_display_available\":" + String(boardHardware.hasTinyColorDisplay() ? "true" : "false");
  json += ",\"tiny_touch_available\":" + String(boardHardware.hasTinyTouch() ? "true" : "false");
  json += ",\"tiny_accelerometer_available\":" + String(boardHardware.hasTinyAccelerometer() ? "true" : "false");
  json += ",\"tiny_motion_state\":\"" + String(boardHardware.getTinyMotionState()) + "\"";
  json += ",\"tiny_vibration_rms_g\":" + String(boardHardware.getTinyVibrationRmsG(), 4);
  json += ",\"tiny_vibration_energy_g2\":" + String(boardHardware.getTinyVibrationEnergyG2(), 5);
  json += ",\"tiny_quiet_confidence\":" + String(boardHardware.getTinyQuietConfidence(), 3);
  json += ",\"tiny_impact_peak_g\":" + String(boardHardware.getTinyImpactPeakG(), 3);
  json += ",\"tiny_roll_degrees\":" + String(boardHardware.getTinyRollDegrees(), 2);
  json += ",\"tiny_pitch_degrees\":" + String(boardHardware.getTinyPitchDegrees(), 2);
  json += ",\"tiny_impact_count\":" + String(boardHardware.getTinyImpactCount());
  json += ",\"tiny_tap_count\":" + String(boardHardware.getTinyTapCount());
  json += ",\"tiny_double_tap_count\":" + String(boardHardware.getTinyDoubleTapCount());

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  json += ",\"wifi_connected\":" + String(wifiConnected ? "true" : "false");
  json += ",\"wifi_signal_strength\":" + String(wifiConnected ? WiFi.RSSI() : 0);
  json += ",\"wifi_signal_quality\":\"" + getWiFiSignalQuality() + "\"";
  json += ",\"bluetooth_connected\":" + String(bluetoothScale.isConnected() ? "true" : "false");
  json += ",\"bluetooth_signal_strength\":" + String(bluetoothScale.getBluetoothSignalStrength());
  json += ",\"wmb_output_profile\":\"" + String(bluetoothScale.getWmbOutputProfileName()) + "\"";
  json += ",\"wmb_output_rate_hz\":" + String(bluetoothScale.getWmbOutputRateHz());
  json += ",\"wmb_output_interval_ms\":" + String(bluetoothScale.getWmbOutputIntervalMillis());
  json += ",\"wmb_effective_output_interval_ms\":" + String(bluetoothScale.getWmbEffectiveOutputIntervalMillis());
  json += ",\"wmb_under_source_rate\":" + String(bluetoothScale.isWmbUnderSourceRate() ? "true" : "false");
  json += ",\"wmb_last_weight_g\":" + String(bluetoothScale.getLastWmbCleanWeight(), 3);
  json += ",\"wmb_source_valid\":" + String(bluetoothScale.hasLastWmbSelectedSource() ? "true" : "false");
  json += ",\"wmb_source_sample_count\":" + String(bluetoothScale.getLastWmbSelectedSourceCount());
  json += ",\"wmb_source_sequence\":" + String(bluetoothScale.getLastWmbSelectedSourceSequence());
  json += ",\"wmb_source_sample_ms\":" + (bluetoothScale.hasLastWmbSelectedSource() ? String(bluetoothScale.getLastWmbSelectedSourceMillis()) : String("null"));
  json += ",\"wmb_source_age_ms\":" + (bluetoothScale.hasLastWmbSelectedSource() ? String(bluetoothScale.getLastWmbSelectedSourceAgeMs()) : String("null"));
  json += ",\"wmb_source_window_range_g\":" + String(bluetoothScale.getLastWmbSelectedWindowRangeGrams(), 3);
  json += ",\"wmb_source_stale\":" + String(bluetoothScale.isLastWmbSourceStale() ? "true" : "false");
  json += ",\"wmb_stale_source_count\":" + String(bluetoothScale.getWmbStaleSourceCount());
  json += ",\"wmb_last_selection_suspect\":" + String(bluetoothScale.wasLastWmbSelectionSuspect() ? "true" : "false");
  json += ",\"wmb_clean_flowrate\":" + String(bluetoothScale.getLastWmbCleanFlowRate(), 2);
  json += ",\"wmb_clean_flow_valid\":" + String(bluetoothScale.isLastWmbFlowValid() ? "true" : "false");
  json += ",\"float32_notify_count\":" + String(bluetoothScale.getFloat32NotifyCount());
  json += ",\"float32_notify_drop_count\":" + String(bluetoothScale.getFloat32NotifyDropCount());
  json += ",\"float32_last_weight_g\":" + String(bluetoothScale.getLastFloat32Weight(), 3);
  json += ",\"float32_source_valid\":" + String(bluetoothScale.hasLastFloat32SelectedSource() ? "true" : "false");
  json += ",\"float32_source_sample_count\":" + String(bluetoothScale.getLastFloat32SelectedSourceCount());
  json += ",\"float32_source_sequence\":" + String(bluetoothScale.getLastFloat32SelectedSourceSequence());
  json += ",\"float32_source_age_ms\":" + (bluetoothScale.hasLastFloat32SelectedSource() ? String(bluetoothScale.getLastFloat32SelectedSourceAgeMs()) : String("null"));
  json += ",\"float32_source_window_ms\":" + String(bluetoothScale.getFloat32SelectionWindowMillis());
  json += ",\"float32_source_window_range_g\":" + String(bluetoothScale.getLastFloat32SelectedWindowRangeGrams(), 3);
  json += ",\"float32_source_limited\":" + String(bluetoothScale.isLastFloat32SourceLimited() ? "true" : "false");
  json += ",\"float32_source_age_over_tick\":" + String(bluetoothScale.isLastFloat32SourceAgeOverTick() ? "true" : "false");
  json += ",\"float32_source_reused\":" + String(bluetoothScale.isLastFloat32SourceReused() ? "true" : "false");
  json += ",\"float32_source_stale\":" + String(bluetoothScale.isLastFloat32SourceStale() ? "true" : "false");
  json += ",\"float32_stale_source_count\":" + String(bluetoothScale.getFloat32StaleSourceCount());
  json += ",\"float32_last_emission_ms\":" + String(bluetoothScale.getLastFloat32NotifyMillis());
  json += ",\"float32_last_estimator_ms\":" + String(bluetoothScale.getLastFloat32EstimatorMillis());
  json += ",\"float32_last_schedule_ms\":" + String(bluetoothScale.getLastFloat32ScheduleMillis());
  json += ",\"float32_last_selection_suspect\":" + String(bluetoothScale.wasLastFloat32SelectionSuspect() ? "true" : "false");
  json += ",\"float32_suspect_selection_count\":" + String(bluetoothScale.getFloat32SuspectSelectionCount());
  json += ",\"float32_last_confirmed_load_step\":" + String(bluetoothScale.wasLastFloat32ConfirmedLoadStep() ? "true" : "false");
  json += ",\"float32_confirmed_load_step_count\":" + String(bluetoothScale.getFloat32ConfirmedLoadStepCount());

  json += ",\"device_version\":\"" + String(WEIGHMYBRU_VERSION_STRING) + "\"";
  json += ",\"device_board\":\"" + String(WEIGHMYBRU_BOARD_NAME) + "\"";
  json += ",\"device_build_date\":\"" + String(WEIGHMYBRU_BUILD_DATE) + "\"";
  json += ",\"device_build_time\":\"" + String(WEIGHMYBRU_BUILD_TIME) + "\"";
  json += ",\"device_build_number\":" + String(WEIGHMYBRU_BUILD_NUMBER);
  json += ",\"device_commit_hash\":\"" + String(WEIGHMYBRU_COMMIT_HASH) + "\"";
  json += ",\"device_full_version\":\"" + String(WEIGHMYBRU_FULL_VERSION) + "\"";
  json += ",\"pour_over\":" + buildPourOverJson(pourOverSession);

  json += "}";
  return json;
}

static String cachedJsonOrWarming(const String cache[2], uint8_t activeIndex) {
  return cache[activeIndex].length() > 0
      ? cache[activeIndex]
      : String("{\"status\":\"warming\"}");
}

static String cachedTextOrZero(const String cache[2], uint8_t activeIndex) {
  return cache[activeIndex].length() > 0 ? cache[activeIndex] : String("0.00");
}

static String buildBrewStatusJson(Scale &scale, FlowRate &flowRate) {
  String json;
  json.reserve(32);
  json += "{\"w\":";
  json += String(scale.getCurrentWeight(), 1);
  json += ",\"f\":";
  json += String(flowRate.getFlowRate(), 1);
  json += "}";
  return json;
}

static String buildLiveSnapshotJson(Scale &scale,
                                    FlowRate &flowRate,
                                    Display &display,
                                    BatteryMonitor &battery,
                                    PourOverSession &pourOverSession) {
  String json;
  json.reserve(360);
  const unsigned long elapsedTime = display.getElapsedTime();
  const unsigned long minutes = elapsedTime / 60000;
  const unsigned long seconds = (elapsedTime % 60000) / 1000;
  const unsigned long milliseconds = elapsedTime % 1000;
  json += "{";
  json += "\"ms\":" + String(millis()) + ",";
  json += "\"seq\":" + String(scale.getSampleSequence()) + ",";
  json += "\"weight\":" + String(scale.getCurrentWeight(), 2) + ",";
  json += "\"flowrate\":" + String(flowRate.getFlowRate(), 2) + ",";
  json += "\"w\":" + String(scale.getCurrentWeight(), 2) + ",";
  json += "\"f\":" + String(flowRate.getFlowRate(), 2) + ",";
  json += "\"timer_running\":" + String(display.isTimerRunning() ? "true" : "false") + ",";
  json += "\"timer_elapsed\":" + String(elapsedTime) + ",";
  json += "\"timer_display\":\"" + String(minutes) + ":" +
          (seconds < 10 ? "0" : "") + String(seconds) + "." +
          (milliseconds < 100 ? (milliseconds < 10 ? "00" : "0") : "") + String(milliseconds) + "\",";
  json += "\"battery_percentage\":" + String(battery.getBatteryPercentage()) + ",";
  json += "\"battery_charging\":" + String(battery.isCharging() ? "true" : "false") + ",";
  json += "\"hx711_connected\":" + String(scale.isHX711Connected() ? "true" : "false") + ",";
  json += "\"scale_connected\":" + String(scale.isHX711Connected() ? "true" : "false") + ",";
  json += "\"hx711_rate_hz\":" + String(scale.getDetectedSampleRateHz(), 2) + ",";
  json += "\"firmware_quality\":" + String(scale.getScaleQualityScore()) + ",";
  json += "\"pour_over\":" + buildPourOverLiveJson(pourOverSession);
  json += "}";
  return json;
}

static String buildBatteryJson(BatteryMonitor &battery) {
  String json;
  json.reserve(3600);
  json += "{";
  json += "\"voltage\":" + String(battery.getBatteryVoltage(), 3);
  json += ",\"percentage\":" + String(battery.getBatteryPercentage());
  json += ",\"raw_percentage\":" + String(battery.getRawBatteryPercentage());
  json += ",\"capacity_mah\":" + String(battery.getBatteryCapacityMah());
  json += ",\"backend\":\"" + battery.getBatteryBackend() + "\"";
  json += ",\"fuel_gauge\":" + String(battery.hasFuelGauge() ? "true" : "false");
  json += ",\"fuel_gauge_soc\":" + String(battery.getFuelGaugeStateOfCharge(), 2);
#if HAS_I2C_FUEL_GAUGE
  json += ",\"fuel_gauge_version\":" + String(battery.getFuelGaugeVersion());
  json += ",\"fuel_gauge_charge_rate_percent_per_hour\":" + String(battery.getFuelGaugeChargeRatePercentPerHour(), 3);
  json += ",\"fuel_gauge_status\":" + String(battery.getFuelGaugeStatus());
  json += ",\"fuel_gauge_configuration\":" + String(battery.getFuelGaugeConfiguration());
  json += ",\"fuel_gauge_alert_asserted\":" + String(battery.isFuelGaugeAlertAsserted() ? "true" : "false");
  json += ",\"fuel_gauge_soc_alert_threshold_percent\":" + String(battery.getFuelGaugeSocAlertThresholdPercent());
  json += ",\"fuel_gauge_minimum_voltage_alert\":" + String(battery.getFuelGaugeMinimumVoltageAlert(), 2);
  json += ",\"fuel_gauge_maximum_voltage_alert\":" + String(battery.getFuelGaugeMaximumVoltageAlert(), 2);
  json += ",\"fuel_gauge_communication_errors\":" + String(battery.getFuelGaugeCommunicationErrors());
  json += ",\"fuel_gauge_last_diagnostic_ms\":" + String(battery.getFuelGaugeLastDiagnosticMillis());
  json += ",\"fuel_gauge_rate_fresh\":" + String(battery.hasFreshFuelGaugeRate() ? "true" : "false");
  const int gaugeRuntimeMinutes = battery.getFuelGaugeRateRuntimeMinutes();
  const int gaugeMinutesTo80 = battery.getFuelGaugeRateMinutesTo80();
  const int gaugeMinutesTo100 = battery.getFuelGaugeRateMinutesTo100();
  const int projectedRuntimeWifiOff = battery.getProjectedRuntimeMinutes(false);
  const int projectedRuntimeWifiOn = battery.getProjectedRuntimeMinutes(true);
  const int projectedTo80WifiOff = battery.getProjectedMinutesTo80(false);
  const int projectedTo80WifiOn = battery.getProjectedMinutesTo80(true);
  const int projectedTo100WifiOff = battery.getProjectedMinutesTo100(false);
  const int projectedTo100WifiOn = battery.getProjectedMinutesTo100(true);
  json += ",\"fuel_gauge_rate_runtime_minutes\":";
  json += gaugeRuntimeMinutes >= 0 ? String(gaugeRuntimeMinutes) : "null";
  json += ",\"fuel_gauge_rate_runtime_display\":\"" + formatRuntimeEstimate(gaugeRuntimeMinutes) + "\"";
  json += ",\"fuel_gauge_rate_minutes_to_80\":";
  json += gaugeMinutesTo80 >= 0 ? String(gaugeMinutesTo80) : "null";
  json += ",\"fuel_gauge_rate_minutes_to_100\":";
  json += gaugeMinutesTo100 >= 0 ? String(gaugeMinutesTo100) : "null";
  json += ",\"projection_model\":\"" + String(battery.getBatteryProjectionModel()) + "\"";
  json += ",\"projected_active_current_wifi_off_ma\":" + String(battery.getProjectedActiveCurrentMa(false), 1);
  json += ",\"projected_active_current_wifi_on_ma\":" + String(battery.getProjectedActiveCurrentMa(true), 1);
  json += ",\"projected_net_charge_current_wifi_off_ma\":" + String(battery.getProjectedNetChargeCurrentMa(false), 1);
  json += ",\"projected_net_charge_current_wifi_on_ma\":" + String(battery.getProjectedNetChargeCurrentMa(true), 1);
  json += ",\"projected_charger_current_ma\":" + String(battery.getProjectedChargerCurrentMa(), 1);
  json += ",\"projected_charge_efficiency_percent\":" + String(battery.getProjectedChargeEfficiency() * 100.0f, 1);
  json += ",\"projected_runtime_wifi_off_minutes\":";
  json += projectedRuntimeWifiOff >= 0 ? String(projectedRuntimeWifiOff) : "null";
  json += ",\"projected_runtime_wifi_off_display\":\"" + formatRuntimeEstimate(projectedRuntimeWifiOff) + "\"";
  json += ",\"projected_runtime_wifi_on_minutes\":";
  json += projectedRuntimeWifiOn >= 0 ? String(projectedRuntimeWifiOn) : "null";
  json += ",\"projected_runtime_wifi_on_display\":\"" + formatRuntimeEstimate(projectedRuntimeWifiOn) + "\"";
  json += ",\"projected_minutes_to_80_wifi_off\":";
  json += projectedTo80WifiOff >= 0 ? String(projectedTo80WifiOff) : "null";
  json += ",\"projected_minutes_to_80_wifi_off_display\":\"" + formatRuntimeEstimate(projectedTo80WifiOff) + "\"";
  json += ",\"projected_minutes_to_80_wifi_on\":";
  json += projectedTo80WifiOn >= 0 ? String(projectedTo80WifiOn) : "null";
  json += ",\"projected_minutes_to_80_wifi_on_display\":\"" + formatRuntimeEstimate(projectedTo80WifiOn) + "\"";
  json += ",\"projected_minutes_to_100_wifi_off\":";
  json += projectedTo100WifiOff >= 0 ? String(projectedTo100WifiOff) : "null";
  json += ",\"projected_minutes_to_100_wifi_off_display\":\"" + formatRuntimeEstimate(projectedTo100WifiOff) + "\"";
  json += ",\"projected_minutes_to_100_wifi_on\":";
  json += projectedTo100WifiOn >= 0 ? String(projectedTo100WifiOn) : "null";
  json += ",\"projected_minutes_to_100_wifi_on_display\":\"" + formatRuntimeEstimate(projectedTo100WifiOn) + "\"";
#endif
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
  return json;
}

static String buildDiagnosticsSelfTestJson(Scale &scale,
                                           Display &display,
                                           BatteryMonitor &battery,
                                           BluetoothScale &bluetoothScale,
                                           DiagnosticEventLog &diagnosticEvents,
                                           BoardHardware &boardHardware) {
  String json;
  json.reserve(2400);
  json += "{";
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
  json += "\"display_connected\":" + String((display.isConnected() || boardHardware.hasTinyColorDisplay()) ? "true" : "false") + ",";
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
  return json;
}

static String buildFilterDebugJson(Scale &scale) {
  String json;
  json.reserve(512);
  json += "{";
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
  return json;
}

static String buildSettingsJson(PowerManager &powerManager,
                                BatteryMonitor &battery,
                                BoardHardware &boardHardware) {
  String ssid = getStoredSSID();
  const bool hasPassword = getStoredPassword().length() > 0;
  const int decimals = getCachedDecimals();

  String json;
  json.reserve(900);
  json += "{";
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
  return json;
}

static String buildScaleStatusJson(Scale &scale) {
  String json;
  json.reserve(3400);
  json += "{";
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
  json += "\"expected_interval_us\":" + String(scale.getSampleIntervalExpectedMicros()) + ",";
  json += "\"min_interval_us\":" + String(scale.getSampleIntervalMinMicros()) + ",";
  json += "\"max_interval_us\":" + String(scale.getSampleIntervalMaxMicros()) + ",";
  json += "\"long_gap_count\":" + String(scale.getSampleIntervalLongGapCount()) + ",";
  json += "\"estimated_lost_cadence_slots\":" + String(scale.getSampleIntervalEstimatedLostCadenceSlots()) + ",";
  json += "\"cadence_stats_count\":" + String(scale.getSampleIntervalStatsCount()) + ",";
  json += "\"acquisition_model\":\"" + String(scale.getAcquisitionModel()) + "\",";
  json += "\"acquisition_poll_count\":" + String(scale.getAcquisitionPollCount()) + ",";
  json += "\"acquisition_ready_count\":" + String(scale.getAcquisitionReadyCount()) + ",";
  json += "\"acquisition_not_ready_count\":" + String(scale.getAcquisitionNotReadyCount()) + ",";
  json += "\"acquisition_accepted_count\":" + String(scale.getAcquisitionAcceptedCount()) + ",";
  json += "\"acquisition_raw_read_count\":" + String(scale.getAcquisitionRawReadCount()) + ",";
  json += "\"acquisition_estimated_lost_cadence_slots\":" + String(scale.getAcquisitionEstimatedLostCadenceSlots()) + ",";
  json += "\"raw_read_sequence\":" + String(scale.getRawReadSequence()) + ",";
  json += "\"raw_read_count\":" + String(scale.getRawReadCount()) + ",";
  json += "\"raw_input_count\":" + String(scale.getRawInputSampleCount()) + ",";
  json += "\"qualified_input_count\":" + String(scale.getQualifiedInputSampleCount()) + ",";
  json += "\"plausibility_rejected_count\":" + String(scale.getPlausibilityRejectedSampleCount()) + ",";
  json += "\"last_raw_input_sequence\":" + String(scale.getLastRawInputSequence()) + ",";
  json += "\"last_qualified_input_sequence\":" + String(scale.getLastQualifiedInputSequence()) + ",";
  json += "\"last_public_raw_input_sequence\":" + String(scale.getLastPublicRawInputSequence()) + ",";
  json += "\"raw_to_public_sequence_gap\":" + String(scale.getRawToPublicSequenceGap()) + ",";
  json += "\"raw_to_public_sequence_lag\":" + String(scale.getRawToPublicSequenceGap()) + ",";
  json += "\"last_raw_input_ms\":" + String(scale.getLastRawInputMillis()) + ",";
  json += "\"last_qualified_input_ms\":" + String(scale.getLastQualifiedInputMillis()) + ",";
  json += "\"last_public_raw_input_ms\":" + String(scale.getLastPublicRawInputMillis()) + ",";
  json += "\"last_raw_input_g\":" + (scale.hasLastRawInputWeight() ? String(scale.getLastRawInputWeightGrams(), 3) : String("null")) + ",";
  json += "\"last_qualified_input_g\":" + (scale.hasLastQualifiedInputWeight() ? String(scale.getLastQualifiedInputWeightGrams(), 3) : String("null")) + ",";
  json += "\"raw_read_avg_interval_us\":" + String(scale.getRawReadIntervalAverageMicros()) + ",";
  json += "\"raw_read_expected_interval_us\":" + String(scale.getRawReadIntervalExpectedMicros()) + ",";
  json += "\"raw_read_min_interval_us\":" + String(scale.getRawReadIntervalMinMicros()) + ",";
  json += "\"raw_read_max_interval_us\":" + String(scale.getRawReadIntervalMaxMicros()) + ",";
  json += "\"raw_read_long_gap_count\":" + String(scale.getRawReadIntervalLongGapCount()) + ",";
  json += "\"raw_read_stats_count\":" + String(scale.getRawReadIntervalStatsCount()) + ",";
  json += "\"raw_read_estimated_lost_cadence_slots\":" + String(scale.getRawReadEstimatedLostCadenceSlots()) + ",";
  json += "\"raw_read_last_duration_us\":" + String(scale.getLastRawReadDurationMicros()) + ",";
  json += "\"raw_read_max_duration_us\":" + String(scale.getMaxRawReadDurationMicros()) + ",";
  json += "\"acquisition_rejected_count\":" + String(scale.getAcquisitionRejectedCount()) + ",";
  json += "\"acquisition_read_error_count\":" + String(scale.getAcquisitionReadErrorCount()) + ",";
  json += "\"acquisition_disconnected_count\":" + String(scale.getAcquisitionDisconnectedCount()) + ",";
  json += "\"acquisition_data_ready_notifications\":" + String(scale.getAcquisitionDataReadyNotificationCount()) + ",";
  json += "\"acquisition_task_wake_count\":" + String(scale.getAcquisitionTaskWakeCount()) + ",";
  json += "\"acquisition_ready_recovered_by_level_poll_count\":" + String(scale.getAcquisitionReadyRecoveredByLevelPollCount()) + ",";
  json += "\"acquisition_dout_high_timeout_count\":" + String(scale.getAcquisitionDoutHighTimeoutCount()) + ",";
  json += "\"acquisition_spurious_ready_count\":" + String(scale.getAcquisitionSpuriousReadyCount()) + ",";
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
  return json;
}

void updateDashboardCache(Scale &scale,
                          FlowRate &flowRate,
                          BluetoothScale &bluetoothScale,
                          Display &display,
                          BatteryMonitor &battery,
                          PowerManager &powerManager,
                          DiagnosticEventLog &diagnosticEvents,
                          BoardHardware &boardHardware,
                          PourOverSession &pourOverSession) {
  const unsigned long now = millis();
  if (cachedWeightText[cachedLiveApiActiveIndex].length() == 0 ||
      now - lastLiveApiCacheUpdateMs >= LIVE_API_CACHE_INTERVAL_MS) {
    const uint8_t liveInactiveIndex = cachedLiveApiActiveIndex == 0 ? 1 : 0;
    const float currentWeight = scale.getCurrentWeight();
    cachedWeightText[liveInactiveIndex] = String(currentWeight);
    cachedWeightFastText[liveInactiveIndex] = String(currentWeight, 2);
    cachedBrewWeightText[liveInactiveIndex] = String(currentWeight, 1);
    cachedBrewStatusJson[liveInactiveIndex] = buildBrewStatusJson(scale, flowRate);
    cachedLiveSnapshotJson[liveInactiveIndex] = buildLiveSnapshotJson(scale, flowRate, display, battery, pourOverSession);
    cachedLiveApiActiveIndex = liveInactiveIndex;
    lastLiveApiCacheUpdateMs = now;
    if (now - lastLiveSseSendMs >= LIVE_SSE_INTERVAL_MS) {
      liveEvents.send(cachedLiveSnapshotJson[liveInactiveIndex].c_str(), "snapshot", now, LIVE_SSE_INTERVAL_MS * 3);
      lastLiveSseSendMs = now;
    }
  }

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
      boardHardware,
      pourOverSession);
  cachedPourOverJson[inactiveIndex] = buildPourOverJson(pourOverSession);
  cachedDashboardActiveIndex = inactiveIndex;
  cachedPourOverActiveIndex = inactiveIndex;

  const uint8_t runtimeInactiveIndex = cachedRuntimeApiActiveIndex == 0 ? 1 : 0;
  cachedBatteryJson[runtimeInactiveIndex] = buildBatteryJson(battery);
  cachedDiagnosticsSelfTestJson[runtimeInactiveIndex] = buildDiagnosticsSelfTestJson(
      scale,
      display,
      battery,
      bluetoothScale,
      diagnosticEvents,
      boardHardware);
  cachedFilterDebugJson[runtimeInactiveIndex] = buildFilterDebugJson(scale);
  cachedSettingsJson[runtimeInactiveIndex] = buildSettingsJson(
      powerManager,
      battery,
      boardHardware);
  cachedScaleStatusJson[runtimeInactiveIndex] = buildScaleStatusJson(scale);
  cachedRuntimeApiActiveIndex = runtimeInactiveIndex;
  lastDashboardCacheUpdateMs = now;
}

void setupWebServer(Scale &scale, FlowRate &flowRate, BluetoothScale &bluetoothScale, Display &display, BatteryMonitor &battery, SmbComms &smb, PowerManager &powerManager, DiagnosticEventLog &diagnosticEvents, BoardHardware &boardHardware, BatteryDrainSession &batteryDrainSession, TouchSensor &touchSensor, ScaleCommandQueue &scaleCommandQueue, PourOverSession &pourOverSession, PourOverCommandQueue &pourOverCommandQueue) {
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

  updateDashboardCache(scale, flowRate, bluetoothScale, display, battery, powerManager, diagnosticEvents, boardHardware, pourOverSession);

  // Register API route first. Serve a loop-owned cached dashboard snapshot so
  // browser/PWA polling cannot make AsyncTCP walk live HX711/battery state.
  server.addHandler(&liveEvents);

  server.on("/api/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedDashboardJson, cachedDashboardActiveIndex));
  });

  server.on("/api/pourover/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedPourOverJson, cachedPourOverActiveIndex));
  });

  server.on("/api/pourover/recipe", HTTP_POST, [&pourOverCommandQueue](AsyncWebServerRequest *request) {
    if (!request->hasParam("name", true) || !request->hasParam("stageCount", true)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"name and stageCount are required\"}");
      return;
    }
    PourOverRecipe recipe;
    strlcpy(recipe.name, request->getParam("name", true)->value().c_str(), sizeof(recipe.name));
    const int stageCount = request->getParam("stageCount", true)->value().toInt();
    if (stageCount < 1 || stageCount > static_cast<int>(PourOverRecipe::MAX_STAGES)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"stageCount must be 1-12\"}");
      return;
    }
    recipe.stageCount = static_cast<uint8_t>(stageCount);
    for (uint8_t i = 0; i < recipe.stageCount; i++) {
      const String prefix = "s" + String(i);
      const String typeKey = prefix + "Type";
      const String nameKey = prefix + "Name";
      if (!request->hasParam(typeKey, true) || !request->hasParam(nameKey, true)) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"stage type and name are required\"}");
        return;
      }
      PourOverStage& stage = recipe.stages[i];
      const String type = request->getParam(typeKey, true)->value();
      if (type == "pour") stage.type = PourOverStageType::Pour;
      else if (type == "pause") stage.type = PourOverStageType::Pause;
      else if (type == "agitate") stage.type = PourOverStageType::Agitate;
      else if (type == "drawdown") stage.type = PourOverStageType::Drawdown;
      else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"unknown stage type\"}");
        return;
      }
      strlcpy(stage.name, request->getParam(nameKey, true)->value().c_str(), sizeof(stage.name));
      const String targetKey = prefix + "TargetGrams";
      const String durationKey = prefix + "DurationSec";
      const String flowMinKey = prefix + "FlowMin";
      const String flowMaxKey = prefix + "FlowMax";
      const String autoKey = prefix + "AutoAdvance";
      stage.targetGrams = request->hasParam(targetKey, true) ? request->getParam(targetKey, true)->value().toFloat() : 0.0f;
      const float durationSeconds = request->hasParam(durationKey, true) ? request->getParam(durationKey, true)->value().toFloat() : 0.0f;
      stage.durationMs = durationSeconds > 0.0f ? static_cast<uint32_t>(durationSeconds * 1000.0f) : 0;
      stage.flowMin = request->hasParam(flowMinKey, true) ? request->getParam(flowMinKey, true)->value().toFloat() : 0.0f;
      stage.flowMax = request->hasParam(flowMaxKey, true) ? request->getParam(flowMaxKey, true)->value().toFloat() : 0.0f;
      stage.autoAdvance = !request->hasParam(autoKey, true) || request->getParam(autoKey, true)->value() != "false";
    }
    if (!pourOverCommandQueue.requestRecipe(recipe)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"invalid recipe\"}");
      return;
    }
    request->send(202, "application/json", "{\"status\":\"queued\"}");
  });

  auto registerPourOverCommand = [&pourOverCommandQueue](const char* path, PourOverCommandQueue::Command command) {
    server.on(path, HTTP_POST, [&pourOverCommandQueue, command](AsyncWebServerRequest *request) {
      if (!pourOverCommandQueue.requestCommand(command)) {
        request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"command queue full\"}");
        return;
      }
      request->send(202, "application/json", "{\"status\":\"queued\"}");
    });
  };
  registerPourOverCommand("/api/pourover/start", PourOverCommandQueue::Command::Start);
  registerPourOverCommand("/api/pourover/pause", PourOverCommandQueue::Command::Pause);
  registerPourOverCommand("/api/pourover/resume", PourOverCommandQueue::Command::Resume);
  registerPourOverCommand("/api/pourover/next", PourOverCommandQueue::Command::Next);
  registerPourOverCommand("/api/pourover/previous", PourOverCommandQueue::Command::Previous);
  registerPourOverCommand("/api/pourover/finish", PourOverCommandQueue::Command::Finish);
  registerPourOverCommand("/api/pourover/reset", PourOverCommandQueue::Command::Reset);

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

  server.on("/api/weight", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", cachedTextOrZero(cachedWeightText, cachedLiveApiActiveIndex));
  });

  // Lightweight weight-only endpoint for brewing applications
  server.on("/api/weight-fast", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", cachedTextOrZero(cachedWeightFastText, cachedLiveApiActiveIndex));
  });

  // Brewing mode endpoints for external devices like GaggiMate
  server.on("/api/brew/weight", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", cachedTextOrZero(cachedBrewWeightText, cachedLiveApiActiveIndex));
  });
  
  server.on("/api/brew/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedBrewStatusJson, cachedLiveApiActiveIndex));
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

  server.on("/api/battery/calibrate-full", HTTP_POST, [&battery](AsyncWebServerRequest *request) {
    float fullVoltage = 4.20f;
    if (request->hasParam("voltage", true)) {
      fullVoltage = request->getParam("voltage", true)->value().toFloat();
    } else if (request->hasParam("voltage")) {
      fullVoltage = request->getParam("voltage")->value().toFloat();
    }

    if (fullVoltage < 4.05f || fullVoltage > 4.30f) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Full calibration voltage must be 4.05-4.30V\"}");
      return;
    }

    float beforeVoltage = battery.getBatteryVoltage();
    int beforePercentage = battery.getBatteryPercentage();
    battery.calibrateVoltage(fullVoltage);
    String json = "{";
    json += "\"status\":\"success\",";
    json += "\"message\":\"Battery calibrated as full\",";
    json += "\"before_voltage\":" + String(beforeVoltage, 3) + ",";
    json += "\"before_percentage\":" + String(beforePercentage) + ",";
    json += "\"after_voltage\":" + String(battery.getBatteryVoltage(), 3) + ",";
    json += "\"after_percentage\":" + String(battery.getBatteryPercentage()) + ",";
    json += "\"target_voltage\":" + String(fullVoltage, 3) + ",";
    json += "\"calibration_offset\":" + String(battery.getCalibrationOffset(), 3);
    json += "}";
    request->send(200, "application/json", json);
    Serial.printf("Battery calibrated as full: %.3fV (was %.3fV)\n", fullVoltage, beforeVoltage);
  });

  // Battery monitoring endpoint (general status). Serve the loop-owned cache so
  // AsyncTCP does not race BatteryMonitor's mutable String fields while polling.
  server.on("/api/battery", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedBatteryJson, cachedRuntimeApiActiveIndex));
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

  server.on("/api/battery/benchmark", HTTP_GET, [&battery, &batteryDrainSession, &scale, &display, &bluetoothScale, &boardHardware](AsyncWebServerRequest *request) {
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
    json += ",\"display_connected\":" + String((display.isConnected() || boardHardware.hasTinyColorDisplay()) ? "true" : "false");
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

  server.on("/api/scale/quality/reset", HTTP_POST, [&scale](AsyncWebServerRequest *request){
    scale.resetTransientQualityStats();
    request->send(200, "application/json",
                  "{\"status\":\"ok\",\"bump_count\":0,\"glitch_count\":0,\"recent_bump\":false,\"recent_glitch\":false}");
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
  server.on("/api/scale/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedScaleStatusJson, cachedRuntimeApiActiveIndex));
  });

  server.on("/api/wifi-creds", HTTP_GET, [](AsyncWebServerRequest *request) {
    String ssid = getStoredSSID();
    const bool hasPassword = getStoredPassword().length() > 0;
    String json = "{\"ssid\":\"" + ssid + "\",\"stored_credentials\":" + String(ssid.length() > 0 ? "true" : "false") + ",\"password_configured\":" + String(hasPassword ? "true" : "false") + "}";
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
    json += "\"connected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
    json += "\"stored_credentials\":" + String(hasStoredWiFiCredentials() ? "true" : "false") + ",";
    json += "\"mode\":" + String(static_cast<int>(WiFi.getMode()));
    if (WiFi.status() == WL_CONNECTED) {
      json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    } else {
      String storedSSID = getStoredSSID();
      if (storedSSID.length() > 0) {
        json += ",\"stored_ssid\":\"" + storedSSID + "\"";
      }
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

  server.on("/api/diagnostics/self-test", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedDiagnosticsSelfTestJson, cachedRuntimeApiActiveIndex));
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

  server.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest *request) {
    String error;
    if (!startRemoteReleaseCheck(error)) {
      request->send(error.length() == 0 ? 409 : 500, "application/json", otaStatusJson());
      return;
    }
    request->send(202, "application/json", otaStatusJson());
  });

  server.on("/api/ota/download", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (otaRemoteTaskRunning) {
      request->send(409, "application/json", otaStatusJson());
      return;
    }
    if (otaRemotePendingInstall) {
      otaRemoteStatus = "pending_install";
      otaRemoteMessage = "Firmware already downloaded; install pending";
      request->send(200, "application/json", otaStatusJson());
      return;
    }
    if (!otaRemoteUpdateAvailable || otaRemoteFirmwareUrl.length() == 0) {
      otaRemoteStatus = "idle";
      otaRemoteMessage = "Check for updates before downloading";
      request->send(409, "application/json", otaStatusJson());
      return;
    }

    OtaDownloadRequest* downloadRequest = new OtaDownloadRequest{
      otaRemoteFirmwareUrl,
      otaRemoteLatestVersion,
      otaRemoteFirmwareName
    };

    BaseType_t taskStarted = xTaskCreatePinnedToCore(
      remoteFirmwareDownloadTask,
      "ota-download",
      12288,
      downloadRequest,
      1,
      nullptr,
      1);
    if (taskStarted != pdPASS) {
      delete downloadRequest;
      otaRemoteStatus = "error";
      otaRemoteMessage = "Could not start OTA download task";
      request->send(500, "application/json", otaStatusJson());
      return;
    }
    otaRemoteTaskRunning = true;
    otaRemoteStatus = "downloading";
    otaRemoteMessage = "Download started";
    request->send(202, "application/json", otaStatusJson());
  });

  server.on("/api/ota/install", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!otaRemotePendingInstall) {
      otaRemoteStatus = "idle";
      otaRemoteMessage = "No downloaded firmware is pending install";
      request->send(409, "application/json", otaStatusJson());
      return;
    }
    otaRemoteStatus = "installing";
    otaRemoteMessage = "Installing update; restarting";
    otaLastMessage = otaRemoteMessage;
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", otaStatusJson());
    response->addHeader("Connection", "close");
    request->send(response);
    otaRestartTicker.once(1.5f, restartAfterOta);
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
    json += ",\"wmb_output_profile\":\"" + String(bluetoothScale.getWmbOutputProfileName()) + "\"";
    json += ",\"wmb_output_rate_hz\":" + String(bluetoothScale.getWmbOutputRateHz());
    json += ",\"wmb_effective_output_interval_ms\":" + String(bluetoothScale.getWmbEffectiveOutputIntervalMillis());
    json += ",\"wmb_under_source_rate\":" + String(bluetoothScale.isWmbUnderSourceRate() ? "true" : "false");
    json += ",\"wmb_clean_flow_valid\":" + String(bluetoothScale.isLastWmbFlowValid() ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/bluetooth/wmb-output-rate", HTTP_POST, [&bluetoothScale](AsyncWebServerRequest *request) {
    const bool wantsDiagnosticProfile =
      request->hasParam("profile", true) &&
      request->getParam("profile", true)->value() == "diagnostic-high-rate";
    if (wantsDiagnosticProfile) {
      bluetoothScale.requestWmbDiagnosticHighRateProfile();
      request->send(202, "application/json", "{\"status\":\"queued\",\"wmb_output_profile\":\"diagnostic-high-rate\"}");
      return;
    }
    if (!request->hasParam("rate", true)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"rate is required for clean WMB output\"}");
      return;
    }
    const int rate = request->getParam("rate", true)->value().toInt();
    if (!bluetoothScale.requestWmbCleanOutputRateHz(static_cast<uint8_t>(rate))) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"rate must be 10, 20, 40, or 80\"}");
      return;
    }
    String json = "{\"status\":\"queued\",\"wmb_output_profile\":\"clean\",\"wmb_output_rate_hz\":";
    json += String(rate);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/bluetooth/wmb-output-profile", HTTP_POST, [&bluetoothScale](AsyncWebServerRequest *request) {
    if (!request->hasParam("profile", true)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"profile is required\"}");
      return;
    }
    const String profile = request->getParam("profile", true)->value();
    if (profile == "diagnostic-high-rate") {
      bluetoothScale.requestWmbDiagnosticHighRateProfile();
      request->send(202, "application/json", "{\"status\":\"queued\",\"wmb_output_profile\":\"diagnostic-high-rate\"}");
      return;
    }
    if (profile != "clean") {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"profile must be diagnostic-high-rate or clean\"}");
      return;
    }
    const uint8_t rate = request->hasParam("rate", true)
      ? static_cast<uint8_t>(request->getParam("rate", true)->value().toInt())
      : bluetoothScale.getWmbOutputRateHz();
    if (!bluetoothScale.requestWmbCleanOutputRateHz(rate)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"rate must be 10, 20, 40, or 80\"}");
      return;
    }
    String json = "{\"status\":\"queued\",\"wmb_output_profile\":\"clean\",\"wmb_output_rate_hz\":";
    json += String(rate);
    json += "}";
    request->send(202, "application/json", json);
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

  // Filter debug endpoint - shows current filter state from the loop-owned cache.
  server.on("/api/filter-debug", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedFilterDebugJson, cachedRuntimeApiActiveIndex));
  });

  // Combined settings endpoint for faster loading
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", cachedJsonOrWarming(cachedSettingsJson, cachedRuntimeApiActiveIndex));
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

  server.on("/stopmybru.webmanifest", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/stopmybru.webmanifest", "application/manifest+json");
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
    request->send(LittleFS, "/stopmybru.html", "text/html");
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

  // Only start the web server if WiFi is enabled. AsyncWebServer::begin()
  // is not idempotent, so keep listener lifecycle explicit.
  if (isWiFiEnabled()) {
    startWebServer();
    Serial.println("Web server accessible via WiFi");
  } else {
    Serial.println("Web server NOT started - WiFi is disabled for battery saving");
  }
}

void startWebServer() {
  if (!isWiFiEnabled()) {
    Serial.println("Web server NOT started - WiFi is disabled");
    return;
  }
  if (webServerRunning) {
    Serial.println("Web server already running");
    return;
  }
  server.begin();
  webServerRunning = true;
  Serial.println("Web server started");
}

void stopWebServer() {
  if (!webServerRunning) {
    Serial.println("Web server already stopped");
    return;
  }
  server.end();
  webServerRunning = false;
  Serial.println("Web server stopped");
}
