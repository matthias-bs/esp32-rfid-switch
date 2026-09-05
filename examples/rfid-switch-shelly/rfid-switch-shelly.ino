#if defined(ESP32) && !defined(CONFIG_ARDUINO_ISR_IRAM)
#define CONFIG_ARDUINO_ISR_IRAM 0
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <esp_sleep.h>
#include <ShellyBleRpc.h>
#include <rfid_switch_webconfig.h>
#include <rfid_switch_presence.h>

static const int CONFIG_PIN = 0;
#if defined(ARDUINO_M5STACK_CORE2)
static const uint8_t CORE2_BUTTON_A_PIN = 37;
#endif
const uint32_t RECONFIG_WINDOW_MS = 3000;
const char CONFIG_AP_SSID[] = "RFID-Switch-Setup";
const char CONFIG_AP_PASSWORD[] = "12345678";
const uint32_t CONFIG_PORTAL_TIMEOUT_S = 300;
const IPAddress CONFIG_AP_IP(192, 168, 4, 1);

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
static const uint8_t RFID_OPERATING_REGION = 3;
static const uint16_t RFID_TX_POWER = 2600;
static const uint8_t RFID_REMOVAL_MISSES = 3;
static const uint32_t RFID_SLEEP_DURATION_SECONDS = 5;
static const uint32_t SHELLY_SCAN_DURATION_MS = 5000;
static const uint8_t SHELLY_SWITCH_ID = 0;
static const uint32_t RTC_STATE_MAGIC = 0x52464944;

RTC_DATA_ATTR static uint32_t rtcStateMagic;
RTC_DATA_ATTR static bool rtcSwitchEnabled;
RTC_DATA_ATTR static uint8_t rtcMissedScans;

extern String configuredBleAddress;
extern String configuredNameFilter;
extern String configuredEpc;
extern String configuredTid;
extern String configuredPassword;
extern String configuredToken;
extern bool hasStoredConfig;

static RfidSwitchReader rfidReader;
static RfidSwitchTagConfig tagConfig;
static ShellyBleRpc shelly;
static RfidSwitchPresenceController *presenceController = nullptr;

static bool shouldEnterConfigMode()
{
#if defined(ARDUINO_M5STACK_CORE2)
    log_i("Press Button A within %lu ms after startup for web config mode.",
          static_cast<unsigned long>(RECONFIG_WINDOW_MS));
#else
    log_i("Press BOOT within %lu ms after startup for web config mode.",
          static_cast<unsigned long>(RECONFIG_WINDOW_MS));
#endif

    uint32_t startMs = millis();
    while (millis() - startMs < RECONFIG_WINDOW_MS) {
        const bool configButtonPressed = digitalRead(
#if defined(ARDUINO_M5STACK_CORE2)
            CORE2_BUTTON_A_PIN
#else
            CONFIG_PIN
#endif
        ) == LOW;
        if (configButtonPressed) {
            delay(30);
            if (digitalRead(
#if defined(ARDUINO_M5STACK_CORE2)
                    CORE2_BUTTON_A_PIN
#else
                    CONFIG_PIN
#endif
                ) == LOW) {
                return true;
            }
        }
        delay(10);
    }
    return false;
}

static void shutdownWifi()
{
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}

static bool setShellySwitch(bool enabled, void *)
{
    if (!shelly.isConnected()) {
        return false;
    }

    String response;
    return shelly.switchSet(SHELLY_SWITCH_ID, enabled, response);
}

static bool connectShelly()
{
    shelly.setDebug(false);
    if (!shelly.begin()) {
        return false;
    }

    if (configuredBleAddress.length() > 0) {
        return shelly.connect(configuredBleAddress.c_str());
    }

    const char *nameFilter = configuredNameFilter.length() > 0
                                 ? configuredNameFilter.c_str()
                                 : nullptr;
    return shelly.scanAndConnect(SHELLY_SCAN_DURATION_MS, nameFilter);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== RFID Tag Switch - Shelly BLE ===");

    pinMode(
#if defined(ARDUINO_M5STACK_CORE2)
        CORE2_BUTTON_A_PIN
#else
        CONFIG_PIN
#endif
        , INPUT_PULLUP);
    loadConfig();

    const bool forceConfig = shouldEnterConfigMode();
    const bool noConfig = !hasStoredConfig;
    const bool noShellyTarget = configuredBleAddress.length() == 0 &&
                                configuredNameFilter.length() == 0;
    if (forceConfig || noConfig || noShellyTarget) {
        runConfigPortal(true);
    }

    tagConfig.epc = configuredEpc;
    tagConfig.tid = configuredTid;
    tagConfig.accessPassword = configuredPassword;
    tagConfig.token = configuredToken;

    shutdownWifi();

    const bool restoreState = rtcStateMagic == RTC_STATE_MAGIC;
    if (!connectShelly()) {
        Serial.println("Shelly BLE connection failed; switch state unchanged.");
        return;
    }

    if (!rfidReader.begin(&Serial2, RFID_RX_PIN, RFID_TX_PIN,
                          RFID_OPERATING_REGION, RFID_TX_POWER)) {
        Serial.println("RFID reader initialization failed; switch state unchanged.");
        shelly.disconnect();
        return;
    }

    static RfidSwitchPresenceController controller(
        rfidReader, tagConfig, setShellySwitch, nullptr,
        0, RFID_REMOVAL_MISSES);
    presenceController = &controller;
    presenceController->begin(restoreState && rtcSwitchEnabled,
                              restoreState ? rtcMissedScans : 0);
}

void loop()
{
    if (presenceController == nullptr) {
        esp_sleep_enable_timer_wakeup(
            static_cast<uint64_t>(RFID_SLEEP_DURATION_SECONDS) * 1000000ULL);
        esp_deep_sleep_start();
    }

    presenceController->tick();
    rtcStateMagic = RTC_STATE_MAGIC;
    rtcSwitchEnabled = presenceController->isRelayEnabled();
    rtcMissedScans = presenceController->missedScanCount();
    shelly.disconnect();

    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(RFID_SLEEP_DURATION_SECONDS) * 1000000ULL);
    esp_deep_sleep_start();
}