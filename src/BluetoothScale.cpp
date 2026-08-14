#include "BluetoothScale.h"
#include "BatteryMonitor.h"
#include "Display.h"
#include "FlowRate.h"
#include "TouchSensor.h"
#include "BoardConfig.h"
#include "Version.h"
#include <Arduino.h>
#include <stdexcept>
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
#if HAS_I2C_FUEL_GAUGE
    | FEATURE_FUEL_GAUGE_BATTERY
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
      weightNotifyCount(0), float32NotifyCount(0), packetSequence(0), batteryNotifyCount(0),
      lastWeightNotifyMillis(0), lastFloat32NotifyMillis(0), lastBatteryNotifyMillis(0),
      lastFloat32Weight(0.0f), hasLastFloat32Weight(false),
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
    
    Serial.println("BluetoothScale: Starting service...");
    
    // Start the service
    service->start();
    batteryService->start();
    
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
    advertising->setScanResponse(true);
    
    // Set proper connection interval preferences to avoid packet rejection
    // and ensure reliable discovery on all ESP32-S3 variants
    advertising->setMinPreferred(0x06);  // 7.5 ms minimum interval
    advertising->setMaxPreferred(0x12);  // 22.5 ms maximum interval
    
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
        lastFloat32NotifyMillis = now;
        lastFloat32Weight = scale ? scale->getCurrentWeight() : 0.0f;
        hasLastFloat32Weight = true;
        
        // Send initialization response for WeighMyBru client
        delay(100); // Give time for connection to stabilize
        sendNotificationRequest();
        updateBatteryLevel(true);
    }
    
    if (deviceConnected) {
        // Send weight updates only when the scale has produced a fresh public
        // weight value. This avoids timer-driven duplicate packets and lets BLE
        // cadence follow the actual scale acquisition cadence.
        const uint32_t sampleSequence = scale->getSampleSequence();
        if (sampleSequence != lastNotifiedSampleSequence) {
            float currentWeight = scale->getCurrentWeight();
            sendWeightNotification(currentWeight);
            lastNotifiedSampleSequence = sampleSequence;
            lastNotifiedScaleSampleMillis = scale->getLastSampleMillis();
        }

        updateFloat32CompatibilityStream(now);

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

bool BluetoothScale::isConnected() {
    return deviceConnected;
}

void BluetoothScale::sendWeightNotification(float weight) {
    if (!deviceConnected) {
        return;
    }
    
    // Send to GaggiMate first (WeighMyBru protocol format) - critical for backward compatibility
    sendGaggiMateWeight(weight);

    weightNotifyCount++;
    lastWeightNotifyMillis = millis();
}

void BluetoothScale::updateFloat32CompatibilityStream(uint32_t now) {
    if (!deviceConnected || !weightCharacteristic || !scale) {
        return;
    }

    if (lastFloat32NotifyMillis == 0) {
        lastFloat32NotifyMillis = now;
        lastFloat32Weight = getFloat32CompatibilityWeight(now);
        hasLastFloat32Weight = true;
        sendBeanConquerorWeight(lastFloat32Weight);
        return;
    }

    if (now - lastFloat32NotifyMillis < FLOAT32_COMPAT_INTERVAL_MS) {
        return;
    }

    // Keep the compatibility stream paced at 20 Hz without catch-up bursts.
    // If the loop stalls, publish one current sample and re-anchor the schedule.
    if (now - lastFloat32NotifyMillis > FLOAT32_COMPAT_INTERVAL_MS * 2) {
        lastFloat32NotifyMillis = now;
    } else {
        lastFloat32NotifyMillis += FLOAT32_COMPAT_INTERVAL_MS;
    }

    lastFloat32Weight = getFloat32CompatibilityWeight(now);
    hasLastFloat32Weight = true;
    sendBeanConquerorWeight(lastFloat32Weight);
}

float BluetoothScale::getFloat32CompatibilityWeight(uint32_t now) {
    float candidate = scale ? scale->getCurrentWeight() : 0.0f;

    if (!hasLastFloat32Weight) {
        return candidate;
    }

    const bool timerRunning = display && display->isTimerRunning();

    // Glitches are rejected before they become public scale samples. Keep only
    // a tiny pre-shot guard on the legacy Float32 lane so handling noise does
    // not leak, but do not hide real movement once a shot is running.
    if (!timerRunning && scale && scale->hasRecentGlitch(FLOAT32_GLITCH_HOLD_MS)) {
        return lastFloat32Weight;
    }

    // Before the scale timer is running, very short multi-gram disturbances are
    // more likely cup placement / knock / pre-shot handling than useful brew
    // signal. Hold briefly; sustained changes come through on the next 20 Hz
    // ticks once the bump window clears.
    if (!timerRunning && scale && scale->hasRecentBump(FLOAT32_PRE_SHOT_BUMP_HOLD_MS)) {
        return lastFloat32Weight;
    }

    return candidate;
}

void BluetoothScale::sendBeanConquerorWeight(float weight) {
    if (!weightCharacteristic) {
        return;
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
        weightCharacteristic->notify();
        float32NotifyCount++;
        lastFloat32NotifyMillis = lastFloat32NotifyMillis == 0 ? millis() : lastFloat32NotifyMillis;
        
        //Serial.printf("BluetoothScale: Sent Bean Conqueror weight %.2fg as 4-byte float\n", weight);
    } catch (const std::exception& e) {
        Serial.printf("BluetoothScale: ERROR sending Bean Conqueror weight: %s\n", e.what());
    }
}

void BluetoothScale::sendGaggiMateWeight(float weight) {
    if (!gaggiMateWeightCharacteristic) {
        Serial.println("BluetoothScale: WARNING - GaggiMate characteristic is null!");
        return;
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

        const uint32_t sampleTimestampMs =
            scale ? (scale->getLastSampleMillis() & 0x00FFFFFFUL) : (millis() & 0x00FFFFFFUL);
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
        
        const float currentFlowRate = flowRate ? flowRate->getFlowRate() : 0.0f;
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

        packetSequence++;
        payload[14] = packetSequence;

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
        if (flowRate) {
            diagnosticFlags |= DIAG_FLOW_PRESENT;
        }
        payload[18] = diagnosticFlags;
        
        // Calculate and set checksum (last byte)
        payload[PROTOCOL_LENGTH - 1] = calculateChecksum(payload, PROTOCOL_LENGTH - 1);
        
        // Send notification
        gaggiMateWeightCharacteristic->setValue(payload, PROTOCOL_LENGTH);
        gaggiMateWeightCharacteristic->notify();
        
        //Serial.printf("BluetoothScale: Sent GaggiMate weight %.2fg as WeighMyBru protocol\n", weight);
    } catch (const std::exception& e) {
        Serial.printf("BluetoothScale: ERROR sending GaggiMate weight: %s\n", e.what());
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
    } else if (scale) {
        Serial.println("BluetoothScale: Touch tare path unavailable; falling back to default scale tare");
        scale->tare();
    } else {
        Serial.println("BluetoothScale: Cannot tare; scale is not available");
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
    } else if (scale) {
        Serial.println("BluetoothScale: Touch tare path unavailable; falling back to direct atomic tare+start");
        scale->tare();
        if (display) {
            display->resetTimer();
            display->startTimer();
            Serial.println("BluetoothScale: Timer started after direct atomic tare fallback");
        } else {
            Serial.println("BluetoothScale: Timer start requested after tare, but display/timer is unavailable");
        }
    } else {
        Serial.println("BluetoothScale: Cannot atomic tare+start; scale is not available");
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
void BluetoothScale::onConnect(NimBLEServer* pServer) {
    deviceConnected = true;
    NimBLEDevice::stopAdvertising();
    Serial.println("BluetoothScale: Device connected");
}

void BluetoothScale::onDisconnect(NimBLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BluetoothScale: Device disconnected");
}

// BLE Characteristic Callbacks
void BluetoothScale::onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
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
    Serial.println("  wmbPlusExtendedCadence=fresh-scale-sample");
    Serial.printf("  legacyFloat32Cadence=20Hz interval=%lums glitchHold=%lums preShotBumpHold=%lums\n",
                  static_cast<unsigned long>(FLOAT32_COMPAT_INTERVAL_MS),
                  static_cast<unsigned long>(FLOAT32_GLITCH_HOLD_MS),
                  static_cast<unsigned long>(FLOAT32_PRE_SHOT_BUMP_HOLD_MS));
    Serial.printf("  scaleSampleSequence=%lu notifiedSequence=%lu scaleSampleMs=%lu\n",
                  static_cast<unsigned long>(scale ? scale->getSampleSequence() : 0),
                  static_cast<unsigned long>(lastNotifiedSampleSequence),
                  static_cast<unsigned long>(lastNotifiedScaleSampleMillis));
    Serial.printf("  extendedWeightNotifyCount=%lu rate=%.2f/s lastMs=%lu\n",
                  static_cast<unsigned long>(weightNotifyCount), extendedWeightRate,
                  static_cast<unsigned long>(lastWeightNotifyMillis));
    Serial.printf("  float32NotifyCount=%lu rate=%.2f/s lastScheduleMs=%lu lastWeight=%.2f\n",
                  static_cast<unsigned long>(float32NotifyCount), float32Rate,
                  static_cast<unsigned long>(lastFloat32NotifyMillis),
                  lastFloat32Weight);
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
