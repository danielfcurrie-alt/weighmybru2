#include "BluetoothScale.h"
#include "BatteryMonitor.h"
#include "Display.h"
#include "FlowRate.h"
#include "TouchSensor.h"
#include "BoardConfig.h"
#include "Version.h"
#include <Arduino.h>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <esp_bt.h>
#include <math.h>

namespace {
constexpr uint8_t WMB_PLUS_EXTENSION_PACKET_VERSION = 0x01;
constexpr uint8_t WMB_PLUS_EXTENSION_PACKET_LENGTH = 20;

constexpr uint32_t FEATURE_STANDARD_BATTERY_SERVICE = 1UL << 0;
constexpr uint32_t FEATURE_PHYSICAL_PARITY_TARE = 1UL << 1;
constexpr uint32_t FEATURE_ATOMIC_TARE_START = 1UL << 2;
constexpr uint32_t FEATURE_FRESH_SAMPLE_NOTIFY = 1UL << 3;
constexpr uint32_t FEATURE_COMMAND_NOTIFY_ACK = 1UL << 4;
constexpr uint32_t FEATURE_WMB_20_BYTE_WEIGHT = 1UL << 5;
constexpr uint32_t FEATURE_FLOAT32_WEIGHT = 1UL << 6;
constexpr uint32_t FEATURE_HX711_CADENCE_DIAGNOSTICS = 1UL << 7;
constexpr uint32_t FEATURE_EXTENDED_WMB_PACKET = 1UL << 8;
constexpr uint32_t FEATURE_PACKET_DEVICE_TIMESTAMP = 1UL << 9;
constexpr uint32_t FEATURE_PACKET_FLOW = 1UL << 10;
constexpr uint32_t FEATURE_PACKET_BATTERY = 1UL << 11;
constexpr uint32_t FEATURE_PACKET_SEQUENCE = 1UL << 12;
constexpr uint32_t FEATURE_SCALE_QUALITY_DIAGNOSTICS = 1UL << 13;
constexpr uint32_t FEATURE_LIFETIME_QUALITY_DIAGNOSTICS = 1UL << 14;
constexpr uint32_t FEATURE_ZERO_STABILITY_CONTROL = 1UL << 15;
constexpr uint32_t FEATURE_GLITCH_REJECTION = 1UL << 16;
constexpr uint32_t FEATURE_BATTERY_CHARGE_ESTIMATE = 1UL << 17;
constexpr uint32_t FEATURE_LEGACY_FLOAT32_20HZ = 1UL << 18;
constexpr uint32_t FEATURE_FUEL_GAUGE_BATTERY = 1UL << 19;
constexpr uint32_t FEATURE_DIAGNOSTIC_EVENT_LOG = 1UL << 20;
constexpr uint32_t FEATURE_USB_POWER_SENSE = 1UL << 21;
constexpr uint32_t FEATURE_RF_ANTENNA_SWITCH = 1UL << 22;
constexpr uint32_t FEATURE_RGB_STATUS_LED = 1UL << 23;

constexpr uint32_t WMB_PLUS_FEATURE_MASK =
    FEATURE_STANDARD_BATTERY_SERVICE |
    FEATURE_PHYSICAL_PARITY_TARE |
    FEATURE_ATOMIC_TARE_START |
    FEATURE_FRESH_SAMPLE_NOTIFY |
    FEATURE_COMMAND_NOTIFY_ACK |
    FEATURE_WMB_20_BYTE_WEIGHT |
    FEATURE_FLOAT32_WEIGHT |
    FEATURE_HX711_CADENCE_DIAGNOSTICS |
    FEATURE_EXTENDED_WMB_PACKET |
    FEATURE_PACKET_DEVICE_TIMESTAMP |
    FEATURE_PACKET_FLOW |
    FEATURE_PACKET_BATTERY |
    FEATURE_PACKET_SEQUENCE |
    FEATURE_SCALE_QUALITY_DIAGNOSTICS |
    FEATURE_LIFETIME_QUALITY_DIAGNOSTICS |
    FEATURE_ZERO_STABILITY_CONTROL |
    FEATURE_GLITCH_REJECTION |
    FEATURE_BATTERY_CHARGE_ESTIMATE |
    FEATURE_LEGACY_FLOAT32_20HZ
    | FEATURE_DIAGNOSTIC_EVENT_LOG
#if HAS_I2C_FUEL_GAUGE
    | FEATURE_FUEL_GAUGE_BATTERY
#endif
#if HAS_USB_POWER_SENSE
    | FEATURE_USB_POWER_SENSE
#endif
#if HAS_RF_ANTENNA_SWITCH
    | FEATURE_RF_ANTENNA_SWITCH
#endif
#if HAS_BOARD_RGB_STATUS_LED
    | FEATURE_RGB_STATUS_LED
#endif
    ;

constexpr uint8_t STATUS_TIMER_RUNNING = 1U << 0;
constexpr uint8_t STATUS_HX711_CONNECTED = 1U << 1;
constexpr uint8_t STATUS_TARE_PENDING = 1U << 2;
constexpr uint8_t STATUS_ATOMIC_TARE_START_PENDING = 1U << 3;
constexpr uint8_t STATUS_BATTERY_LOW = 1U << 4;
constexpr uint8_t STATUS_BATTERY_CRITICAL = 1U << 5;
constexpr uint8_t STATUS_BATTERY_PRESENT = 1U << 6;
constexpr uint8_t STATUS_DISPLAY_PRESENT = 1U << 7;

constexpr uint8_t DIAG_RECENT_BUMP = 1U << 0;
constexpr uint8_t DIAG_LONG_GAP_SEEN = 1U << 1;
constexpr uint8_t DIAG_CADENCE_VALID = 1U << 2;
constexpr uint8_t DIAG_80SPS_DETECTED = 1U << 3;
constexpr uint8_t DIAG_10SPS_DETECTED = 1U << 4;
constexpr uint8_t DIAG_QUALITY_VALID = 1U << 5;
constexpr uint8_t DIAG_FLOW_PRESENT = 1U << 6;
constexpr uint8_t DIAG_EXTENSION_PRESENT = 1U << 7;

uint16_t clampUnsigned16(int32_t value) {
    if (value <= 0) {
        return 0;
    }
    if (value > 0xFFFF) {
        return 0xFFFF;
    }
    return static_cast<uint16_t>(value);
}

template <typename NotifyReturn>
struct NotifyPayloadAdapter;

template <typename NotifyReturn>
struct NotifyCurrentValueAdapter;

template <>
struct NotifyPayloadAdapter<bool> {
    static bool notify(NimBLECharacteristic* characteristic, const uint8_t* payload, size_t length) {
        return characteristic->notify(payload, length);
    }
};

template <>
struct NotifyCurrentValueAdapter<bool> {
    static bool notify(NimBLECharacteristic* characteristic) {
        return characteristic->notify();
    }
};

template <>
struct NotifyPayloadAdapter<void> {
    static bool notify(NimBLECharacteristic* characteristic, const uint8_t* payload, size_t length) {
        characteristic->notify(payload, length);
        return true;
    }
};

template <>
struct NotifyCurrentValueAdapter<void> {
    static bool notify(NimBLECharacteristic* characteristic) {
        characteristic->notify();
        return true;
    }
};

bool notifyPayloadQueued(NimBLECharacteristic* characteristic, const uint8_t* payload, size_t length) {
    using NotifyReturn = decltype(std::declval<NimBLECharacteristic*>()->notify(
        static_cast<const uint8_t*>(nullptr),
        std::declval<size_t>()));
    return NotifyPayloadAdapter<NotifyReturn>::notify(characteristic, payload, length);
}

bool notifyCurrentValueQueued(NimBLECharacteristic* characteristic) {
    using NotifyReturn = decltype(std::declval<NimBLECharacteristic*>()->notify());
    return NotifyCurrentValueAdapter<NotifyReturn>::notify(characteristic);
}
}

// UUIDs for WeighMyBru protocol - unique to avoid conflicts with Bookoo scales
const char* BluetoothScale::DEVICE_NAME = WMB_PLUS_BLE_DEVICE_NAME;
const char* BluetoothScale::SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* BluetoothScale::WEIGHT_CHARACTERISTIC_UUID = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E";  // Bean Conqueror (new UUID)
const char* BluetoothScale::GAGGIMATE_CHARACTERISTIC_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // GaggiMate (original UUID)
const char* BluetoothScale::COMMAND_CHARACTERISTIC_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
const char* BluetoothScale::CAPABILITIES_CHARACTERISTIC_UUID = "6E400005-B5A3-F393-E0A9-E50E24DCCA9E";
const char* BluetoothScale::BATTERY_SERVICE_UUID = "180F";
const char* BluetoothScale::BATTERY_LEVEL_CHARACTERISTIC_UUID = "2A19";

