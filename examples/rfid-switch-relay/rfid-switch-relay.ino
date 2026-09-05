///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid-switch-relay.ino
//
// Example for RFID Tag Switch - Relay
// 
// Configure the RFID EPC, TID, password, and token via the web interface.
// EPC and TID are required; password and token can be left empty.
// The relay will be activated when a tag with the configured EPC etc. is detected.
// Presence of the tag will keep the relay activated. When the tag is removed, the relay will
// be deactivated after a configurable delay.
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

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <rfid_switch_webconfig.h>
#include <rfid_switch_presence.h>
#if defined(ARDUINO_M5STACK_CORE2)
#include <M5Unified.h>
#endif

// ============================================================================
// Configuration constants
// ============================================================================

/** GPIO pin used to request reconfiguration during the startup window. */
static const int CONFIG_PIN = 0;

/** Time window after boot in which pressing BOOT enters config mode. */
const uint32_t RECONFIG_WINDOW_MS = 3000;

/** AP SSID shown during configuration. */
const char CONFIG_AP_SSID[] = "RFID-Switch-Setup";

/** AP password (minimum 8 characters). */
const char CONFIG_AP_PASSWORD[] = "12345678";

/** Configuration portal timeout in seconds. */
const uint32_t CONFIG_PORTAL_TIMEOUT_S = 300;

/** IP address assigned to the ESP32 in AP mode. */
const IPAddress CONFIG_AP_IP(192, 168, 4, 1);

/**
 * GPIO connected to the relay input.
 *
 * The selected GPIO determines the sleep mode: RTC-capable pins use GPIO
 * output hold with deep sleep to preserve the relay state; non-RTC pins use
 * light sleep because their output state cannot be retained through deep
 * sleep.
 */
#if defined(ARDUINO_ESP32S3_DEV)
static const uint8_t RELAY_PIN = 47;
#elif defined(ARDUINO_M5STACK_CORE2)
static const uint8_t RELAY_PIN = 32; // PORT A - yellow
#elif defined(ARDUINO_ESP32_DEV)
static const uint8_t RELAY_PIN = 5;
#endif

/** UHF reader UART pins for the ESP32 host. */
#if defined(ARDUINO_ESP32S3_DEV)
static const uint8_t RFID_RX_PIN = 1;
static const uint8_t RFID_TX_PIN = 2;
#elif defined(ARDUINO_M5STACK_CORE2)
static const uint8_t RFID_RX_PIN = 13;
static const uint8_t RFID_TX_PIN = 14;
#elif defined(ARDUINO_ESP32_DEV)
static const uint8_t RFID_RX_PIN = 16;
static const uint8_t RFID_TX_PIN = 17;
#else
#pragma message "Unknown device."
#endif

/** UHF operating region: 3 = Europe; replace with another supported region code as needed. */
static const uint8_t RFID_OPERATING_REGION = 3;
static const uint16_t RFID_TX_POWER = 2600;
static const uint8_t RFID_REMOVAL_MISSES = 3;
static const uint32_t RFID_SLEEP_DURATION_SECONDS = 5;

static const uint32_t RTC_STATE_MAGIC = 0x52464944;
static const bool RELAY_PIN_IS_RTC_CAPABLE =
    rtc_gpio_is_valid_gpio(static_cast<gpio_num_t>(RELAY_PIN));

RTC_DATA_ATTR static uint32_t rtcStateMagic;
RTC_DATA_ATTR static bool rtcRelayEnabled;
RTC_DATA_ATTR static uint8_t rtcMissedScans;

// ============================================================================
// Globals
// ============================================================================

extern Preferences prefs;

extern String configuredNameFilter;
extern String configuredEpc;
extern String configuredTid;
extern String configuredPassword;
extern String configuredToken;

static RfidSwitchReader rfidReader;
static RfidSwitchTagConfig tagConfig;
static RfidSwitchPresenceController *presenceController = nullptr;

