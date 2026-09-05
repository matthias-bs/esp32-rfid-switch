///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid_switch_webconfig.h
//
// Web configuration interface for RFID Tag Switch
//
// Configure the RFID EPC, TID, password, and token via the web interface.
// EPC and TID are required for tag acceptance; password and token are optional.
// Configure a Shelly BLE address or an exact, case-sensitive device name.
//
// https://github.com/matthias-bs/rfid-switch
//
//
// created: 07/2026
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
// 20260725 Initial
//
// ToDo: 
// - 
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <Arduino.h>
#include <WiFi.h>

extern const char CONFIG_AP_SSID[];
extern const char CONFIG_AP_PASSWORD[];
extern const uint32_t RECONFIG_WINDOW_MS;
extern const uint32_t CONFIG_PORTAL_TIMEOUT_S;
extern const IPAddress CONFIG_AP_IP;

extern bool hasStoredConfig;
extern String configuredNameFilter;
extern String configuredBleAddress;
extern String configuredEpc;
extern String configuredTid;
extern String configuredPassword;
extern String configuredToken;
extern void (*configPortalLoopCallback)();

void loadConfig();
void saveConfig(const String& bleAddress,
				const String& nameFilter,
				const String& epc,
				const String& tid,
				const String& password,
				const String& token);
void runConfigPortal(bool configureNameFilter = false);