BluetoothScale::BluetoothScale() 
    : scale(nullptr), display(nullptr), touchSensor(nullptr), batteryMonitor(nullptr), flowRate(nullptr), server(nullptr), service(nullptr), batteryService(nullptr),
      weightCharacteristic(nullptr), gaggiMateWeightCharacteristic(nullptr), 
      commandCharacteristic(nullptr), capabilitiesCharacteristic(nullptr), batteryLevelCharacteristic(nullptr), advertising(nullptr), deviceConnected(false),
      oldDeviceConnected(false), lastHeartbeat(0), lastBatterySent(0),
      lastNotifiedSampleSequence(0), lastNotifiedScaleSampleMillis(0),
      weightNotifyCount(0), weightNotifyDropCount(0),
      wmbOutputProfile(static_cast<uint8_t>(WMB_DEFAULT_OUTPUT_PROFILE)),
      wmbOutputRateHz(WMB_DEFAULT_OUTPUT_RATE_HZ),
      pendingWmbOutputConfig(false),
      pendingWmbOutputProfile(static_cast<uint8_t>(WMB_DEFAULT_OUTPUT_PROFILE)),
      pendingWmbOutputRateHz(WMB_DEFAULT_OUTPUT_RATE_HZ),
      wmbEstimatorTickCount(0),
      lastWmbScheduleMillis(0), lastWmbWeight(0.0f), hasLastWmbWeight(false),
      lastWmbObservedTareMillis(0), wmbConsecutiveSuppressedSelectionCount(0),
      lastWmbSelectedSourceCount(0), lastWmbSelectedSourceSequence(0),
      lastWmbSelectedSourceMillis(0),
      lastWmbSelectedSourceAgeMs(0), lastWmbSelectedWindowRangeGrams(0.0f),
      lastWmbEmittedSourceSequence(0),
      lastWmbSelectedSourceValid(false), lastWmbSelectionSuspect(false),
      lastWmbSourceStale(true), wmbStaleSourceCount(0),
      lastWmbFlowRate(0.0f), lastWmbFlowWeight(0.0f), lastWmbFlowSourceMillis(0),
      lastWmbFlowValid(false),
      float32EstimatorTickCount(0), float32NotifyCount(0), float32NotifyDropCount(0), packetSequence(0), batteryNotifyCount(0),
      lastWeightNotifyMillis(0), lastFloat32NotifyMillis(0), lastFloat32EstimatorMillis(0), lastFloat32ScheduleMillis(0), lastBatteryNotifyMillis(0),
      lastFloat32Weight(0.0f), hasLastFloat32Weight(false),
      lastFloat32ObservedTareMillis(0), float32SuspectSelectionCount(0),
      float32ConfirmedLoadStepCount(0), float32StaleSourceCount(0),
      float32ConsecutiveSuppressedSelectionCount(0),
      lastFloat32SelectedSourceCount(0), lastFloat32SelectedSourceSequence(0),
      lastFloat32SelectedSourceAgeMs(0), lastFloat32SelectedWindowRangeGrams(0.0f),
      lastFloat32SelectedSourceValid(false), lastFloat32SelectionSuspect(false),
      lastFloat32ConfirmedLoadStep(false), lastFloat32SourceStale(false),
      lastBatteryPercent(255),
      connectionRSSI(-100), connectionHandle(0) {
}

BluetoothScale::~BluetoothScale() {
    end();
}

void BluetoothScale::begin(Scale* scaleInstance) {
    scale = scaleInstance;
    
    Serial.println("BluetoothScale: Starting BLE initialization...");
    
    // Check available memory before BLE initialization
    size_t freeHeap = ESP.getFreeHeap();
    Serial.println("BluetoothScale: Free heap before BLE: " + String(freeHeap) + " bytes");
    
    if (freeHeap < 50000) {  // Need at least 50KB for BLE
        Serial.println("BluetoothScale: Insufficient memory for BLE - disabling");
        scale = nullptr;
        return;
    }
    
    // Add comprehensive error handling for BLE initialization
    bool initializationSuccessful = false;
    
    try {
        Serial.println("BluetoothScale: Releasing Classic BT memory...");
        
        // Release Classic Bluetooth memory more carefully
        esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (ret != ESP_OK) {
            Serial.println("BluetoothScale: Warning - could not release Classic BT memory: " + String(esp_err_to_name(ret)));
        }
        
        Serial.println("BluetoothScale: Initializing BLE device directly...");
        
        // Skip btStart() and go directly to BLE initialization
        // This avoids the problematic Bluetooth controller initialization
        initializeBLE();
        
        // Small delay before starting advertising
        delay(200);
        
        startAdvertising();
        
        Serial.printf("BluetoothScale: Successfully started advertising as %s\n", DEVICE_NAME);
        initializationSuccessful = true;
        
    } catch (const std::exception& e) {
        Serial.println("BluetoothScale: Exception during initialization: " + String(e.what()));
        scale = nullptr;
    } catch (...) {
        Serial.println("BluetoothScale: Unknown error during initialization - disabling Bluetooth");
        scale = nullptr;
    }
    
    if (!initializationSuccessful) {
        Serial.println("BluetoothScale: BLE initialization failed - scale will work without Bluetooth");
        // Clean up any partial initialization
        end();
    } else {
        Serial.println("BluetoothScale: BLE initialization completed successfully");
        Serial.println("BluetoothScale: Free heap after BLE: " + String(ESP.getFreeHeap()) + " bytes");
    }
}

void BluetoothScale::end() {
    if (server) {
        stopAdvertising();
        NimBLEDevice::deinit();
        server = nullptr;
        service = nullptr;
        batteryService = nullptr;
        weightCharacteristic = nullptr;
        gaggiMateWeightCharacteristic = nullptr;
        commandCharacteristic = nullptr;
        capabilitiesCharacteristic = nullptr;
        batteryLevelCharacteristic = nullptr;
        advertising = nullptr;
    }
}

void BluetoothScale::initializeBLE() {
    Serial.println("BluetoothScale: Initializing BLE device...");
    Serial.printf("BluetoothScale: Free heap at start: %u bytes\n", ESP.getFreeHeap());
    
    // Reduce BLE power consumption during initialization to prevent voltage sag
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N0);      // Moderate advertising power (0dBm)
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, ESP_PWR_LVL_N0); // Moderate connection power (0dBm)
    
    // Initialize BLE Device with the advertised WeighMyBru-compatible name.
    NimBLEDevice::init(DEVICE_NAME);
    
    // Set moderate power to reduce current draw during boot while maintaining connectivity
    NimBLEDevice::setPower(ESP_PWR_LVL_N0);  // Moderate BLE power reduction (0dBm)
    
    // Small delay to let power settle
    delay(100);
    
    Serial.printf("BluetoothScale: Free heap after NimBLEDevice::init: %u bytes\n", ESP.getFreeHeap());
    
    Serial.println("BluetoothScale: Creating BLE server...");
    
    // Create BLE Server
    server = NimBLEDevice::createServer();
    if (!server) {
        throw std::runtime_error("Failed to create BLE server");
    }
    server->setCallbacks(this);
    
    Serial.println("BluetoothScale: Creating BLE service...");
    
    // Create BLE Service
    service = server->createService(SERVICE_UUID);
    if (!service) {
        throw std::runtime_error("Failed to create BLE service");
    }
    
    Serial.println("BluetoothScale: Creating characteristics...");
    
    // Create Weight Characteristic for GaggiMate (WeighMyBru protocol format) - Keep original UUID
    gaggiMateWeightCharacteristic = service->createCharacteristic(
        GAGGIMATE_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::NOTIFY |
        NIMBLE_PROPERTY::INDICATE
    );
    
    if (!gaggiMateWeightCharacteristic) {
        Serial.println("BluetoothScale: ERROR - Failed to create GaggiMate weight characteristic");
        throw std::runtime_error("Failed to create GaggiMate weight characteristic");
    }
    
    Serial.println("BluetoothScale: GaggiMate characteristic created successfully");
    
    // Note: NimBLE automatically creates 0x2902 descriptors for characteristics with NOTIFY/INDICATE properties
    
    // Create Weight Characteristic for Bean Conqueror (simple float format) - New UUID
    weightCharacteristic = service->createCharacteristic(
        WEIGHT_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::NOTIFY |
        NIMBLE_PROPERTY::INDICATE
    );
    
    if (!weightCharacteristic) {
        Serial.println("BluetoothScale: ERROR - Failed to create Bean Conqueror weight characteristic");
        throw std::runtime_error("Failed to create Bean Conqueror weight characteristic");
    }
    
    Serial.println("BluetoothScale: Bean Conqueror characteristic created successfully");
    
    // Note: NimBLE automatically creates 0x2902 descriptors for characteristics with NOTIFY/INDICATE properties
    
    // Create Command Characteristic (for receiving commands)
    commandCharacteristic = service->createCharacteristic(
        COMMAND_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::WRITE_NR |
        NIMBLE_PROPERTY::NOTIFY
    );
    
    if (!commandCharacteristic) {
        throw std::runtime_error("Failed to create command characteristic");
    }
    commandCharacteristic->setCallbacks(this);

    // Read-only capabilities characteristic. Existing WMB/BeanConqueror/GaggiMate
    // clients ignore it; extension-aware apps can read it after discovery instead
    // of guessing from the BLE name.
    capabilitiesCharacteristic = service->createCharacteristic(
        CAPABILITIES_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ
    );

    if (!capabilitiesCharacteristic) {
        throw std::runtime_error("Failed to create capabilities characteristic");
    }

    updateCapabilities();
    Serial.println("BluetoothScale: Capabilities characteristic created successfully");

    Serial.println("BluetoothScale: Creating standard Battery Service...");

    batteryService = server->createService(BATTERY_SERVICE_UUID);
    if (!batteryService) {
        throw std::runtime_error("Failed to create Battery Service");
    }

    batteryLevelCharacteristic = batteryService->createCharacteristic(
        BATTERY_LEVEL_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::NOTIFY
    );

    if (!batteryLevelCharacteristic) {
        throw std::runtime_error("Failed to create Battery Level characteristic");
    }

    updateBatteryLevel(false);
    
    Serial.println("BluetoothScale: Setting up advertising...");
    
    // Get advertising object
    advertising = NimBLEDevice::getAdvertising();
    if (!advertising) {
        throw std::runtime_error("Failed to get advertising object");
    }
    
    NimBLEAdvertisementData advertisementData;
    advertisementData.setFlags(BLE_HS_ADV_F_DISC_GEN);
    advertisementData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
    advertising->setAdvertisementData(advertisementData);

    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setName(DEVICE_NAME);
    scanResponseData.setCompleteServices16({NimBLEUUID(BATTERY_SERVICE_UUID)});
    advertising->setScanResponseData(scanResponseData);
    advertising->enableScanResponse(true);
    
    // Set proper connection interval preferences to avoid packet rejection
    // and ensure reliable discovery on all ESP32-S3 variants
    advertising->setPreferredParams(0x06, 0x12);  // 7.5 ms to 22.5 ms
    
    Serial.println("BluetoothScale: BLE initialization completed successfully");
}