#if defined(ARDUINO_M5STACK_CORE2)
static void showConfigPortalScreen() {
    M5.Display.setBrightness(255);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(10, 80);
    M5.Display.println("Open http://192.168.4.1");
    M5.Display.setCursor(10, 110);
    M5.Display.println("and set configuration.");
}

static void blinkConfigLed() {
    static uint32_t lastToggleMs = 0;
    static bool ledOn = false;

    if (millis() - lastToggleMs >= 500) {
        lastToggleMs = millis();
        ledOn = !ledOn;
        M5.Power.setLed(ledOn ? 255 : 0);
    }
}
#endif

static bool setRelay(bool enabled, void *) {
    log_d("[RELAY] GPIO %u -> %s (%s hold)",
          RELAY_PIN,
          enabled ? "ON" : "OFF",
          RELAY_PIN_IS_RTC_CAPABLE ? "RTC" : "no");

    if (RELAY_PIN_IS_RTC_CAPABLE) {
        gpio_hold_dis(static_cast<gpio_num_t>(RELAY_PIN));
        gpio_deep_sleep_hold_dis();
    }

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
#if defined(ARDUINO_M5STACK_CORE2)
    M5.Power.setLed(enabled ? 255 : 0);
#endif

    if (RELAY_PIN_IS_RTC_CAPABLE) {
        gpio_hold_en(static_cast<gpio_num_t>(RELAY_PIN));
        gpio_deep_sleep_hold_en();
    }
    return true;
}

#if defined(ARDUINO_M5STACK_CORE2)
static bool shouldEnterConfigMode() {
    log_i("Press Button A within %lu ms after startup for web config mode.",
          static_cast<unsigned long>(RECONFIG_WINDOW_MS));

    uint32_t startMs = millis();
    while (millis() - startMs < RECONFIG_WINDOW_MS) {
        M5.update();
        const bool configButtonPressed = M5.BtnA.isPressed();
        if (configButtonPressed) {
            delay(30);  // Simple debounce for Button A.
            M5.update();
            if (M5.BtnA.isPressed()) {
                return true;
            }
        }
        delay(10);
    }

    return false;
}
#else
static bool shouldEnterConfigMode() {
    log_i("Press BOOT within %lu ms after startup for web config mode.",
          static_cast<unsigned long>(RECONFIG_WINDOW_MS));

    uint32_t startMs = millis();
    while (millis() - startMs < RECONFIG_WINDOW_MS) {
        const bool configButtonPressed = digitalRead(CONFIG_PIN) == LOW;
        if (configButtonPressed) {
            delay(30);  // Simple debounce for the BOOT button.
            if (digitalRead(CONFIG_PIN) == LOW) {
                return true;
            }
        }
        delay(10);
    }

    return false;
}
#endif

static void shutdownWifi() {
    const wifi_mode_t mode = WiFi.getMode();

    log_d("[WIFI] Shutting down Wi-Fi (mode %d)", static_cast<int>(mode));

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        WiFi.softAPdisconnect(true);
    }
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        WiFi.disconnect(true, true);
    }

    WiFi.mode(WIFI_OFF);
    log_i("[WIFI] Wi-Fi is off.");
}

