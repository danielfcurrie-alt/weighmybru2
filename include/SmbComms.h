#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include "espnow_protocol.h"

// ============================================================
// SmbPairingState — FSM states for WMB side of the pairing
// ============================================================
enum class SmbPairingState : uint8_t {
    IDLE,              // No active pairing attempt
    BROADCASTING,      // Sending PAIRING_BEACONs, waiting for SMB response
    AWAITING_CONFIRM,  // Received PAIRING_RESPONSE, sent PAIRING_CONFIRM, listening for STATUS_ACK
    PAIRED             // Active pairing; relay comms operational
};

// ============================================================
// SmbComms — WeighMyBru side of the ESP-NOW link
//
// Responsibilities:
//   - Broadcast PAIRING_BEACON on current channel when requested
//   - Receive PAIRING_RESPONSE from SMB, send PAIRING_CONFIRM
//   - Send RELAY_ON / RELAY_OFF commands (timer start/stop events)
//   - Send WEIGHT_UPDATE every 50ms while paired
//   - Send CONFIG_UPDATE when setpoint or invert changes
//   - Receive STATUS_ACK heartbeats from SMB and cache last state
//   - Persist SMB MAC, channel, setpoint, and invert to NVS ("smb")
// ============================================================

class SmbComms {
public:
    SmbComms();

    // Call once in setup(), after WiFi is initialised
    void begin();

    // Drive state machine — call from every loop() iteration
    void update();

    // Broadcast PAIRING_BEACONs for PAIRING_TIMEOUT_MS (30 s)
    void startPairing();

    // Clear stored pairing and stop all comms
    void unpair();

    // ---- Relay commands ----
    void sendRelayOn();
    void sendRelayOff();

    // ---- Local HTTP webhook relay target ----
    void setWebhookConfig(bool enabled, const String& profile, const String& onUrl, const String& offUrl);
    bool triggerWebhookRelayOn(const char* reason = "manual");
    bool triggerWebhookRelayOff(const char* reason = "manual");
    void setStopLearningEnabled(bool enabled);
    void resetStopLearning();

    // ---- Data commands ----
    // Should be called every 50ms while paired (piggyback on weight loop)
    void sendWeightUpdate(float weight);

    // Push current setpoint + invert flag to SMB; also persists to NVS
    void sendConfig(float setpoint, bool invert);

    // ---- Setters that persist to NVS and push CONFIG_UPDATE if paired ----
    void setSetpoint(float grams);
    void setInvert(bool invert);

    // ---- Accessors (used by WebServer route handlers) ----
    bool             isPaired()         const { return _state == SmbPairingState::PAIRED; }
    SmbPairingState  getPairingState()  const { return _state; }
    const uint8_t*   getSmbMac()        const { return _smbMac; }
    float            getSetpoint()      const { return _setpoint; }
    float            getEffectiveSetpoint() const;
    bool             isInverted()       const { return _invert; }

    // SMB-reported state (from last STATUS_ACK)
    bool   getSmbRelayOn()     const { return _smbRelayOn; }
    float  getSmbLastWeight()  const { return _smbLastWeight; }
    unsigned long getLastSeenMs() const { return _lastStatusAckTime; }

    bool getWebhookEnabled() const { return _webhookEnabled; }
    String getWebhookProfile() const { return _webhookProfile; }
    String getWebhookOnUrl() const { return _webhookOnUrl; }
    String getWebhookOffUrl() const { return _webhookOffUrl; }
    bool getWebhookRelayOn() const { return _webhookRelayOn; }
    bool getWebhookTargetCutSent() const { return _webhookTargetCutSent; }
    int getWebhookLastHttpCode() const { return _webhookLastHttpCode; }
    String getWebhookLastMessage() const { return _webhookLastMessage; }
    unsigned long getWebhookLastAttemptMs() const { return _webhookLastAttemptMs; }
    bool getStopLearningEnabled() const { return _stopLearningEnabled; }
    float getLearnedStopOffset() const { return _learnedStopOffset; }
    uint16_t getStopLearningObservations() const { return _stopLearningObservations; }
    float getLastStopError() const { return _lastStopError; }
    float getLastStopFinalWeight() const { return _lastStopFinalWeight; }
    float getLastStopCutWeight() const { return _lastStopCutWeight; }
    bool getStopLearningAwaitingSettle() const { return _stopLearningAwaitingSettle; }

    // ---- NVS ----
    void loadFromNVS();
    void saveToNVS();

private:
    SmbPairingState _state             = SmbPairingState::IDLE;
    uint8_t         _smbMac[6]        = {0};
    bool            _hasPairedMac     = false;
    bool            _peerRegistered   = false;

    // Pairing broadcast timing
    unsigned long   _pairingStartTime  = 0;
    unsigned long   _lastBeaconTime    = 0;
    static constexpr unsigned long BEACON_INTERVAL_MS   = 500;
    static constexpr unsigned long PAIRING_TIMEOUT_MS   = 30000;

    // Config
    float   _setpoint  = 36.0f;
    bool    _invert    = false;

    // Stop target learning. Positive offset cuts early; negative offset cuts late.
    bool    _stopLearningEnabled = true;
    float   _learnedStopOffset = 0.0f;
    uint16_t _stopLearningObservations = 0;
    float   _lastStopError = 0.0f;
    float   _lastStopFinalWeight = 0.0f;
    float   _lastStopCutWeight = 0.0f;
    float   _lastStopEffectiveSetpoint = 36.0f;
    bool    _stopLearningAwaitingSettle = false;
    bool    _suppressNextRelayOffLearning = false;
    unsigned long _stopLearningStopMillis = 0;
    static constexpr float MAX_EARLY_STOP_OFFSET_G = 8.0f;
    static constexpr float MAX_LATE_STOP_OFFSET_G = 3.0f;
    static constexpr float MAX_LEARNABLE_STOP_ERROR_G = 8.0f;
    static constexpr float STOP_LEARNING_ALPHA = 0.35f;
    static constexpr unsigned long STOP_LEARNING_SETTLE_MS = 3000;

    // Optional local HTTP webhook relay target, e.g. Tasmota or Shelly.
    bool    _webhookEnabled = false;
    String  _webhookProfile = "custom";
    String  _webhookOnUrl;
    String  _webhookOffUrl;
    bool    _webhookRelayOn = false;
    bool    _webhookTargetCutSent = false;
    bool    _webhookOffPending = false;
    float   _webhookPendingCutWeight = 0.0f;
    float   _webhookPendingEffectiveSetpoint = 0.0f;
    int     _webhookLastHttpCode = 0;
    String  _webhookLastMessage;
    unsigned long _webhookLastAttemptMs = 0;

    // Cached SMB-reported state (populated by STATUS_ACK)
    bool          _smbRelayOn        = false;
    float         _smbLastWeight     = 0.0f;
    unsigned long _lastStatusAckTime = 0;

    // Static singleton for C-style callback
    static SmbComms* _instance;

    static void onReceive(const uint8_t* mac, const uint8_t* data, int len);
    void handleRecv(const uint8_t* mac, const uint8_t* data, int len);

    bool registerPeer(const uint8_t* mac);
    void removePeer(const uint8_t* mac);
    bool triggerWebhook(const String& url, const char* action, const char* reason);
    void processPendingWebhookOff();
    void maybeTriggerWebhookTarget(float weight);
    void beginStopLearningObservation(float cutWeight, float effectiveSetpoint, const char* source);
    void maybeCompleteStopLearningObservation(float weight);
    void cancelStopLearningObservation(const char* reason);

    uint8_t getCurrentChannel() const;
};