void BluetoothScale::startAdvertising() {
    if (advertising) {
        advertising->start();
    }
}

void BluetoothScale::stopAdvertising() {
    if (advertising) {
        advertising->stop();
    }
}

void BluetoothScale::update() {
    // Return early if initialization failed
    if (scale == nullptr) {
        return;
    }
    
    uint32_t now = millis();
    applyPendingWmbOutputConfig();
    
    // Handle connection state changes
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // Give the bluetooth stack time to get ready
        server->startAdvertising();
        Serial.println("BluetoothScale: Start advertising after disconnect");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        Serial.println("BluetoothScale: Client connected");
        oldDeviceConnected = deviceConnected;
        lastHeartbeat = now;
        lastNotifiedSampleSequence = scale ? scale->getSampleSequence() : 0;
        lastNotifiedScaleSampleMillis = scale ? scale->getLastSampleMillis() : 0;
        resetWmbCleanEstimator(scale ? scale->getCurrentWeight() : 0.0f);
        // Send initialization response for WeighMyBru client
        delay(100); // Give time for connection to stabilize
        sendNotificationRequest();
        updateBatteryLevel(true);
    }

    updateFloat32Estimator(now);
    if (getWmbOutputProfile() == WmbOutputProfile::Clean) {
        updateWmbCleanEstimator(now);
    } else {
        updateWmbDiagnosticHighRate(now);
    }
    
    if (deviceConnected) {
        if (now - lastBatterySent >= BATTERY_SEND_INTERVAL) {
            updateBatteryLevel(false);
            lastBatterySent = now;
        }
        
        // Send heartbeat
        if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
            sendHeartbeat();
            lastHeartbeat = now;
        }
    }
}

const char* BluetoothScale::getWmbOutputProfileName() const {
    return getWmbOutputProfile() == WmbOutputProfile::Clean
        ? "clean"
        : "diagnostic-high-rate";
}

uint32_t BluetoothScale::getWmbOutputIntervalMillis() const {
    switch (wmbOutputRateHz) {
        case 10: return 100;
        case 20: return 50;
        case 40: return 25;
        case 80: return 13;
        default: return 50;
    }
}

bool BluetoothScale::isWmbUnderSourceRate() const {
    if (!scale) {
        return false;
    }
    const float detectedHz = scale->getDetectedSampleRateHz();
    return isfinite(detectedHz) && detectedHz > 1.0f && detectedHz < static_cast<float>(wmbOutputRateHz) * 0.92f;
}

uint32_t BluetoothScale::getWmbEffectiveOutputIntervalMillis() const {
    uint32_t intervalMs = getWmbOutputIntervalMillis();
    if (!scale) {
        return intervalMs;
    }
    const float detectedHz = scale->getDetectedSampleRateHz();
    if (isfinite(detectedHz) && detectedHz > 1.0f && detectedHz < static_cast<float>(wmbOutputRateHz) * 0.92f) {
        const uint32_t sourceLimitedInterval = static_cast<uint32_t>(ceilf(1000.0f / detectedHz));
        intervalMs = max(intervalMs, sourceLimitedInterval);
    }
    return intervalMs;
}

bool BluetoothScale::isValidWmbOutputRateHz(uint8_t rateHz) {
    return rateHz >= WMB_MIN_OUTPUT_RATE_HZ &&
        rateHz <= WMB_MAX_OUTPUT_RATE_HZ &&
        (rateHz == 10 || rateHz == 20 || rateHz == 40 || rateHz == 80);
}

bool BluetoothScale::requestWmbDiagnosticHighRateProfile() {
    pendingWmbOutputProfile = static_cast<uint8_t>(WmbOutputProfile::DiagnosticHighRate);
    pendingWmbOutputRateHz = wmbOutputRateHz;
    pendingWmbOutputConfig = true;
    return true;
}

bool BluetoothScale::requestWmbCleanOutputRateHz(uint8_t rateHz) {
    if (!isValidWmbOutputRateHz(rateHz)) {
        return false;
    }
    pendingWmbOutputProfile = static_cast<uint8_t>(WmbOutputProfile::Clean);
    pendingWmbOutputRateHz = rateHz;
    pendingWmbOutputConfig = true;
    return true;
}

void BluetoothScale::applyPendingWmbOutputConfig() {
    if (!pendingWmbOutputConfig) {
        return;
    }

    const WmbOutputProfile profile =
        pendingWmbOutputProfile == static_cast<uint8_t>(WmbOutputProfile::Clean)
            ? WmbOutputProfile::Clean
            : WmbOutputProfile::DiagnosticHighRate;
    const uint8_t rateHz = isValidWmbOutputRateHz(pendingWmbOutputRateHz)
        ? pendingWmbOutputRateHz
        : WMB_DEFAULT_OUTPUT_RATE_HZ;
    pendingWmbOutputConfig = false;
    applyWmbOutputConfig(profile, rateHz);
}

void BluetoothScale::applyWmbOutputConfig(WmbOutputProfile profile, uint8_t rateHz) {
    const bool profileChanged = wmbOutputProfile != static_cast<uint8_t>(profile);
    const bool rateChanged = wmbOutputRateHz != rateHz;
    if (!profileChanged && !rateChanged) {
        return;
    }

    wmbOutputProfile = static_cast<uint8_t>(profile);
    wmbOutputRateHz = rateHz;
    lastWmbScheduleMillis = 0;
    lastWmbEmittedSourceSequence = 0;
    lastNotifiedSampleSequence = scale ? scale->getSampleSequence() : 0;
    lastNotifiedScaleSampleMillis = scale ? scale->getLastSampleMillis() : 0;
    resetWmbCleanEstimator(scale ? scale->getCurrentWeight() : lastWmbWeight);
    Serial.printf("BluetoothScale: WMB output profile=%s rate=%uHz\n",
                  getWmbOutputProfileName(),
                  wmbOutputRateHz);
}

bool BluetoothScale::isConnected() {
    return deviceConnected;
}

bool BluetoothScale::sendWeightNotification(float weight, WmbOutputProfile profile) {
    if (!deviceConnected) {
        return false;
    }
    
    // Send to GaggiMate first (WeighMyBru protocol format) - critical for backward compatibility
    if (sendGaggiMateWeight(weight, profile)) {
        weightNotifyCount++;
        lastWeightNotifyMillis = millis();
        return true;
    } else {
        weightNotifyDropCount++;
    }
    return false;
}

void BluetoothScale::resetWmbCleanEstimator(float seedWeight) {
    lastWmbWeight = isfinite(seedWeight) ? seedWeight : 0.0f;
    hasLastWmbWeight = true;
    lastWmbObservedTareMillis = scale ? scale->getLastTareMillis() : 0;
    lastWmbSelectedSourceCount = 0;
    lastWmbSelectedSourceSequence = 0;
    lastWmbSelectedSourceMillis = 0;
    lastWmbSelectedSourceAgeMs = 0;
    lastWmbSelectedWindowRangeGrams = 0.0f;
    lastWmbEmittedSourceSequence = 0;
    lastWmbSelectedSourceValid = false;
    lastWmbSelectionSuspect = false;
    lastWmbSourceStale = true;
    wmbConsecutiveSuppressedSelectionCount = 0;
    lastWmbFlowRate = 0.0f;
    lastWmbFlowWeight = lastWmbWeight;
    lastWmbFlowSourceMillis = 0;
    lastWmbFlowValid = false;
}

void BluetoothScale::updateWmbDiagnosticHighRate(uint32_t now) {
    (void)now;
    if (!scale || !deviceConnected) {
        return;
    }

    const uint32_t sampleSequence = scale->getSampleSequence();
    if (sampleSequence == lastNotifiedSampleSequence) {
        return;
    }

    const float weight = scale->getCurrentWeight();
    if (sendWeightNotification(weight, WmbOutputProfile::DiagnosticHighRate)) {
        lastNotifiedSampleSequence = sampleSequence;
        lastNotifiedScaleSampleMillis = scale->getLastSampleMillis();
    }
}

