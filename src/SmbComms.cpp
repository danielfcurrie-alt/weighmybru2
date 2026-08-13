#include "SmbComms.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <math.h>
#include "esp_wifi.h"

#define NVS_SMB_NS "smb"

SmbComms* SmbComms::_instance = nullptr;

// ============================================================
SmbComms::SmbComms() {
    _instance = this;
}

// ============================================================
void SmbComms::begin() {
    loadFromNVS();

    if (esp_now_init() != ESP_OK) {
        Serial.println("SMB ESP-NOW: init FAILED");
        return;
    }
    esp_now_register_recv_cb(SmbComms::onReceive);
    Serial.println("SMB ESP-NOW: Initialized");

    if (_hasPairedMac) {
        if (registerPeer(_smbMac)) {
            _peerRegistered = true;
            _state = SmbPairingState::PAIRED;
            Serial.printf("SMB ESP-NOW: Auto-paired (NVS restore) SMB=%02X:%02X:%02X:%02X:%02X:%02X\n",
                          _smbMac[0], _smbMac[1], _smbMac[2],
                          _smbMac[3], _smbMac[4], _smbMac[5]);
            // Push current config immediately so SMB gets setpoint/invert after reboot
            sendConfig(_setpoint, _invert);
        } else {
            Serial.println("SMB ESP-NOW: Peer registration failed — idle (pair again via web UI)");
        }
    } else {
        Serial.println("SMB ESP-NOW: No stored pairing — use web UI to pair");
    }
}

// ============================================================
void SmbComms::update() {
    if (_state != SmbPairingState::BROADCASTING) return;

    unsigned long now = millis();

    // Abort after timeout
    if (now - _pairingStartTime >= PAIRING_TIMEOUT_MS) {
        Serial.println("SMB ESP-NOW: Pairing broadcast timed out");
        _state = SmbPairingState::IDLE;
        return;
    }

    // Send a beacon every 500ms
    if (now - _lastBeaconTime >= BEACON_INTERVAL_MS) {
        PktPairingBeacon pkt;
        pkt.type    = PKT_PAIRING_BEACON;
        pkt.channel = getCurrentChannel();
        WiFi.macAddress(pkt.mac);

        // Broadcast address
        const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        // Ensure broadcast peer exists
        if (!esp_now_is_peer_exist(broadcast)) {
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, broadcast, 6);
            peer.channel = 0;
            peer.ifidx   = WIFI_IF_STA;  // WMB runs in STA mode
            peer.encrypt = false;
            esp_now_add_peer(&peer);
        }
        esp_now_send(broadcast, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        _lastBeaconTime = now;
    }
}

// ============================================================
void SmbComms::startPairing() {
    // Remove any existing peer before starting a new pairing
    if (_hasPairedMac && _peerRegistered) {
        removePeer(_smbMac);
        _peerRegistered = false;
    }
    _hasPairedMac = false;

    _state            = SmbPairingState::BROADCASTING;
    _pairingStartTime = millis();
    _lastBeaconTime   = 0;
    Serial.printf("SMB ESP-NOW: Pairing broadcast started (%lus window, %lums interval)\n",
                  PAIRING_TIMEOUT_MS / 1000, BEACON_INTERVAL_MS);
}

// ============================================================
void SmbComms::unpair() {
    Serial.println("SMB ESP-NOW: Unpairing");
    if (_peerRegistered) {
        removePeer(_smbMac);
        _peerRegistered = false;
    }
    _hasPairedMac = false;
    memset(_smbMac, 0, 6);
    _state = SmbPairingState::IDLE;

    Preferences prefs;
    if (prefs.begin(NVS_SMB_NS, false)) {
        prefs.putBool("paired", false);
        prefs.end();
    }
}

