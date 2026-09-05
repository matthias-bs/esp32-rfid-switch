#pragma once

#include <Arduino.h>
#include "rfid_switch_rfid.h"

typedef bool (*RfidSwitchRelayCallback)(bool enabled, void *context);

class RfidSwitchPresenceController {
public:
    RfidSwitchPresenceController(RfidSwitchReader &reader,
                                 const RfidSwitchTagConfig &tagConfig,
                                 RfidSwitchRelayCallback relayCallback,
                                 void *relayContext,
                                 uint32_t scanIntervalMs,
                                 uint8_t removalMisses);

    void begin(bool initialRelayEnabled = false, uint8_t initialMissedScans = 0);
    void tick(uint32_t nowMs = millis());
    bool isRelayEnabled() const;
    uint8_t missedScanCount() const;

private:
    RfidSwitchReader &reader;
    const RfidSwitchTagConfig &tagConfig;
    RfidSwitchRelayCallback relayCallback;
    void *relayContext;
    uint32_t scanIntervalMs;
    uint8_t removalMisses;
    uint32_t lastScanMs;
    uint8_t missedScans;
    bool relayEnabled;
    bool hasScanned;
};