void BluetoothScale::updateWmbCleanEstimator(uint32_t now) {
    if (!scale || !deviceConnected) {
        return;
    }

    const uint32_t intervalMs = getWmbEffectiveOutputIntervalMillis();
    if (lastWmbScheduleMillis == 0) {
        lastWmbScheduleMillis = now;
        resetWmbCleanEstimator(scale->getCurrentWeight());
    } else if (now - lastWmbScheduleMillis < intervalMs) {
        return;
    } else if (now - lastWmbScheduleMillis > intervalMs * 2) {
        lastWmbScheduleMillis = now;
    } else {
        lastWmbScheduleMillis += intervalMs;
    }

    lastWmbWeight = getWmbCleanWeight(now);
    hasLastWmbWeight = true;
    wmbEstimatorTickCount++;
    const bool selectedNewerThanLastEmission =
        lastWmbSelectedSourceValid &&
        (lastWmbEmittedSourceSequence == 0 ||
         lastWmbSelectedSourceSequence > lastWmbEmittedSourceSequence);
    if (selectedNewerThanLastEmission &&
        sendWeightNotification(lastWmbWeight, WmbOutputProfile::Clean)) {
        lastWmbEmittedSourceSequence = lastWmbSelectedSourceSequence;
    }
}

void BluetoothScale::updateWmbCleanFlow(float weight, uint32_t selectedSourceMillis, bool valid) {
    if (!valid || selectedSourceMillis == 0) {
        lastWmbFlowValid = false;
        lastWmbFlowRate = 0.0f;
        return;
    }

    if (lastWmbFlowSourceMillis == 0 || selectedSourceMillis <= lastWmbFlowSourceMillis) {
        lastWmbFlowWeight = weight;
        lastWmbFlowSourceMillis = selectedSourceMillis;
        lastWmbFlowValid = false;
        lastWmbFlowRate = 0.0f;
        return;
    }

    const float dtSeconds = (selectedSourceMillis - lastWmbFlowSourceMillis) / 1000.0f;
    const float instantFlow = dtSeconds > 0.0f ? (weight - lastWmbFlowWeight) / dtSeconds : 0.0f;
    lastWmbFlowWeight = weight;
    lastWmbFlowSourceMillis = selectedSourceMillis;

    if (!isfinite(instantFlow) || fabsf(instantFlow) > WMB_FLOW_MAX_VALID_GPS || lastWmbSelectionSuspect || lastWmbSourceStale) {
        lastWmbFlowValid = false;
        lastWmbFlowRate = 0.0f;
        return;
    }

    const float cleanedFlow = fabsf(instantFlow) < WMB_FLOW_DEADBAND_GPS ? 0.0f : instantFlow;
    lastWmbFlowRate = lastWmbFlowValid
        ? (lastWmbFlowRate * 0.65f) + (cleanedFlow * 0.35f)
        : cleanedFlow;
    lastWmbFlowValid = true;
}

void BluetoothScale::resetFloat32CompatibilityEstimator(float seedWeight) {
    lastFloat32Weight = isfinite(seedWeight) ? seedWeight : 0.0f;
    hasLastFloat32Weight = true;
    lastFloat32ObservedTareMillis = scale ? scale->getLastTareMillis() : 0;
    lastFloat32SelectedSourceCount = 0;
    lastFloat32SelectedSourceSequence = 0;
    lastFloat32SelectedSourceAgeMs = 0;
    lastFloat32SelectedWindowRangeGrams = 0.0f;
    lastFloat32SelectedSourceValid = false;
    lastFloat32SelectionSuspect = false;
    lastFloat32ConfirmedLoadStep = false;
    lastFloat32SourceStale = true;
    float32ConsecutiveSuppressedSelectionCount = 0;
}

void BluetoothScale::updateFloat32Estimator(uint32_t now) {
    if (!scale) {
        return;
    }

    if (lastFloat32ScheduleMillis == 0) {
        lastFloat32ScheduleMillis = now;
        resetFloat32CompatibilityEstimator(scale->getCurrentWeight());
        lastFloat32Weight = getFloat32CompatibilityWeight(now);
        hasLastFloat32Weight = true;
        lastFloat32EstimatorMillis = now;
        float32EstimatorTickCount++;
        if (deviceConnected && weightCharacteristic && lastFloat32SelectedSourceValid) {
            if (!sendBeanConquerorWeight(lastFloat32Weight)) {
                float32NotifyDropCount++;
            }
        }
        return;
    }

    if (now - lastFloat32ScheduleMillis < FLOAT32_COMPAT_INTERVAL_MS) {
        return;
    }

    // Keep the compatibility stream paced at 20 Hz without catch-up bursts.
    // If the loop stalls, publish one current sample and re-anchor the schedule.
    if (now - lastFloat32ScheduleMillis > FLOAT32_COMPAT_INTERVAL_MS * 2) {
        lastFloat32ScheduleMillis = now;
    } else {
        lastFloat32ScheduleMillis += FLOAT32_COMPAT_INTERVAL_MS;
    }

    lastFloat32Weight = getFloat32CompatibilityWeight(now);
    hasLastFloat32Weight = true;
    lastFloat32EstimatorMillis = now;
    float32EstimatorTickCount++;
    if (deviceConnected && weightCharacteristic && lastFloat32SelectedSourceValid) {
        if (!sendBeanConquerorWeight(lastFloat32Weight)) {
            float32NotifyDropCount++;
        }
    }
}

float BluetoothScale::selectObservedWindowWeight(uint32_t now,
                                                 uint32_t windowMillis,
                                                 float previousWeight,
                                                 bool hasPreviousWeight,
                                                 uint8_t* selectedSampleCount,
                                                 float* selectedWindowRange,
                                                 uint32_t* selectedSourceSequence,
                                                 uint32_t* selectedSourceMillis,
                                                 bool* selectedConfirmedLoadStep) {
    Scale::InputSample sourceSamples[FLOAT32_MAX_SOURCE_SAMPLES];
    const uint8_t count = scale
        ? scale->copyRecentQualifiedInputSamples(now, windowMillis, sourceSamples, FLOAT32_MAX_SOURCE_SAMPLES)
        : 0;
    float candidates[FLOAT32_MAX_SOURCE_SAMPLES];
    float minWeight = 0.0f;
    float maxWeight = 0.0f;

    for (uint8_t i = 0; i < count; i++) {
        const float weight = sourceSamples[i].weightGrams;
        if (i == 0) {
            minWeight = weight;
            maxWeight = weight;
        } else {
            minWeight = min(minWeight, weight);
            maxWeight = max(maxWeight, weight);
        }
        candidates[i] = weight;
    }

    if (selectedSampleCount != nullptr) {
        *selectedSampleCount = count;
    }
    if (selectedWindowRange != nullptr) {
        *selectedWindowRange = count > 0 ? maxWeight - minWeight : 0.0f;
    }
    if (selectedSourceSequence != nullptr) {
        *selectedSourceSequence = 0;
    }
    if (selectedSourceMillis != nullptr) {
        *selectedSourceMillis = 0;
    }
    if (selectedConfirmedLoadStep != nullptr) {
        *selectedConfirmedLoadStep = false;
    }

    if (count == 0) {
        return hasPreviousWeight ? previousWeight : (scale ? scale->getCurrentWeight() : 0.0f);
    }

    for (uint8_t i = 1; i < count; i++) {
        const float value = candidates[i];
        int8_t j = i - 1;
        while (j >= 0 && candidates[j] > value) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = value;
    }

    const float target = (count % 2 == 1)
        ? candidates[count / 2]
        : (candidates[count / 2 - 1] + candidates[count / 2]) * 0.5f;

    float bestWeight = target;
    float bestError = INFINITY;
    uint32_t bestMillis = 0;
    uint32_t bestSequence = 0;
    const float oldestWeight = sourceSamples[0].weightGrams;
    const uint32_t oldestMillis = sourceSamples[0].sampleMillis;
    float newestWeight = target;
    uint32_t newestMillis = 0;
    uint32_t newestSequence = 0;
    for (uint8_t i = 0; i < count; i++) {
        const Scale::InputSample& sample = sourceSamples[i];

        newestWeight = sample.weightGrams;
        newestMillis = sample.sampleMillis;
        newestSequence = sample.sourceSequence;

        const float error = fabsf(sample.weightGrams - target);
        if (error <= bestError) {
            bestWeight = sample.weightGrams;
            bestError = error;
            bestMillis = sample.sampleMillis;
            bestSequence = sample.sourceSequence;
        }
    }

    const auto detectConfirmedLoadStep = [&](float selectedWeight) -> bool {
        if (!hasPreviousWeight || count < FLOAT32_LOAD_STEP_MIN_CLUSTER_SAMPLES) {
            return false;
        }

        const float step = selectedWeight - previousWeight;
        if (fabsf(step) < FLOAT32_LOAD_STEP_MIN_GRAMS) {
            return false;
        }

        const float direction = step >= 0.0f ? 1.0f : -1.0f;
        uint8_t clusterCount = 0;
        for (uint8_t i = 0; i < count; i++) {
            const Scale::InputSample& sample = sourceSamples[i];
            if (newestMillis >= sample.sampleMillis &&
                newestMillis - sample.sampleMillis > FLOAT32_LOAD_STEP_RECENT_WINDOW_MS) {
                continue;
            }
            if (fabsf(sample.weightGrams - selectedWeight) > FLOAT32_LOAD_STEP_CLUSTER_GRAMS) {
                continue;
            }
            if ((sample.weightGrams - previousWeight) * direction < FLOAT32_LOAD_STEP_MIN_GRAMS * 0.60f) {
                continue;
            }
            clusterCount++;
        }
        return clusterCount >= FLOAT32_LOAD_STEP_MIN_CLUSTER_SAMPLES;
    };

    const float windowRange = selectedWindowRange != nullptr ? *selectedWindowRange : 0.0f;
    const bool flatCoherentWindow =
        windowRange <= FLOAT32_COHERENT_WINDOW_GRAMS &&
        fabsf(newestWeight - target) <= FLOAT32_NEWEST_MEDIAN_BAND_GRAMS;
    bool rampCoherentWindow = false;
    const uint32_t spanMillis = newestMillis >= oldestMillis ? newestMillis - oldestMillis : 0;
    if (!flatCoherentWindow && count >= 5 && spanMillis >= FLOAT32_COHERENT_RAMP_MIN_SPAN_MS) {
        const float slopeGramsPerMs = (newestWeight - oldestWeight) / static_cast<float>(spanMillis);
        const float slopeGramsPerSecond = slopeGramsPerMs * 1000.0f;
        float maxResidual = 0.0f;
        for (uint8_t i = 0; i < count; i++) {
            const Scale::InputSample& sample = sourceSamples[i];
            const uint32_t sampleOffset = sample.sampleMillis >= oldestMillis ? sample.sampleMillis - oldestMillis : 0;
            const float expected = oldestWeight + slopeGramsPerMs * static_cast<float>(sampleOffset);
            maxResidual = max(maxResidual, fabsf(sample.weightGrams - expected));
        }
        rampCoherentWindow =
            fabsf(slopeGramsPerSecond) <= FLOAT32_COHERENT_RAMP_MAX_RATE_GPS &&
            maxResidual <= FLOAT32_COHERENT_RAMP_RESIDUAL_GRAMS;
    }

    if (flatCoherentWindow || rampCoherentWindow) {
        if (selectedSourceSequence != nullptr) {
            *selectedSourceSequence = newestSequence;
        }
        if (selectedSourceMillis != nullptr) {
            *selectedSourceMillis = newestMillis;
        }
        if (selectedConfirmedLoadStep != nullptr) {
            *selectedConfirmedLoadStep = detectConfirmedLoadStep(newestWeight);
        }
        return newestWeight;
    }

    if (selectedSourceSequence != nullptr) {
        *selectedSourceSequence = bestSequence;
    }
    if (selectedSourceMillis != nullptr) {
        *selectedSourceMillis = bestMillis;
    }
    if (selectedConfirmedLoadStep != nullptr) {
        *selectedConfirmedLoadStep = detectConfirmedLoadStep(bestWeight);
    }
    return bestWeight;
}