void setup() {
    #if defined(ARDUINO_M5STACK_CORE2)
    M5.begin();
    M5.Display.setBrightness(0);
    M5.Power.setLed(0);
    #endif
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(500);
    log_i("=== RFID Tag Switch - Relay ===");
    log_i("[BOOT] Wake cause: %d", static_cast<int>(esp_sleep_get_wakeup_cause()));
    log_i("[BOOT] Relay GPIO: %u (%s), reader RX/TX: %u/%u",
          RELAY_PIN,
          RELAY_PIN_IS_RTC_CAPABLE ? "RTC-capable" : "not RTC-capable",
          RFID_RX_PIN,
          RFID_TX_PIN);

    setRelay(false, nullptr);
#if !defined(ARDUINO_M5STACK_CORE2)
    pinMode(CONFIG_PIN, INPUT_PULLUP);
#endif

    loadConfig();
    log_i("[CONFIG] Stored config: %s, EPC='%s', TID='%s', token=%s",
        hasStoredConfig ? "yes" : "no",
        configuredEpc.c_str(),
        configuredTid.c_str(),
        configuredToken.length() > 0 ? "set" : "not set");

    bool forceConfig = shouldEnterConfigMode();
    bool noConfig = !hasStoredConfig;

    // Start configuration on first boot or when forced by button.
    if (forceConfig || noConfig) {
        if (forceConfig) {
            log_i("Configuration button detected during startup window.");
        } else {
            log_w("No stored configuration found; entering config mode.");
        }
#if defined(ARDUINO_M5STACK_CORE2)
            showConfigPortalScreen();
        configPortalLoopCallback = blinkConfigLed;
#endif
        runConfigPortal();
        log_i("[CONFIG] Configuration portal returned.");
    }

    tagConfig.epc = configuredEpc;
    tagConfig.tid = configuredTid;
    tagConfig.accessPassword = configuredPassword;
    tagConfig.token = configuredToken;

    // Ensure Wi-Fi is off before starting the RFID reader.
    shutdownWifi();

    const bool restoreState = rtcStateMagic == RTC_STATE_MAGIC;
    log_i("[RTC] Saved state: %s, relay=%s, missed scans=%u",
        restoreState ? "valid" : "none",
        restoreState && rtcRelayEnabled ? "ON" : "OFF",
        restoreState ? rtcMissedScans : 0);
    if (restoreState) {
        setRelay(rtcRelayEnabled, nullptr);
    }

        log_i("[RFID] Starting reader on Serial2, region=%u, TX power=%u",
                    RFID_OPERATING_REGION, RFID_TX_POWER);
    if (!rfidReader.begin(&Serial2, RFID_RX_PIN, RFID_TX_PIN,
                          RFID_OPERATING_REGION, RFID_TX_POWER)) {
                log_e("[RFID] Reader initialization failed; relay remains OFF.");
        rtcStateMagic = RTC_STATE_MAGIC;
        rtcRelayEnabled = false;
        rtcMissedScans = 0;
        setRelay(false, nullptr);
        return;
    }
    log_i("[RFID] Reader initialized successfully.");

    static RfidSwitchPresenceController controller(
        rfidReader, tagConfig, setRelay, nullptr,
        0, RFID_REMOVAL_MISSES);
    presenceController = &controller;
    presenceController->begin(restoreState && rtcRelayEnabled,
                              restoreState ? rtcMissedScans : 0);
        log_i("[PRESENCE] Controller started: relay=%s, missed scans=%u/%u",
            presenceController->isRelayEnabled() ? "ON" : "OFF",
            presenceController->missedScanCount(),
            RFID_REMOVAL_MISSES);
}

void loop() {
    if (presenceController != nullptr) {
        presenceController->tick();

        rtcStateMagic = RTC_STATE_MAGIC;
        rtcRelayEnabled = presenceController->isRelayEnabled();
        rtcMissedScans = presenceController->missedScanCount();

          log_i("[SCAN] Result: %s, relay=%s, missed scans=%u/%u",
              rtcRelayEnabled ? "matching tag present" : "no matching tag",
              rtcRelayEnabled ? "ON" : "OFF",
              rtcMissedScans,
              RFID_REMOVAL_MISSES);
          log_i("[SLEEP] Sleeping for %lu s (%s)",
              static_cast<unsigned long>(RFID_SLEEP_DURATION_SECONDS),
              RELAY_PIN_IS_RTC_CAPABLE ? "deep sleep" : "light sleep");

        esp_sleep_enable_timer_wakeup(
            static_cast<uint64_t>(RFID_SLEEP_DURATION_SECONDS) * 1000000ULL);
        if (RELAY_PIN_IS_RTC_CAPABLE) {
            esp_deep_sleep_start();
        } else {
            esp_light_sleep_start();
        }
    }
}
