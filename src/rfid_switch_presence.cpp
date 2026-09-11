///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid_switch_presence.cpp
//
// RFID tag presence state controller implementation.
//
// Tracks consecutive missed scans and controls the configured relay callback.
//
// https://github.com/matthias-bs/esp32-rfid-switch
//
//
// created: 09/2026
//
//
// MIT License
//
// Copyright (c) 2026 Matthias Prinke
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// History:
//
// 20260911 Initial
//
// ToDo:
// -
//
///////////////////////////////////////////////////////////////////////////////////////////////////

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