float BluetoothScale::getWmbCleanWeight(uint32_t now) {
    const uint32_t currentTareMillis = scale ? scale->getLastTareMillis() : 0;
    if (currentTareMillis != lastWmbObservedTareMillis) {
        resetWmbCleanEstimator(scale ? scale->getCurrentWeight() : 0.0f);
    }

    uint8_t selectedSampleCount = 0;
    float selectedWindowRange = 0.0f;
    uint32_t selectedSourceSequence = 0;
    uint32_t selectedSourceMillis = 0;
    bool selectedConfirmedLoadStep = false;
    const float selectedWeight = selectObservedWindowWeight(
        now,
        WMB_SELECTION_WINDOW_MS,
        lastWmbWeight,
        hasLastWmbWeight,
        &selectedSampleCount,
        &selectedWindowRange,
        &selectedSourceSequence,
        &selectedSourceMillis,
        &selectedConfirmedLoadStep);

    lastWmbSelectedSourceCount = selectedSampleCount;
    lastWmbSelectedWindowRangeGrams = selectedWindowRange;
    lastWmbSelectedSourceSequence = selectedSourceSequence;
    lastWmbSelectedSourceMillis = selectedSourceMillis;
    lastWmbSelectedSourceValid = selectedSourceSequence != 0;
    lastWmbSelectedSourceAgeMs =
        lastWmbSelectedSourceValid ? now - selectedSourceMillis : 0;
    lastWmbSourceStale =
        !lastWmbSelectedSourceValid || lastWmbSelectedSourceAgeMs > WMB_SOURCE_STALE_MS;
    if (lastWmbSourceStale) {
        wmbStaleSourceCount++;
        updateWmbCleanFlow(selectedWeight, selectedSourceMillis, false);
        return lastWmbWeight;
    }

    lastWmbSelectionSuspect =
        !selectedConfirmedLoadStep &&
        selectedSampleCount > 0 &&
        selectedWindowRange > FLOAT32_SUSPECT_SELECTION_GRAMS &&
        fabsf(selectedWeight - lastWmbWeight) > FLOAT32_SUSPECT_SELECTION_GRAMS;
    const bool suppressSuspectSelection =
        !selectedConfirmedLoadStep &&
        selectedSampleCount > 0 &&
        selectedWindowRange > FLOAT32_SUPPRESS_SELECTION_GRAMS &&
        fabsf(selectedWeight - lastWmbWeight) > FLOAT32_SUPPRESS_SELECTION_GRAMS;
    if (suppressSuspectSelection &&
        wmbConsecutiveSuppressedSelectionCount < FLOAT32_MAX_CONSECUTIVE_SUPPRESSED_SELECTIONS) {
        wmbConsecutiveSuppressedSelectionCount++;
        lastWmbSelectedSourceValid = false;
        updateWmbCleanFlow(lastWmbWeight, selectedSourceMillis, false);
        return lastWmbWeight;
    }

    wmbConsecutiveSuppressedSelectionCount = 0;
    updateWmbCleanFlow(selectedWeight, selectedSourceMillis, true);
    return selectedWeight;
}

float BluetoothScale::getFloat32CompatibilityWeight(uint32_t now) {
    const uint32_t currentTareMillis = scale ? scale->getLastTareMillis() : 0;
    if (currentTareMillis != lastFloat32ObservedTareMillis) {
        resetFloat32CompatibilityEstimator(scale ? scale->getCurrentWeight() : 0.0f);
    }

    uint8_t selectedSampleCount = 0;
    float selectedWindowRange = 0.0f;
    uint32_t selectedSourceSequence = 0;
    uint32_t selectedSourceMillis = 0;
    bool selectedConfirmedLoadStep = false;
    const float selectedWeight = selectObservedWindowWeight(
        now,
        FLOAT32_SELECTION_WINDOW_MS,
        lastFloat32Weight,
        hasLastFloat32Weight,
        &selectedSampleCount,
        &selectedWindowRange,
        &selectedSourceSequence,
        &selectedSourceMillis,
        &selectedConfirmedLoadStep);

    if (!hasLastFloat32Weight) {
        return selectedWeight;
    }

    lastFloat32SelectedSourceCount = selectedSampleCount;
    lastFloat32SelectedWindowRangeGrams = selectedWindowRange;
    lastFloat32SelectedSourceSequence = selectedSourceSequence;
    lastFloat32SelectedSourceValid = selectedSourceSequence != 0;
    lastFloat32SelectedSourceAgeMs =
        lastFloat32SelectedSourceValid ? now - selectedSourceMillis : 0;
    lastFloat32SourceStale =
        !lastFloat32SelectedSourceValid || lastFloat32SelectedSourceAgeMs > FLOAT32_SOURCE_STALE_MS;
    if (lastFloat32SourceStale) {
        float32StaleSourceCount++;
    }
    lastFloat32ConfirmedLoadStep = selectedConfirmedLoadStep;
    if (lastFloat32ConfirmedLoadStep) {
        float32ConfirmedLoadStepCount++;
    }
    lastFloat32SelectionSuspect =
        !lastFloat32ConfirmedLoadStep &&
        selectedSampleCount > 0 &&
        selectedWindowRange > FLOAT32_SUSPECT_SELECTION_GRAMS &&
        fabsf(selectedWeight - lastFloat32Weight) > FLOAT32_SUSPECT_SELECTION_GRAMS;
    if (lastFloat32SelectionSuspect) {
        float32SuspectSelectionCount++;
    }
    const bool suppressSuspectSelection =
        !lastFloat32ConfirmedLoadStep &&
        selectedSampleCount > 0 &&
        selectedWindowRange > FLOAT32_SUPPRESS_SELECTION_GRAMS &&
        fabsf(selectedWeight - lastFloat32Weight) > FLOAT32_SUPPRESS_SELECTION_GRAMS;
    if (suppressSuspectSelection &&
        float32ConsecutiveSuppressedSelectionCount < FLOAT32_MAX_CONSECUTIVE_SUPPRESSED_SELECTIONS) {
        // The Float32 lane is a presentation/control-compatible output. When
        // the selected observed sample would create a large jump from an
        // incoherent source window, skip this 20 Hz tick rather than publishing
        // a known-suspect value or inventing an interpolated replacement. The
        // consecutive cap keeps this from becoming an unbounded output freeze.
        float32ConsecutiveSuppressedSelectionCount++;
        lastFloat32SelectedSourceValid = false;
        return lastFloat32Weight;
    }

    float32ConsecutiveSuppressedSelectionCount = 0;
    return selectedWeight;
}