// ============================================================
void SmbComms::sendRelayOn() {
    cancelStopLearningObservation("relay_on");
    _suppressNextRelayOffLearning = false;
    if (_peerRegistered && _state == SmbPairingState::PAIRED) {
        PktRelayCmd pkt;
        pkt.type = PKT_RELAY_ON;
        esp_now_send(_smbMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        Serial.println("SMB ESP-NOW: RELAY_ON sent");
    }
    triggerWebhookRelayOn("relay_on");
}

void SmbComms::sendRelayOff() {
    cancelStopLearningObservation("relay_off");
    _suppressNextRelayOffLearning = true;
    if (_peerRegistered && _state == SmbPairingState::PAIRED) {
        PktRelayCmd pkt;
        pkt.type = PKT_RELAY_OFF;
        esp_now_send(_smbMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        Serial.println("SMB ESP-NOW: RELAY_OFF sent");
    }
    triggerWebhookRelayOff("relay_off");
}

float SmbComms::getEffectiveSetpoint() const {
    const float effective = _stopLearningEnabled ? (_setpoint - _learnedStopOffset) : _setpoint;
    return constrain(effective, 0.0f, _setpoint + MAX_LATE_STOP_OFFSET_G);
}

// ============================================================
// Optional local HTTP webhook relay target
// ============================================================
void SmbComms::setWebhookConfig(bool enabled, const String& profile, const String& onUrl, const String& offUrl) {
    _webhookEnabled = enabled;
    _webhookProfile = profile.length() > 0 ? profile : "custom";
    _webhookOnUrl = onUrl;
    _webhookOffUrl = offUrl;
    _webhookRelayOn = false;
    _webhookTargetCutSent = false;
    saveToNVS();
    Serial.printf("SMB webhook: saved enabled=%d profile=%s on=%u chars off=%u chars\n",
                  enabled ? 1 : 0,
                  _webhookProfile.c_str(),
                  static_cast<unsigned>(_webhookOnUrl.length()),
                  static_cast<unsigned>(_webhookOffUrl.length()));
}

bool SmbComms::triggerWebhookRelayOn(const char* reason) {
    if (!_webhookEnabled) return false;

    cancelStopLearningObservation("webhook_on");
    _webhookRelayOn = true;
    _webhookTargetCutSent = false;

    if (_webhookOnUrl.length() == 0) {
        _webhookLastHttpCode = 0;
        _webhookLastMessage = "Webhook armed; ON URL empty";
        _webhookLastAttemptMs = millis();
        Serial.println("SMB webhook: ON URL empty; armed target cutoff only");
        return true;
    }

    return triggerWebhook(_webhookOnUrl, "on", reason);
}

bool SmbComms::triggerWebhookRelayOff(const char* reason) {
    if (!_webhookEnabled) return false;

    if (!reason || String(reason) != "target_reached") {
        cancelStopLearningObservation(reason ? reason : "webhook_off");
    }
    _webhookRelayOn = false;

    if (_webhookOffUrl.length() == 0) {
        _webhookLastHttpCode = 0;
        _webhookLastMessage = "Webhook OFF URL empty";
        _webhookLastAttemptMs = millis();
        Serial.println("SMB webhook: OFF URL empty");
        return false;
    }

    return triggerWebhook(_webhookOffUrl, "off", reason);
}

bool SmbComms::triggerWebhook(const String& url, const char* action, const char* reason) {
    _webhookLastAttemptMs = millis();

    if (!url.startsWith("http://")) {
        _webhookLastHttpCode = -2;
        _webhookLastMessage = "Only local http:// webhook URLs are supported";
        Serial.printf("SMB webhook %s blocked: %s\n", action, _webhookLastMessage.c_str());
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        _webhookLastHttpCode = -1;
        _webhookLastMessage = "WiFi is not connected";
        Serial.printf("SMB webhook %s skipped: WiFi not connected\n", action);
        return false;
    }

    HTTPClient http;
    http.setTimeout(750);
    if (!http.begin(url)) {
        _webhookLastHttpCode = -3;
        _webhookLastMessage = "HTTP client could not open URL";
        Serial.printf("SMB webhook %s failed: begin() failed\n", action);
        return false;
    }

    const int httpCode = http.GET();
    http.end();

    _webhookLastHttpCode = httpCode;
    const bool ok = httpCode >= 200 && httpCode < 400;
    _webhookLastMessage =
        String(action) + " webhook " + (ok ? "OK" : "failed") +
        " (" + String(httpCode) + ", " + String(reason ? reason : "manual") + ")";
    Serial.printf("SMB webhook %s %s: HTTP %d reason=%s\n",
                  action,
                  ok ? "OK" : "failed",
                  httpCode,
                  reason ? reason : "manual");
    return ok;
}

void SmbComms::maybeTriggerWebhookTarget(float weight) {
    if (!_webhookEnabled || !_webhookRelayOn || _webhookTargetCutSent) return;
    const float effectiveSetpoint = getEffectiveSetpoint();
    if (weight < effectiveSetpoint) return;

    _webhookTargetCutSent = true;
    Serial.printf("SMB webhook: effective target %.1fg reached at %.2fg (desired %.1fg, offset %.2fg); sending OFF webhook\n",
                  effectiveSetpoint, weight, _setpoint, _learnedStopOffset);
    const bool stopped = triggerWebhookRelayOff("target_reached");
    if (stopped) {
        beginStopLearningObservation(weight, effectiveSetpoint, "webhook");
    }
}

void SmbComms::beginStopLearningObservation(float cutWeight, float effectiveSetpoint, const char* source) {
    if (!_stopLearningEnabled || _setpoint <= 0.0f) return;

    _stopLearningAwaitingSettle = true;
    _stopLearningStopMillis = millis();
    _lastStopCutWeight = cutWeight;
    _lastStopEffectiveSetpoint = effectiveSetpoint;
    Serial.printf("SMB stop learning: observing final weight after %s cutoff at %.2fg (target %.2fg, effective %.2fg, offset %.2fg)\n",
                  source ? source : "target",
                  cutWeight,
                  _setpoint,
                  effectiveSetpoint,
                  _learnedStopOffset);
}

void SmbComms::maybeCompleteStopLearningObservation(float weight) {
    if (!_stopLearningAwaitingSettle) return;
    const unsigned long now = millis();
    if (now - _stopLearningStopMillis < STOP_LEARNING_SETTLE_MS) return;

    _stopLearningAwaitingSettle = false;
    _lastStopFinalWeight = weight;
    _lastStopError = weight - _setpoint;

    if (fabsf(_lastStopError) > MAX_LEARNABLE_STOP_ERROR_G ||
        weight < 0.0f ||
        _setpoint <= 0.0f) {
        Serial.printf("SMB stop learning: ignored final %.2fg for target %.2fg (error %.2fg too large or invalid)\n",
                      weight, _setpoint, _lastStopError);
        saveToNVS();
        return;
    }

    const float oldOffset = _learnedStopOffset;
    const float nextOffset = oldOffset + (_lastStopError * STOP_LEARNING_ALPHA);
    _learnedStopOffset = constrain(nextOffset, -MAX_LATE_STOP_OFFSET_G, MAX_EARLY_STOP_OFFSET_G);
    if (_stopLearningObservations < UINT16_MAX) {
        _stopLearningObservations++;
    }

    saveToNVS();
    sendConfig(_setpoint, _invert);
    Serial.printf("SMB stop learning: final %.2fg target %.2fg error %.2fg offset %.2fg -> %.2fg (%u obs, effective %.2fg)\n",
                  weight,
                  _setpoint,
                  _lastStopError,
                  oldOffset,
                  _learnedStopOffset,
                  _stopLearningObservations,
                  getEffectiveSetpoint());
}

void SmbComms::cancelStopLearningObservation(const char* reason) {
    if (!_stopLearningAwaitingSettle) return;
    _stopLearningAwaitingSettle = false;
    Serial.printf("SMB stop learning: observation cancelled (%s)\n", reason ? reason : "manual");
}

void SmbComms::setStopLearningEnabled(bool enabled) {
    _stopLearningEnabled = enabled;
    cancelStopLearningObservation(enabled ? "learning_enabled" : "learning_disabled");
    saveToNVS();
    sendConfig(_setpoint, _invert);
}

void SmbComms::resetStopLearning() {
    _learnedStopOffset = 0.0f;
    _stopLearningObservations = 0;
    _lastStopError = 0.0f;
    _lastStopFinalWeight = 0.0f;
    _lastStopCutWeight = 0.0f;
    _lastStopEffectiveSetpoint = _setpoint;
    _stopLearningAwaitingSettle = false;
    saveToNVS();
    sendConfig(_setpoint, _invert);
    Serial.println("SMB stop learning: reset");
}

// ============================================================
void SmbComms::sendWeightUpdate(float weight) {
    maybeCompleteStopLearningObservation(weight);
    maybeTriggerWebhookTarget(weight);

    if (!_peerRegistered || _state != SmbPairingState::PAIRED) return;

    PktWeightUpdate pkt;
    pkt.type   = PKT_WEIGHT_UPDATE;
    pkt.weight = weight;
    esp_now_send(_smbMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

// ============================================================
void SmbComms::sendConfig(float setpoint, bool invert) {
    if (!_peerRegistered || _state != SmbPairingState::PAIRED) return;
    PktConfigUpdate pkt;
    pkt.type     = PKT_CONFIG_UPDATE;
    pkt.setpoint = getEffectiveSetpoint();
    pkt.invert   = invert ? 1 : 0;
    pkt.channel  = getCurrentChannel();
    esp_now_send(_smbMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    Serial.printf("SMB ESP-NOW: CONFIG_UPDATE sent (desired=%.1fg, effective=%.1fg, offset=%.2fg, invert=%d)\n",
                  setpoint, pkt.setpoint, _learnedStopOffset, invert ? 1 : 0);
}

// ============================================================
void SmbComms::setSetpoint(float grams) {
    _setpoint = grams;
    saveToNVS();
    sendConfig(_setpoint, _invert);
}

void SmbComms::setInvert(bool invert) {
    _invert = invert;
    saveToNVS();
    sendConfig(_setpoint, _invert);
}

// ============================================================
// Static ESP-NOW receive callback (WiFi driver task)
// ============================================================
void SmbComms::onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (_instance) {
        _instance->handleRecv(mac, data, len);
    }
}

void SmbComms::handleRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 1) return;
    const uint8_t pktType = data[0];

    switch (pktType) {

    // ----------------------------------------------------------
    case PKT_PAIRING_RESPONSE: {
        if (_state != SmbPairingState::BROADCASTING) break;
        if (len < static_cast<int>(sizeof(PktPairingResponse))) break;

        Serial.printf("SMB ESP-NOW: PAIRING_RESPONSE from %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        // Register SMB as peer so we can send the CONFIRM
        if (!esp_now_is_peer_exist(mac)) {
            registerPeer(mac);
        }

        // Send PAIRING_CONFIRM
        PktPairingConfirm confirm;
        confirm.type    = PKT_PAIRING_CONFIRM;
        confirm.channel = getCurrentChannel();
        WiFi.macAddress(confirm.mac);

        esp_err_t err = esp_now_send(mac,
                                     reinterpret_cast<const uint8_t*>(&confirm),
                                     sizeof(confirm));
        if (err == ESP_OK) {
            memcpy(_smbMac, mac, 6);
            _hasPairedMac   = true;
            _peerRegistered = true;
            _state          = SmbPairingState::PAIRED;
            saveToNVS();
            Serial.printf("SMB ESP-NOW: PAIRED! SMB=%02X:%02X:%02X:%02X:%02X:%02X\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            Serial.printf("SMB ESP-NOW: PAIRING_CONFIRM send error: %s\n",
                          esp_err_to_name(err));
            removePeer(mac);
        }
        break;
    }

    // ----------------------------------------------------------
    case PKT_STATUS_ACK: {
        if (_state != SmbPairingState::PAIRED) break;
        if (len < static_cast<int>(sizeof(PktStatusAck))) break;

        const auto* pkt = reinterpret_cast<const PktStatusAck*>(data);
        const bool wasRelayOn = _smbRelayOn;
        _smbRelayOn       = pkt->relayOn != 0;
        _smbLastWeight    = pkt->lastWeight;
        _lastStatusAckTime = millis();
        if (wasRelayOn && !_smbRelayOn && _suppressNextRelayOffLearning) {
            _suppressNextRelayOffLearning = false;
        } else if (wasRelayOn && !_smbRelayOn && !_stopLearningAwaitingSettle) {
            beginStopLearningObservation(pkt->lastWeight, getEffectiveSetpoint(), "esp-now");
        }
        break;
    }

    default:
        break;
    }
}

// ============================================================
// Peer helpers
// ============================================================
bool SmbComms::registerPeer(const uint8_t* mac) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;  // WMB runs in STA mode
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("SMB ESP-NOW: esp_now_add_peer failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

void SmbComms::removePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
}

uint8_t SmbComms::getCurrentChannel() const {
    uint8_t ch = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ch, &sec);
    return ch;
}

// ============================================================
// NVS
// ============================================================
void SmbComms::loadFromNVS() {
    Preferences prefs;
    if (prefs.begin(NVS_SMB_NS, /*readOnly=*/true)) {
        _hasPairedMac = prefs.getBool("paired", false);
        if (_hasPairedMac) {
            size_t n = prefs.getBytes("smb_mac", _smbMac, 6);
            if (n != 6) _hasPairedMac = false;
        }
        _setpoint = prefs.getFloat("setpoint", 36.0f);
        _invert   = prefs.getBool ("invert",   false);
        _stopLearningEnabled = prefs.getBool("sl_en", true);
        _learnedStopOffset = prefs.getFloat("sl_off", 0.0f);
        _learnedStopOffset = constrain(_learnedStopOffset, -MAX_LATE_STOP_OFFSET_G, MAX_EARLY_STOP_OFFSET_G);
        _stopLearningObservations = prefs.getUShort("sl_obs", 0);
        _lastStopError = prefs.getFloat("sl_err", 0.0f);
        _lastStopFinalWeight = prefs.getFloat("sl_final", 0.0f);
        _lastStopCutWeight = prefs.getFloat("sl_cut", 0.0f);
        _lastStopEffectiveSetpoint = prefs.getFloat("sl_eff", _setpoint);
        _webhookEnabled = prefs.getBool("wh_en", false);
        _webhookProfile = prefs.getString("wh_prof", "custom");
        _webhookOnUrl = prefs.getString("wh_on", "");
        _webhookOffUrl = prefs.getString("wh_off", "");
        prefs.end();
    }
}

void SmbComms::saveToNVS() {
    Preferences prefs;
    if (prefs.begin(NVS_SMB_NS, /*readOnly=*/false)) {
        prefs.putBool ("paired",   _hasPairedMac);
        if (_hasPairedMac) {
            prefs.putBytes("smb_mac", _smbMac, 6);
        }
        prefs.putFloat("setpoint", _setpoint);
        prefs.putBool ("invert",   _invert);
        prefs.putBool("sl_en", _stopLearningEnabled);
        prefs.putFloat("sl_off", _learnedStopOffset);
        prefs.putUShort("sl_obs", _stopLearningObservations);
        prefs.putFloat("sl_err", _lastStopError);
        prefs.putFloat("sl_final", _lastStopFinalWeight);
        prefs.putFloat("sl_cut", _lastStopCutWeight);
        prefs.putFloat("sl_eff", _lastStopEffectiveSetpoint);
        prefs.putBool("wh_en", _webhookEnabled);
        prefs.putString("wh_prof", _webhookProfile);
        prefs.putString("wh_on", _webhookOnUrl);
        prefs.putString("wh_off", _webhookOffUrl);
        prefs.end();
    }
}
