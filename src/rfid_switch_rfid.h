///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid_switch_rfid.h
//
// RFID reader initialization and tag validation interface.
//
// Reads and validates EPC, variable-length TID, and optional User Memory token
// data for the RFID switch examples.
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

#pragma once

#include <Arduino.h>
#include <UNIT_UHF_RFID.h>

static const uint8_t RFID_SWITCH_BANK_TID = 0x02;
static const uint8_t RFID_SWITCH_BANK_USER = 0x03;
static const size_t RFID_SWITCH_MAX_TID_BYTES = 20;

struct RfidSwitchTagConfig {
    String epc;
    String tid;
    String accessPassword;
    String token;
};

class RfidSwitchReader {
public:
    bool begin(HardwareSerial *serial, uint8_t rxPin, uint8_t txPin,
               uint8_t region, uint16_t txPower);
    bool hasValidTag(const RfidSwitchTagConfig &config);

private:
    bool readTid(String &tid);
    bool readToken(const RfidSwitchTagConfig &config);
    static bool hexToBytes(const String &value, uint8_t *output, size_t outputSize);
    static bool hexEquals(const String &left, const String &right);
    static bool hexStartsWith(const String &value, const String &prefix);
    static uint32_t parsePassword(const String &value);

    Unit_UHF_RFID reader;
    uint8_t tidBuffer[RFID_SWITCH_MAX_TID_BYTES] = {0};
    uint8_t tokenBuffer[4] = {0};
};