bool BluetoothScale::sendBeanConquerorWeight(float weight) {
    if (!weightCharacteristic) {
        return false;
    }
    
    try {
        // Bean Conqueror expects a simple 4-byte float in little-endian format
        union {
            float floatValue;
            uint8_t bytes[4];
        } weightData;
        
        weightData.floatValue = weight;
        
        // ESP32 is little-endian, so bytes are already in correct order for Bean Conqueror
        weightCharacteristic->setValue(weightData.bytes, 4);
        if (!notifyCurrentValueQueued(weightCharacteristic)) {
            return false;
        }
        float32NotifyCount++;
        lastFloat32NotifyMillis = millis();
        return true;
        
        //Serial.printf("BluetoothScale: Sent Bean Conqueror weight %.2fg as 4-byte float\n", weight);
    } catch (const std::exception& e) {
        Serial.printf("BluetoothScale: ERROR sending Bean Conqueror weight: %s\n", e.what());
    }
    return false;
}

bool BluetoothScale::sendGaggiMateWeight(float weight, WmbOutputProfile profile) {
    if (!gaggiMateWeightCharacteristic) {
        Serial.println("BluetoothScale: WARNING - GaggiMate characteristic is null!");
        return false;
    }
    
    try {
        // Convert weight to integer (grams * 100 for 0.01g precision)
        int32_t weightInt = (int32_t)(weight * 100);
        
        // Create weight message following WeighMyBru protocol
        uint8_t payload[PROTOCOL_LENGTH] = {0};
        
        // Header. Bytes 0, 1, 6, 7, 8, 9, and 19 preserve the legacy
        // WeighMyBru/GaggiMate contract. Extension-aware apps can parse the
        // formerly-reserved bytes advertised by capabilities char 6E400005.
        payload[0] = PRODUCT_NUMBER; // Product number
        payload[1] = static_cast<uint8_t>(WeighMyBruMessageType::WEIGHT); // Message type

        const bool cleanPacket = profile == WmbOutputProfile::Clean;
        const uint32_t packetSourceMillis =
            cleanPacket && lastWmbSelectedSourceValid
                ? lastWmbSelectedSourceMillis
                : (scale ? scale->getLastSampleMillis() : millis());
        const uint32_t sampleTimestampMs = packetSourceMillis & 0x00FFFFFFUL;
        payload[2] = (sampleTimestampMs >> 16) & 0xFF;
        payload[3] = (sampleTimestampMs >> 8) & 0xFF;
        payload[4] = sampleTimestampMs & 0xFF;
        payload[5] = WMB_PLUS_EXTENSION_PACKET_VERSION;
        
        // Sign (positive = 43, negative = 45)
        payload[6] = (weightInt >= 0) ? 43 : 45;
        
        // Weight data (3 bytes, big endian)
        uint32_t absWeight = abs(weightInt);
        payload[7] = (absWeight >> 16) & 0xFF;
        payload[8] = (absWeight >> 8) & 0xFF;
        payload[9] = absWeight & 0xFF;
        
        bool packetFlowValid = false;
        float currentFlowRate = 0.0f;
        if (cleanPacket) {
            packetFlowValid = lastWmbFlowValid;
            currentFlowRate = packetFlowValid ? lastWmbFlowRate : 0.0f;
        } else if (flowRate) {
            const float diagnosticFlow = flowRate->getFlowRate();
            packetFlowValid = isfinite(diagnosticFlow);
            currentFlowRate = packetFlowValid ? diagnosticFlow : 0.0f;
        }
        const int32_t flowCentiPerSecond = static_cast<int32_t>(roundf(currentFlowRate * 100.0f));
        const uint16_t absFlow = clampUnsigned16(abs(flowCentiPerSecond));
        payload[10] = (flowCentiPerSecond >= 0) ? 43 : 45;
        payload[11] = (absFlow >> 8) & 0xFF;
        payload[12] = absFlow & 0xFF;

        uint8_t batteryPercent = 0xFF;
        if (batteryMonitor && batteryMonitor->hasValidReading()) {
            const int percentage = batteryMonitor->getBatteryPercentage();
            if (percentage >= 0 && percentage <= 100) {
                batteryPercent = static_cast<uint8_t>(percentage);
            }
        }
        payload[13] = batteryPercent;

        const uint8_t nextPacketSequence = packetSequence + 1;
        payload[14] = nextPacketSequence;

        uint8_t statusFlags = 0;
        if (display && display->isTimerRunning()) {
            statusFlags |= STATUS_TIMER_RUNNING;
        }
        if (scale && scale->isHX711Connected()) {
            statusFlags |= STATUS_HX711_CONNECTED;
        }
        if (touchSensor && touchSensor->isTarePending()) {
            statusFlags |= STATUS_TARE_PENDING;
        }
        if (touchSensor && touchSensor->isTareAndStartPending()) {
            statusFlags |= STATUS_ATOMIC_TARE_START_PENDING;
        }
        if (batteryMonitor && batteryMonitor->hasValidReading()) {
            statusFlags |= STATUS_BATTERY_PRESENT;
            if (batteryMonitor->isLowBattery()) {
                statusFlags |= STATUS_BATTERY_LOW;
            }
            if (batteryMonitor->isCriticalBattery()) {
                statusFlags |= STATUS_BATTERY_CRITICAL;
            }
        }
        if (display && display->isConnected()) {
            statusFlags |= STATUS_DISPLAY_PRESENT;
        }
        payload[15] = statusFlags;

        payload[16] = scale ? scale->getScaleQualityScore() : 0xFF;
        payload[17] = scale ? scale->getDetectedSampleRateRoundedHz() : 0;

        uint8_t diagnosticFlags = DIAG_EXTENSION_PRESENT;
        if (scale) {
            if (scale->hasRecentBump()) {
                diagnosticFlags |= DIAG_RECENT_BUMP;
            }
            if (scale->getSampleIntervalLongGapCount() > 0) {
                diagnosticFlags |= DIAG_LONG_GAP_SEEN;
            }
            if (scale->getSampleIntervalStatsCount() > 0) {
                diagnosticFlags |= DIAG_CADENCE_VALID;
            }
            const String rateMode = scale->getDetectedHx711RateMode();
            if (rateMode == "80SPS") {
                diagnosticFlags |= DIAG_80SPS_DETECTED;
            } else if (rateMode == "10SPS") {
                diagnosticFlags |= DIAG_10SPS_DETECTED;
            }
            diagnosticFlags |= DIAG_QUALITY_VALID;
        }
        if (packetFlowValid) {
            diagnosticFlags |= DIAG_FLOW_PRESENT;
        }
        payload[18] = diagnosticFlags;
        
        // Calculate and set checksum (last byte)
        payload[PROTOCOL_LENGTH - 1] = calculateChecksum(payload, PROTOCOL_LENGTH - 1);
        
        // Count packets after the notify call is accepted by the local NimBLE
        // API. NimBLE-Arduino versions differ on whether notify(payload, len)
        // reports a bool; the adapter preserves drop accounting where possible
        // and treats void-return builds as queued once the call completes.
        gaggiMateWeightCharacteristic->setValue(payload, PROTOCOL_LENGTH);
        if (!notifyPayloadQueued(gaggiMateWeightCharacteristic, payload, PROTOCOL_LENGTH)) {
            return false;
        }
        packetSequence = nextPacketSequence;
        
        //Serial.printf("BluetoothScale: Sent GaggiMate weight %.2fg as WeighMyBru protocol\n", weight);
        return true;
    } catch (const std::exception& e) {
        Serial.printf("BluetoothScale: ERROR sending GaggiMate weight: %s\n", e.what());
        return false;
    }
}

void BluetoothScale::sendHeartbeat() {
    if (!deviceConnected || !commandCharacteristic) return;
    
    // Send system heartbeat message
    uint8_t payload[] = {0x02, 0x00};
    sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
    
    Serial.println("BluetoothScale: Heartbeat sent");
}

void BluetoothScale::sendNotificationRequest() {
    if (!deviceConnected) return;
    
    // Send notification request for WeighMyBru initialization
    uint8_t payload[] = {0x06, 0x00, 0x00, 0x00, 0x00, 0x00};
    sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
    
    Serial.println("BluetoothScale: Notification request sent");
}

void BluetoothScale::sendMessage(WeighMyBruMessageType msgType, const uint8_t* payload, size_t length) {
    if (!deviceConnected || !commandCharacteristic) return;
    
    // Create message buffer
    uint8_t message[length + 1];
    
    // Copy payload
    memcpy(message, payload, length);
    
    // Calculate and append checksum
    message[length] = calculateChecksum(message, length);
    
    // Send via command characteristic
    commandCharacteristic->setValue(message, length + 1);
    commandCharacteristic->notify();
}

