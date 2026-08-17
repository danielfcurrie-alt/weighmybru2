#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include "Scale.h"

class Display; // Forward declaration
class BatteryMonitor; // Forward declaration
class TouchSensor; // Forward declaration
class FlowRate; // Forward declaration

enum class WeighMyBruMessageType : uint8_t {
  SYSTEM = 0x0A,
  WEIGHT = 0x0B
};

enum class BeanConquerorCommand : uint8_t {
  TARE = 0x01,
  TIMER_START = 0x02,
  TIMER_STOP = 0x03,
  TIMER_RESET = 0x04,
  TARE_AND_START_TIMER = 0x07
};

class BluetoothScale : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
    BluetoothScale();
    ~BluetoothScale();
    
    void begin(Scale* scale);
    void begin();  // Initialize without scale reference
    void setScale(Scale* scale);  // Set scale reference later
    void setDisplay(Display* display); // Set display reference for timer control
    void setTouchSensor(TouchSensor* touchSensor); // Route app tare through physical-tare behavior
    void end();
    void update();
    bool isConnected();
    void sendWeight(float weight);
    void handleTareCommand();
    void handleTareAndStartTimerCommand();
    void handleTimerCommand(BeanConquerorCommand command);
    void setBatteryMonitor(BatteryMonitor* batteryMonitor);
    void setFlowRate(FlowRate* flowRate);
    void printDiagnostics();
    int getBluetoothSignalStrength(); // Get BLE signal strength (RSSI)
    String getBluetoothConnectionInfo(); // Get detailed BLE connection information
    uint32_t getExtendedWeightNotifyCount() const { return weightNotifyCount; }
    uint32_t getFloat32NotifyCount() const { return float32NotifyCount; }
    uint32_t getBatteryNotifyCount() const { return batteryNotifyCount; }
    
    // BLE Server callbacks
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
    
    // BLE Characteristic callbacks
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

private:
    Scale* scale;
    Display* display; // Reference to display for timer control
    TouchSensor* touchSensor;
    BatteryMonitor* batteryMonitor;
    FlowRate* flowRate;
    NimBLEServer* server;
    NimBLEService* service;
    NimBLEService* batteryService;
    NimBLECharacteristic* weightCharacteristic;          // Bean Conqueror (simple float)
    NimBLECharacteristic* gaggiMateWeightCharacteristic; // GaggiMate (WeighMyBru protocol)
    NimBLECharacteristic* commandCharacteristic;
    NimBLECharacteristic* capabilitiesCharacteristic;
    NimBLECharacteristic* batteryLevelCharacteristic;
    NimBLEAdvertising* advertising;
    
    bool deviceConnected;
    bool oldDeviceConnected;
    uint32_t lastHeartbeat;
    uint32_t lastBatterySent;
    uint32_t lastNotifiedSampleSequence;
    uint32_t lastNotifiedScaleSampleMillis;
    uint32_t weightNotifyCount;
    uint32_t float32NotifyCount;
    uint8_t packetSequence;
    uint32_t batteryNotifyCount;
    uint32_t lastWeightNotifyMillis;
    uint32_t lastFloat32NotifyMillis;
    uint32_t lastBatteryNotifyMillis;
    float lastFloat32Weight;
    bool hasLastFloat32Weight;
    uint8_t lastBatteryPercent;
    int8_t connectionRSSI; // Store RSSI value for connected device
    uint16_t connectionHandle; // Store connection handle for RSSI queries
    
    // WeighMyBru protocol constants
    static const char* DEVICE_NAME;
    static const uint8_t PRODUCT_NUMBER = 0x03;
    static const size_t PROTOCOL_LENGTH = 20;
    static const uint32_t HEARTBEAT_INTERVAL = 2000; // 2 seconds
    static const uint32_t BATTERY_SEND_INTERVAL = 1000; // Standard battery service update cadence
    static const uint32_t FLOAT32_COMPAT_INTERVAL_MS = 50; // Legacy Float32 compatibility stream: 20 Hz
    static const uint32_t FLOAT32_GLITCH_HOLD_MS = 100;
    static const uint32_t FLOAT32_PRE_SHOT_BUMP_HOLD_MS = 200;
    
    // WeighMyBru UUIDs - unique to avoid conflicts with Bookoo scales
    static const char* SERVICE_UUID;
    static const char* WEIGHT_CHARACTERISTIC_UUID;        // Bean Conqueror (simple float)
    static const char* GAGGIMATE_CHARACTERISTIC_UUID;     // GaggiMate (WeighMyBru protocol)
    static const char* COMMAND_CHARACTERISTIC_UUID;
    static const char* CAPABILITIES_CHARACTERISTIC_UUID;  // Versioned WMB+ feature discovery
    static const char* BATTERY_SERVICE_UUID;
    static const char* BATTERY_LEVEL_CHARACTERISTIC_UUID;
    static const size_t CAPABILITIES_PAYLOAD_LENGTH = 16;
    
    void initializeBLE();
    void startAdvertising();
    void stopAdvertising();
    void sendMessage(WeighMyBruMessageType msgType, const uint8_t* payload, size_t length);
    void sendHeartbeat();
    void sendNotificationRequest();
    void processIncomingMessage(uint8_t* data, size_t length);
    uint8_t calculateChecksum(const uint8_t* data, size_t length);
    void sendWeightNotification(float weight);
    void updateFloat32CompatibilityStream(uint32_t now);
    float getFloat32CompatibilityWeight(uint32_t now);
    void sendBeanConquerorWeight(float weight);    // Send simple float format
    void sendGaggiMateWeight(float weight);        // Send WeighMyBru protocol format
    void updateCapabilities();
    void updateBatteryLevel(bool forceNotify = false);
};
