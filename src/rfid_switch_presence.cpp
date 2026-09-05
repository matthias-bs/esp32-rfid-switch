#include "rfid_switch_presence.h"

RfidSwitchPresenceController::RfidSwitchPresenceController(
    RfidSwitchReader &reader,
    const RfidSwitchTagConfig &tagConfig,
    RfidSwitchRelayCallback relayCallback,
    void *relayContext,
    uint32_t scanIntervalMs,
    uint8_t removalMisses)
    : reader(reader),
      tagConfig(tagConfig),
      relayCallback(relayCallback),
      relayContext(relayContext),
      scanIntervalMs(scanIntervalMs),
      removalMisses(removalMisses == 0 ? 1 : removalMisses),
      lastScanMs(0),
      missedScans(0),
      relayEnabled(false),
      hasScanned(false)
{
}

void RfidSwitchPresenceController::begin(bool initialRelayEnabled, uint8_t initialMissedScans)
{
    relayEnabled = initialRelayEnabled;
    missedScans = initialMissedScans > removalMisses ? removalMisses : initialMissedScans;
    hasScanned = false;
    if (!relayCallback(relayEnabled, relayContext)) {
        relayEnabled = false;
    }
}

void RfidSwitchPresenceController::tick(uint32_t nowMs)
{
    if (hasScanned && (nowMs - lastScanMs < scanIntervalMs)) {
        return;
    }

    hasScanned = true;
    lastScanMs = nowMs;

    if (reader.hasValidTag(tagConfig)) {
        missedScans = 0;
        if (!relayEnabled) {
            if (relayCallback(true, relayContext)) {
                relayEnabled = true;
            }
        }
        return;
    }

    if (missedScans < removalMisses) {
        ++missedScans;
    }
    if (relayEnabled && missedScans >= removalMisses) {
        if (relayCallback(false, relayContext)) {
            relayEnabled = false;
        }
    }
}

bool RfidSwitchPresenceController::isRelayEnabled() const
{
    return relayEnabled;
}

uint8_t RfidSwitchPresenceController::missedScanCount() const
{
    return missedScans;
}