uint8_t BluetoothScale::calculateChecksum(const uint8_t* data, size_t length) {
    uint8_t checksum = data[0];
    for (size_t i = 1; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

void BluetoothScale::handleTareCommand() {
    if (touchSensor) {
        Serial.println("BluetoothScale: Scheduling BLE tare through physical tare path");
        touchSensor->requestTare("BLE");
    } else {
        Serial.println("BluetoothScale: Cannot tare safely; touch/loop tare path is not available");
        return;
    }

    // Send legacy tare accepted confirmation. The actual tare completes after
    // the same delayed path used by the physical button.
    uint8_t payload[] = {0x03, 0x0a, 0x01, 0x00, 0x00};
    sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
}

void BluetoothScale::handleTareAndStartTimerCommand() {
    if (touchSensor) {
        Serial.println("BluetoothScale: Scheduling BLE atomic tare+start through physical tare path");
        touchSensor->requestTareAndStartTimer("BLE");
    } else {
        Serial.println("BluetoothScale: Cannot atomic tare+start safely; touch/loop tare path is not available");
        return;
    }

    // Immediate accepted confirmation. The timer starts at the tare-completion
    // boundary when routed through TouchSensor's delayed physical-parity path.
    uint8_t payload[] = {0x03, 0x0a, 0x07, 0x00, 0x00};
    sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
}

void BluetoothScale::handleTimerCommand(BeanConquerorCommand command) {
    if (!display) {
        Serial.println("BluetoothScale: Display not available for timer command");
        return;
    }
    
    switch (command) {
        case BeanConquerorCommand::TIMER_START:
            Serial.println("BluetoothScale: Starting timer");
            display->startTimer();
            // Send timer start confirmation
            {
                uint8_t payload[] = {0x03, 0x0a, 0x02, 0x01, 0x00};
                sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
            }
            break;
            
        case BeanConquerorCommand::TIMER_STOP:
            Serial.println("BluetoothScale: Stopping timer");
            display->stopTimer();
            // Send timer stop confirmation
            {
                uint8_t payload[] = {0x03, 0x0a, 0x03, 0x01, 0x00};
                sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
            }
            break;
            
        case BeanConquerorCommand::TIMER_RESET:
            Serial.println("BluetoothScale: Resetting timer");
            display->resetTimer();
            // Send timer reset confirmation
            {
                uint8_t payload[] = {0x03, 0x0a, 0x04, 0x01, 0x00};
                sendMessage(WeighMyBruMessageType::SYSTEM, payload, sizeof(payload));
            }
            break;
            
        default:
            Serial.printf("BluetoothScale: Unknown timer command: 0x%02X\n", static_cast<uint8_t>(command));
            break;
    }
}

void BluetoothScale::processIncomingMessage(uint8_t* data, size_t length) {
    if (length < 2) return;
    
    uint8_t productNumber = data[0];
    WeighMyBruMessageType messageType = static_cast<WeighMyBruMessageType>(data[1]);
    
    Serial.printf("BluetoothScale: Received message - Product: 0x%02X, Type: 0x%02X\n", 
                  productNumber, static_cast<uint8_t>(messageType));
    
    // Accept messages from GaggiMate (Product 0x02) and WeighMyBru (Product 0x03)
    if (productNumber != 0x02 && productNumber != PRODUCT_NUMBER) {
        Serial.printf("BluetoothScale: Ignoring message from unknown product: 0x%02X\n", productNumber);
        return;
    }
    
    if (messageType == WeighMyBruMessageType::SYSTEM && length >= 4) {
        BeanConquerorCommand command = static_cast<BeanConquerorCommand>(data[2]);
        
        switch (command) {
            case BeanConquerorCommand::TARE:
                if (data[3] == 0x01) { // Command trigger
                    handleTareCommand();
                }
                break;
                
            case BeanConquerorCommand::TIMER_START:
                if (data[3] == 0x01) { // Command trigger
                    handleTimerCommand(BeanConquerorCommand::TIMER_START);
                }
                break;
                
            case BeanConquerorCommand::TIMER_STOP:
                if (data[3] == 0x01) { // Command trigger
                    handleTimerCommand(BeanConquerorCommand::TIMER_STOP);
                }
                break;
                
            case BeanConquerorCommand::TIMER_RESET:
                if (data[3] == 0x01) { // Command trigger
                    handleTimerCommand(BeanConquerorCommand::TIMER_RESET);
                }
                break;

            case BeanConquerorCommand::TARE_AND_START_TIMER:
                if (data[3] == 0x00 || data[3] == 0x01) { // Bookoo-style or WMB trigger
                    handleTareAndStartTimerCommand();
                }
                break;
                
            default:
                Serial.printf("BluetoothScale: Unknown command: 0x%02X\n", static_cast<uint8_t>(command));
                break;
        }
    }
}

// BLE Server Callbacks
void BluetoothScale::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    (void)pServer;
    deviceConnected = true;
    connectionHandle = connInfo.getConnHandle();
    NimBLEDevice::stopAdvertising();
    Serial.println("BluetoothScale: Device connected");
}

void BluetoothScale::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    (void)pServer;
    (void)connInfo;
    (void)reason;
    deviceConnected = false;
    connectionHandle = 0;
    Serial.println("BluetoothScale: Device disconnected");
}

// BLE Characteristic Callbacks
void BluetoothScale::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    (void)connInfo;
    auto value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
        uint8_t* data = (uint8_t*)value.data();
        size_t length = value.length();
        
        Serial.printf("BluetoothScale: Received %d bytes\n", length);
        processIncomingMessage(data, length);
    }
}

// Overloaded begin method for early initialization
void BluetoothScale::begin() {
    begin(nullptr);  // Initialize without scale reference
}

void BluetoothScale::setScale(Scale* scaleInstance) {
    scale = scaleInstance;
    lastNotifiedSampleSequence = scale ? scale->getSampleSequence() : 0;
    lastNotifiedScaleSampleMillis = scale ? scale->getLastSampleMillis() : 0;
    Serial.println("BluetoothScale: Scale reference set");
}

void BluetoothScale::setDisplay(Display* displayInstance) {
    display = displayInstance;
    Serial.println("BluetoothScale: Display reference set");
}

void BluetoothScale::setTouchSensor(TouchSensor* touchSensorInstance) {
    touchSensor = touchSensorInstance;
    Serial.println("BluetoothScale: Touch sensor reference set");
}

void BluetoothScale::setBatteryMonitor(BatteryMonitor* batteryMonitorInstance) {
    batteryMonitor = batteryMonitorInstance;
    updateBatteryLevel(true);
    Serial.println("BluetoothScale: Battery monitor reference set");
}

void BluetoothScale::setFlowRate(FlowRate* flowRateInstance) {
    flowRate = flowRateInstance;
    Serial.println("BluetoothScale: Flow rate reference set");
}

void BluetoothScale::updateCapabilities() {
    if (!capabilitiesCharacteristic) {
        return;
    }

    uint8_t payload[CAPABILITIES_PAYLOAD_LENGTH] = {0};
    payload[0] = PRODUCT_NUMBER;
    payload[1] = 0x0C; // Capabilities/feature-discovery message type
    payload[2] = 0x01; // Capabilities payload version
    payload[3] = CAPABILITIES_PAYLOAD_LENGTH;
    payload[4] = 0x01; // Protocol major
    payload[5] = 0x00; // Protocol minor
    payload[6] = WMB_PLUS_FEATURE_MASK & 0xFF;
    payload[7] = (WMB_PLUS_FEATURE_MASK >> 8) & 0xFF;
    payload[8] = (WMB_PLUS_FEATURE_MASK >> 16) & 0xFF;
    payload[9] = (WMB_PLUS_FEATURE_MASK >> 24) & 0xFF;
    payload[10] = static_cast<uint8_t>(BeanConquerorCommand::TARE_AND_START_TIMER); // Preferred atomic command
    payload[11] = 0x00; // Preferred Bookoo-style command data1 for atomic tare+start
    payload[12] = WMB_PLUS_EXTENSION_PACKET_VERSION;
    payload[13] = WMB_PLUS_EXTENSION_PACKET_LENGTH;
    payload[14] = 0x00; // Reserved
    payload[15] = calculateChecksum(payload, CAPABILITIES_PAYLOAD_LENGTH - 1);

    capabilitiesCharacteristic->setValue(payload, CAPABILITIES_PAYLOAD_LENGTH);
}

void BluetoothScale::updateBatteryLevel(bool forceNotify) {
    if (!batteryLevelCharacteristic || !batteryMonitor || !batteryMonitor->hasValidReading()) {
        return;
    }

    const int percentage = batteryMonitor->getBatteryPercentage();
    if (percentage < 0 || percentage > 100) {
        Serial.printf("BluetoothScale: Ignoring invalid battery percentage %d\n", percentage);
        return;
    }

    const uint8_t batteryPercent = static_cast<uint8_t>(percentage);
    const bool changed = batteryPercent != lastBatteryPercent;

    if (!forceNotify && !changed) {
        return;
    }

    batteryLevelCharacteristic->setValue(&batteryPercent, 1);
    lastBatteryPercent = batteryPercent;

    if (deviceConnected && (forceNotify || changed)) {
        batteryLevelCharacteristic->notify();
        batteryNotifyCount++;
        lastBatteryNotifyMillis = millis();
    }
}

void BluetoothScale::printDiagnostics() {
    const uint32_t now = millis();
    const float uptimeSeconds = now / 1000.0f;
    const float extendedWeightRate = uptimeSeconds > 0.0f ? weightNotifyCount / uptimeSeconds : 0.0f;
    const float float32Rate = uptimeSeconds > 0.0f ? float32NotifyCount / uptimeSeconds : 0.0f;
    const float batteryRate = uptimeSeconds > 0.0f ? batteryNotifyCount / uptimeSeconds : 0.0f;

    Serial.println("BLE diagnostics:");
    Serial.printf("  name=%s\n", DEVICE_NAME);
    Serial.printf("  service=%s\n", SERVICE_UUID);
    Serial.printf("  gaggiMateWeight=%s\n", GAGGIMATE_CHARACTERISTIC_UUID);
    Serial.printf("  beanConquerorWeight=%s\n", WEIGHT_CHARACTERISTIC_UUID);
    Serial.printf("  command=%s\n", COMMAND_CHARACTERISTIC_UUID);
    Serial.printf("  capabilities=%s payloadVersion=1 featureMask=0x%08lX atomicCommand=0x%02X extensionPacket=v%u/%uB\n",
                  CAPABILITIES_CHARACTERISTIC_UUID,
                  static_cast<unsigned long>(WMB_PLUS_FEATURE_MASK),
                  static_cast<uint8_t>(BeanConquerorCommand::TARE_AND_START_TIMER),
                  WMB_PLUS_EXTENSION_PACKET_VERSION,
                  WMB_PLUS_EXTENSION_PACKET_LENGTH);
    Serial.printf("  batteryService=%s batteryLevel=%s\n", BATTERY_SERVICE_UUID, BATTERY_LEVEL_CHARACTERISTIC_UUID);
    Serial.printf("  connected=%s\n", deviceConnected ? "true" : "false");
    Serial.printf("  wmbPlusExtendedProfile=%s requestedRate=%uHz interval=%lums effectiveInterval=%lums underSource=%s cleanWindow=%lums output=observed-sample-only-in-clean-profile\n",
                  getWmbOutputProfileName(),
                  wmbOutputRateHz,
                  static_cast<unsigned long>(getWmbOutputIntervalMillis()),
                  static_cast<unsigned long>(getWmbEffectiveOutputIntervalMillis()),
                  isWmbUnderSourceRate() ? "true" : "false",
                  static_cast<unsigned long>(WMB_SELECTION_WINDOW_MS));
    Serial.printf("  legacyFloat32Cadence=20Hz interval=%lums source=zero-qualified-input selectionWindow=%lums suspectSelection=%.2fg suppressSelection=%.2fg/%u coherentRange=%.2fg newestMedianBand=%.2fg rampResidual=%.2fg rampMaxRate=%.1fg/s loadStep>=%.2fg cluster=%.2fg/%u output=observed-sample-only\n",
                  static_cast<unsigned long>(FLOAT32_COMPAT_INTERVAL_MS),
                  static_cast<unsigned long>(FLOAT32_SELECTION_WINDOW_MS),
                  FLOAT32_SUSPECT_SELECTION_GRAMS,
                  FLOAT32_SUPPRESS_SELECTION_GRAMS,
                  FLOAT32_MAX_CONSECUTIVE_SUPPRESSED_SELECTIONS,
                  FLOAT32_COHERENT_WINDOW_GRAMS,
                  FLOAT32_NEWEST_MEDIAN_BAND_GRAMS,
                  FLOAT32_COHERENT_RAMP_RESIDUAL_GRAMS,
                  FLOAT32_COHERENT_RAMP_MAX_RATE_GPS,
                  FLOAT32_LOAD_STEP_MIN_GRAMS,
                  FLOAT32_LOAD_STEP_CLUSTER_GRAMS,
                  FLOAT32_LOAD_STEP_MIN_CLUSTER_SAMPLES);
    Serial.printf("  scaleSampleSequence=%lu diagnosticNotifiedSequence=%lu scaleSampleMs=%lu\n",
                  static_cast<unsigned long>(scale ? scale->getSampleSequence() : 0),
                  static_cast<unsigned long>(lastNotifiedSampleSequence),
                  static_cast<unsigned long>(lastNotifiedScaleSampleMillis));
    Serial.printf("  extendedWeightNotifyCount=%lu dropCount=%lu rate=%.2f/s lastMs=%lu\n",
                  static_cast<unsigned long>(weightNotifyCount),
                  static_cast<unsigned long>(weightNotifyDropCount), extendedWeightRate,
                  static_cast<unsigned long>(lastWeightNotifyMillis));
    Serial.printf("  wmbCleanSource valid=%s count=%u ageMs=%lu seq=%lu emittedSeq=%lu windowRange=%.3fg stale=%s lastSuspect=%s flowValid=%s flow=%.2fg/s staleSources=%lu\n",
                  lastWmbSelectedSourceValid ? "true" : "false",
                  lastWmbSelectedSourceCount,
                  static_cast<unsigned long>(lastWmbSelectedSourceAgeMs),
                  static_cast<unsigned long>(lastWmbSelectedSourceSequence),
                  static_cast<unsigned long>(lastWmbEmittedSourceSequence),
                  lastWmbSelectedWindowRangeGrams,
                  lastWmbSourceStale ? "true" : "false",
                  lastWmbSelectionSuspect ? "true" : "false",
                  lastWmbFlowValid ? "true" : "false",
                  lastWmbFlowRate,
                  static_cast<unsigned long>(wmbStaleSourceCount));
    Serial.printf("  float32NotifyCount=%lu dropCount=%lu rate=%.2f/s lastScheduleMs=%lu lastEmissionMs=%lu lastWeight=%.2f suspectSelections=%lu confirmedLoadSteps=%lu staleSources=%lu\n",
                  static_cast<unsigned long>(float32NotifyCount),
                  static_cast<unsigned long>(float32NotifyDropCount),
                  float32Rate,
                  static_cast<unsigned long>(lastFloat32ScheduleMillis),
                  static_cast<unsigned long>(lastFloat32NotifyMillis),
                  lastFloat32Weight,
                  static_cast<unsigned long>(float32SuspectSelectionCount),
                  static_cast<unsigned long>(float32ConfirmedLoadStepCount),
                  static_cast<unsigned long>(float32StaleSourceCount));
    Serial.printf("  float32Source valid=%s count=%u ageMs=%lu seq=%lu windowRange=%.3fg limited=%s ageOverTick=%s stale=%s lastSuspect=%s lastConfirmedLoadStep=%s\n",
                  lastFloat32SelectedSourceValid ? "true" : "false",
                  lastFloat32SelectedSourceCount,
                  static_cast<unsigned long>(lastFloat32SelectedSourceAgeMs),
                  static_cast<unsigned long>(lastFloat32SelectedSourceSequence),
                  lastFloat32SelectedWindowRangeGrams,
                  isLastFloat32SourceLimited() ? "true" : "false",
                  isLastFloat32SourceAgeOverTick() ? "true" : "false",
                  isLastFloat32SourceStale() ? "true" : "false",
                  lastFloat32SelectionSuspect ? "true" : "false",
                  lastFloat32ConfirmedLoadStep ? "true" : "false");
    Serial.printf("  batteryNotifyCount=%lu rate=%.2f/s lastMs=%lu lastPercent=%s\n",
                  static_cast<unsigned long>(batteryNotifyCount), batteryRate,
                  static_cast<unsigned long>(lastBatteryNotifyMillis),
                  lastBatteryPercent == 255 ? "unset" : String(lastBatteryPercent).c_str());
    Serial.printf("  bleTarePath=%s\n", touchSensor ? "physical-parity-delayed" : "direct-default-fallback");
    Serial.println("  commandMap=0x01:tare 0x02:start 0x03:stop 0x04:reset 0x07:tare-and-start");
}

// Get BLE signal strength (RSSI)
int BluetoothScale::getBluetoothSignalStrength() {
    if (!deviceConnected || !server) {
        return -100; // Return very weak signal if not connected
    }
    
    // Try to get RSSI from the BLE connection
    // Note: ESP32 BLE library doesn't directly expose RSSI for server connections
    // This is a limitation of the current BLE implementation
    return connectionRSSI; // Will be updated when available
}

// Get detailed BLE connection information
String BluetoothScale::getBluetoothConnectionInfo() {
    String info = "{";
    
    info += "\"connected\":" + String(deviceConnected ? "true" : "false") + ",";
    info += "\"advertising\":" + String((advertising != nullptr) ? "true" : "false") + ",";
    
    if (deviceConnected) {
        info += "\"signal_strength\":" + String(connectionRSSI) + ",";
        
        if (connectionRSSI >= -30) {
            info += "\"signal_quality\":\"Excellent\",";
        } else if (connectionRSSI >= -50) {
            info += "\"signal_quality\":\"Very Good\",";
        } else if (connectionRSSI >= -60) {
            info += "\"signal_quality\":\"Good\",";
        } else if (connectionRSSI >= -70) {
            info += "\"signal_quality\":\"Fair\",";
        } else if (connectionRSSI >= -80) {
            info += "\"signal_quality\":\"Weak\",";
        } else {
            info += "\"signal_quality\":\"Very Weak\",";
        }
        
        info += "\"connection_handle\":" + String(connectionHandle) + ",";
        info += "\"service_uuid\":\"" + String(SERVICE_UUID) + "\",";
        info += "\"device_name\":\"" + String(DEVICE_NAME) + "\"";
    } else {
        info += "\"signal_strength\":null,";
        info += "\"signal_quality\":\"Disconnected\",";
        info += "\"connection_handle\":null,";
        info += "\"service_uuid\":\"" + String(SERVICE_UUID) + "\",";
        info += "\"device_name\":\"" + String(DEVICE_NAME) + "\"";
    }
    
    info += "}";
    return info;
}
