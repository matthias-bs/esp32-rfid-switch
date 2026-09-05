/*
 * RFID Tag Switch - M5Stack Core2 touch configuration
 *
 * This example uses GDTouchKeyboard instead of the Wi-Fi Web Config portal.
 * Select the output variant at compile time. With no external build define,
 * the relay variant is selected by default.
 *
 * To build the Shelly variant, define RFID_SWITCH_VARIANT_SHELLY and remove
 * RFID_SWITCH_VARIANT_RELAY, or change the default selection below.
 */

#if !defined(RFID_SWITCH_VARIANT_RELAY) && \
    !defined(RFID_SWITCH_VARIANT_SHELLY)
#define RFID_SWITCH_VARIANT_RELAY
#endif

#if defined(RFID_SWITCH_VARIANT_RELAY) && \
    defined(RFID_SWITCH_VARIANT_SHELLY)
#error "Select exactly one RFID switch variant."
#endif

#if !defined(ARDUINO_M5STACK_CORE2)
#error "This example requires an M5Stack Core2."
#endif

#include <Arduino.h>
#include <ctype.h>
#include <M5Unified.h>
#include <GDTouchKeyboard.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <rfid_switch_webconfig.h>
#include <rfid_switch_presence.h>

#if defined(RFID_SWITCH_VARIANT_SHELLY)
#include <ShellyBleRpc.h>
#endif

// Core2 wiring: UHF reader RX/TX = GPIO 13/14, relay = Port A GPIO 32.
static const uint8_t RFID_RX_PIN = 13;
static const uint8_t RFID_TX_PIN = 14;
static const uint8_t RFID_OPERATING_REGION = 3;
static const uint16_t RFID_TX_POWER = 2600;
static const uint8_t RFID_REMOVAL_MISSES = 3;
static const uint32_t RFID_SLEEP_DURATION_SECONDS = 5;
const uint32_t RECONFIG_WINDOW_MS = 3000;
static const uint32_t RTC_STATE_MAGIC = 0x52464944;

#if defined(RFID_SWITCH_VARIANT_RELAY)
static const uint8_t RELAY_PIN = 32;
static const bool RELAY_PIN_IS_RTC_CAPABLE =
    rtc_gpio_is_valid_gpio(static_cast<gpio_num_t>(RELAY_PIN));
#else
static const uint32_t SHELLY_SCAN_DURATION_MS = 5000;
static const uint8_t SHELLY_SWITCH_ID = 0;
#endif

RTC_DATA_ATTR static uint32_t rtcStateMagic;
RTC_DATA_ATTR static bool rtcSwitchEnabled;
RTC_DATA_ATTR static uint8_t rtcMissedScans;

extern bool hasStoredConfig;
extern String configuredBleAddress;
extern String configuredNameFilter;
extern String configuredEpc;
extern String configuredTid;
extern String configuredPassword;
extern String configuredToken;

static RfidSwitchReader rfidReader;
static RfidSwitchTagConfig tagConfig;
static RfidSwitchPresenceController *presenceController = nullptr;

#if defined(RFID_SWITCH_VARIANT_SHELLY)
static ShellyBleRpc shelly;
#endif

enum ConfigFieldKind
{
    CONFIG_HEX,
    CONFIG_FIXED_HEX,
    CONFIG_MAC,
    CONFIG_NAME
};

struct ConfigField
{
    const char *label;
    const char *prompt;
    const char *description;
    String *value;
    uint16_t minimumLength;
    uint16_t maximumLength;
    uint8_t availableModes;
    GDTouchKeyboard::key_mode_t initialMode;
    ConfigFieldKind kind;
    bool required;
};

static bool isHex(const String &value)
{
    for (size_t index = 0; index < value.length(); ++index)
    {
        const char character = value[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F')))
        {
            return false;
        }
    }
    return true;
}

static bool isNameCharacter(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           character == '-' || character == '_' || character == ' ';
}

static bool isName(const String &value)
{
    for (size_t index = 0; index < value.length(); ++index)
    {
        if (!isNameCharacter(value[index]))
        {
            return false;
        }
    }
    return true;
}

static bool validateKeyboardInput(const String &candidate)
{
    return isHex(candidate);
}

static bool validateNameInput(const String &candidate)
{
    return isName(candidate);
}

static bool validateFinalField(const ConfigField &field)
{
    const String &value = *field.value;
    if (field.required && value.length() == 0)
    {
        return false;
    }
    if (field.kind == CONFIG_HEX)
    {
        return isHex(value) && (value.length() % 2) == 0;
    }
    if (field.kind == CONFIG_FIXED_HEX)
    {
        return value.length() == 0 || (value.length() == 8 && isHex(value));
    }
    if (field.kind == CONFIG_MAC)
    {
        if (value.length() == 0)
        {
            return true;
        }
        if (value.length() != 17)
        {
            return false;
        }
        for (size_t index = 0; index < value.length(); ++index)
        {
            if ((index + 1) % 3 == 0)
            {
                if (value[index] != ':')
                {
                    return false;
                }
            }
            else if (!isxdigit(static_cast<unsigned char>(value[index])))
            {
                return false;
            }
        }
        return true;
    }
    return isName(value);
}

#if defined(RFID_SWITCH_VARIANT_RELAY)
static ConfigField configuration[] =
{
    {"EPC", "EPC (even hex, max 124)", "required", &configuredEpc,
     2, 124, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_HEX, true},
    {"TID", "TID (even hex, max 40)", "required", &configuredTid,
     2, 40, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_HEX, true},
    {"Password", "Password (optional, 8 hex)", "optional", &configuredPassword,
     0, 8, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_FIXED_HEX, false},
    {"Token", "Token (optional, 8 hex)", "optional", &configuredToken,
     0, 8, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_FIXED_HEX, false},
};
#else
static ConfigField configuration[] =
{
    {"EPC", "EPC (even hex, max 124)", "required", &configuredEpc,
     2, 124, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_HEX, true},
    {"TID", "TID (even hex, max 40)", "required", &configuredTid,
     2, 40, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_HEX, true},
    {"Password", "Password (optional, 8 hex)", "optional", &configuredPassword,
     0, 8, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_FIXED_HEX, false},
    {"Token", "Token (optional, 8 hex)", "optional", &configuredToken,
     0, 8, 1 << GDTouchKeyboard::KEY_MODE_HEX,
     GDTouchKeyboard::KEY_MODE_HEX, CONFIG_FIXED_HEX, false},
    {"BLE address", "BLE address (12 hex)", "optional", &configuredBleAddress,
        0, 12, 1 << GDTouchKeyboard::KEY_MODE_MAC,
     GDTouchKeyboard::KEY_MODE_MAC, CONFIG_MAC, false},
    {"Name filter", "Shelly name (max 40 chars)", "optional", &configuredNameFilter,
     0, 40, (1 << GDTouchKeyboard::KEY_MODE_LETTER) |
            (1 << GDTouchKeyboard::KEY_MODE_NUMBER),
     GDTouchKeyboard::KEY_MODE_LETTER, CONFIG_NAME, false},
};
#endif

static const size_t configurationCount =
    sizeof(configuration) / sizeof(configuration[0]);
static const size_t configurationMenuCount = configurationCount + 1;

enum ConfigurationAction
{
    CONFIG_PREVIOUS,
    CONFIG_EDIT,
    CONFIG_NEXT
};

static bool hasValidConfiguration()
{
    for (size_t index = 0; index < configurationCount; ++index)
    {
        if (!validateFinalField(configuration[index]))
        {
            return false;
        }
    }
#if defined(RFID_SWITCH_VARIANT_SHELLY)
    return configuredBleAddress.length() > 0 || configuredNameFilter.length() > 0;
#else
    return true;
#endif
}

static void drawConfigurationOverview(size_t selectedField,
                                      const char *message = nullptr)
{
    const int controlsHeight = 40;
    const int controlsY = M5.Display.height() - controlsHeight;
    const int statusY = controlsY - 18;
    const int rowHeight = (statusY - 2) /
                          static_cast<int>(configurationMenuCount);

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextSize(1);

    for (size_t index = 0; index < configurationMenuCount; ++index)
    {
        const int y = 2 + static_cast<int>(index * rowHeight);
        const bool selected = index == selectedField;
        const uint16_t background = selected ? TFT_WHITE : TFT_BLACK;
        const uint16_t foreground = selected ? TFT_BLACK : TFT_WHITE;
        M5.Display.fillRect(0, y, M5.Display.width(), rowHeight - 2, background);
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextColor(foreground, background);
        const bool isSaveEntry = index == configurationCount;
        M5.Display.drawString(isSaveEntry ? "Save" : configuration[index].label,
                              6, y + 3);

        String value = isSaveEntry ? "Edit to finish"
                                   : *configuration[index].value;
        if (value.length() == 0)
        {
            value = "<empty>";
        }
        while (value.length() > 0 && M5.Display.textWidth(value) > 205)
        {
            value = value.substring(1);
        }
        M5.Display.setTextDatum(TR_DATUM);
        M5.Display.drawString(value, M5.Display.width() - 6, y + 3);
        M5.Display.setTextDatum(TL_DATUM);
    }

    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    if (message != nullptr)
    {
        M5.Display.drawString(message, 6, statusY + 1);
    }
    else if (hasValidConfiguration())
    {
        M5.Display.drawString("Ready", 6, statusY + 1);
    }
    else
    {
        M5.Display.drawString("Complete required fields", 6, statusY + 1);
    }

    const int buttonCount = 3;
    const int buttonWidth = M5.Display.width() / buttonCount;
    const char *buttonLabels[] = {"  Prev  ", "  Edit  ", "  Next  "};
    const int labelY = controlsY + 3;
    const int arrowBaseY = M5.Display.height() - 5;
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    for (int index = 0; index < buttonCount; ++index)
    {
        const int x = index * buttonWidth;
        const int width = index == buttonCount - 1 ? M5.Display.width() - x
                                : buttonWidth;
        const int centerX = x + (width / 2);
        const int labelWidth = M5.Display.textWidth(buttonLabels[index]) + 4;
        M5.Display.fillRect(centerX - (labelWidth / 2), labelY,
                            labelWidth, 24, TFT_WHITE);
        M5.Display.drawString(buttonLabels[index], centerX, labelY + 12);
        const int labelTextWidth = static_cast<int>(M5.Display.textWidth(buttonLabels[index]));
        const int arrowHalfWidth = max(12, labelTextWidth / 2);
        M5.Display.fillTriangle(centerX, M5.Display.height() - 1,
                                centerX - arrowHalfWidth, arrowBaseY,
                                centerX + arrowHalfWidth, arrowBaseY,
                                TFT_WHITE);
    }
}

static uint8_t waitForConfigurationAction()
{
    while (true)
    {
        M5.update();
        if (M5.BtnA.wasClicked())
        {
            return CONFIG_PREVIOUS;
        }
        if (M5.BtnC.wasClicked())
        {
            return CONFIG_NEXT;
        }
        if (M5.BtnB.isPressed())
        {
            while (M5.BtnB.isPressed())
            {
                M5.update();
            }
            return CONFIG_EDIT;
        }

        if (M5.Touch.getCount() > 0)
        {
            const auto touch = M5.Touch.getDetail();
            const int controlsY = M5.Display.height() - 40;
            if (touch.wasPressed() && touch.y >= controlsY)
            {
                const int buttonCount = 3;
                const int buttonWidth = M5.Display.width() / buttonCount;
                const int button = min(buttonCount - 1, touch.x / buttonWidth);
                return static_cast<ConfigurationAction>(button);
            }
        }
    }
}

static String readConfigurationField(const ConfigField &field)
{
    GDTK.setTouchFeedback(true);
    GDTK.setAvailableModes(field.availableModes);
    GDTK.setInputLength(field.minimumLength, field.maximumLength);
    GDTK.setInputValidator(field.kind == CONFIG_NAME
                               ? validateNameInput
                               : validateKeyboardInput);
    return GDTK.run(*field.value, 0x0ad9, true, &fonts::Font0,
                    field.initialMode, true, field.prompt);
}

static void saveTouchConfiguration()
{
#if defined(RFID_SWITCH_VARIANT_RELAY)
    saveConfig("", "", configuredEpc, configuredTid,
               configuredPassword, configuredToken);
#else
    saveConfig(configuredBleAddress, configuredNameFilter, configuredEpc,
               configuredTid, configuredPassword, configuredToken);
#endif
    hasStoredConfig = true;
}

static void runTouchConfiguration()
{
    M5.Display.setBrightness(255);
    size_t selectedField = 0;
    while (true)
    {
        drawConfigurationOverview(selectedField);
        const ConfigurationAction action = static_cast<ConfigurationAction>(
            waitForConfigurationAction());

        if (action == CONFIG_PREVIOUS)
        {
            selectedField = selectedField == 0 ? configurationMenuCount - 1
                                               : selectedField - 1;
            continue;
        }
        if (action == CONFIG_NEXT)
        {
            selectedField = (selectedField + 1) % configurationMenuCount;
            continue;
        }

        if (selectedField == configurationCount)
        {
            if (hasValidConfiguration())
            {
                saveTouchConfiguration();
                M5.Display.setBrightness(0);
                return;
            }
            drawConfigurationOverview(selectedField, "Required fields are missing");
            delay(1200);
            continue;
        }

        const String newValue = readConfigurationField(configuration[selectedField]);
        *configuration[selectedField].value = newValue;
        if (validateFinalField(configuration[selectedField]))
        {
            saveTouchConfiguration();
        }
        else
        {
            drawConfigurationOverview(selectedField, "Invalid value; edit again");
            delay(1200);
        }
    }
}

static bool shouldEnterConfigMode()
{
    log_i("Press Button A within %lu ms for touch configuration.",
          static_cast<unsigned long>(RECONFIG_WINDOW_MS));
    const uint32_t startMs = millis();
    while (millis() - startMs < RECONFIG_WINDOW_MS)
    {
        M5.update();
        if (M5.BtnA.isPressed())
        {
            delay(30);
            M5.update();
            if (M5.BtnA.isPressed())
            {
                while (M5.BtnA.isPressed())
                {
                    M5.update();
                }
                return true;
            }
        }
        delay(10);
    }
    return false;
}

#if defined(RFID_SWITCH_VARIANT_RELAY)
static bool setSwitch(bool enabled, void *)
{
    log_i("[RELAY] GPIO %u -> %s (%s hold)",
          RELAY_PIN,
          enabled ? "ON" : "OFF",
          RELAY_PIN_IS_RTC_CAPABLE ? "RTC" : "no");

    if (RELAY_PIN_IS_RTC_CAPABLE)
    {
        gpio_hold_dis(static_cast<gpio_num_t>(RELAY_PIN));
        gpio_deep_sleep_hold_dis();
    }

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
    M5.Power.setLed(enabled ? 255 : 0);

    if (RELAY_PIN_IS_RTC_CAPABLE)
    {
        gpio_hold_en(static_cast<gpio_num_t>(RELAY_PIN));
        gpio_deep_sleep_hold_en();
    }
    return true;
}
#else
static bool setSwitch(bool enabled, void *)
{
    if (!shelly.isConnected())
    {
        return false;
    }
    String response;
    return shelly.switchSet(SHELLY_SWITCH_ID, enabled, response);
}

static bool connectShelly()
{
    shelly.setDebug(false);
    if (!shelly.begin())
    {
        return false;
    }
    if (configuredBleAddress.length() > 0)
    {
        return shelly.connect(configuredBleAddress.c_str());
    }
    return shelly.scanAndConnect(
        SHELLY_SCAN_DURATION_MS,
        configuredNameFilter.length() > 0 ? configuredNameFilter.c_str() : nullptr);
}
#endif

static void enterSleep()
{
    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(RFID_SLEEP_DURATION_SECONDS) * 1000000ULL);
#if defined(RFID_SWITCH_VARIANT_RELAY)
    if (RELAY_PIN_IS_RTC_CAPABLE)
    {
        esp_deep_sleep_start();
    }
    else
    {
        esp_light_sleep_start();
    }
#else
    shelly.disconnect();
    esp_deep_sleep_start();
#endif
}

void setup()
{
    M5.begin();
    M5.Display.setBrightness(0);
#if defined(RFID_SWITCH_VARIANT_RELAY)
    M5.Power.setLed(rtcStateMagic == RTC_STATE_MAGIC && rtcSwitchEnabled
                        ? 255
                        : 0);
#else
    M5.Power.setLed(0);
#endif
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(500);
#if defined(RFID_SWITCH_VARIANT_RELAY)
    Serial.println("\n=== RFID Tag Switch - Core2 Touch Relay ===");
#else
    Serial.println("\n=== RFID Tag Switch - Core2 Touch Shelly BLE ===");
#endif
    log_i("[BOOT] Wake cause: %d", static_cast<int>(esp_sleep_get_wakeup_cause()));
    log_i("[BOOT] Relay GPIO: %u (%s), reader RX/TX: %u/%u",
          RELAY_PIN,
          RELAY_PIN_IS_RTC_CAPABLE ? "RTC-capable" : "not RTC-capable",
          RFID_RX_PIN,
          RFID_TX_PIN);

    loadConfig();
    log_i("[CONFIG] Stored config: %s, EPC='%s', TID='%s', token=%s",
          hasStoredConfig ? "yes" : "no",
          configuredEpc.c_str(),
          configuredTid.c_str(),
          configuredToken.length() > 0 ? "set" : "not set");
    const bool forceConfig = shouldEnterConfigMode();
    if (forceConfig || !hasStoredConfig || !hasValidConfiguration())
    {
        log_i("[CONFIG] Starting touch configuration (%s).",
              forceConfig ? "requested" : "missing or invalid");
        runTouchConfiguration();
        log_i("[CONFIG] Touch configuration returned.");
    }

    tagConfig.epc = configuredEpc;
    tagConfig.tid = configuredTid;
    tagConfig.accessPassword = configuredPassword;
    tagConfig.token = configuredToken;

    const bool restoreState = rtcStateMagic == RTC_STATE_MAGIC;
    log_i("[RTC] Saved state: %s, relay=%s, missed scans=%u",
          restoreState ? "valid" : "none",
          restoreState && rtcSwitchEnabled ? "ON" : "OFF",
          restoreState ? rtcMissedScans : 0);
#if defined(RFID_SWITCH_VARIANT_SHELLY)
    if (!connectShelly())
    {
        Serial.println("Shelly BLE connection failed; switch state unchanged.");
        enterSleep();
        return;
    }
#endif

#if defined(RFID_SWITCH_VARIANT_RELAY)
    if (restoreState)
    {
        setSwitch(rtcSwitchEnabled, nullptr);
    }
#endif

    log_i("[RFID] Starting reader on Serial2, region=%u, TX power=%u",
          RFID_OPERATING_REGION, RFID_TX_POWER);
    if (!rfidReader.begin(&Serial2, RFID_RX_PIN, RFID_TX_PIN,
                          RFID_OPERATING_REGION, RFID_TX_POWER))
    {
        Serial.println("RFID reader initialization failed; switch state unchanged.");
        rtcStateMagic = RTC_STATE_MAGIC;
        rtcSwitchEnabled = false;
        rtcMissedScans = 0;
#if defined(RFID_SWITCH_VARIANT_SHELLY)
        shelly.disconnect();
#endif
        log_e("[RFID] Reader initialization failed; output remains OFF.");
        setSwitch(false, nullptr);
        enterSleep();
        return;
    }
    log_i("[RFID] Reader initialized successfully.");

    static RfidSwitchPresenceController controller(
        rfidReader, tagConfig, setSwitch, nullptr,
        0, RFID_REMOVAL_MISSES);
    presenceController = &controller;
    presenceController->begin(restoreState && rtcSwitchEnabled,
                              restoreState ? rtcMissedScans : 0);
        log_i("[PRESENCE] Controller started: relay=%s, missed scans=%u/%u",
            presenceController->isRelayEnabled() ? "ON" : "OFF",
            presenceController->missedScanCount(),
            RFID_REMOVAL_MISSES);
}

void loop()
{
    if (presenceController == nullptr)
    {
        enterSleep();
        return;
    }

    presenceController->tick();
    rtcStateMagic = RTC_STATE_MAGIC;
    rtcSwitchEnabled = presenceController->isRelayEnabled();
    rtcMissedScans = presenceController->missedScanCount();
    log_i("[SCAN] Result: %s, relay=%s, missed scans=%u/%u",
          rtcSwitchEnabled ? "matching tag present" : "no matching tag",
          rtcSwitchEnabled ? "ON" : "OFF",
          rtcMissedScans,
          RFID_REMOVAL_MISSES);
    log_i("[SLEEP] Sleeping for %lu s (%s)",
          static_cast<unsigned long>(RFID_SLEEP_DURATION_SECONDS),
#if defined(RFID_SWITCH_VARIANT_RELAY)
          RELAY_PIN_IS_RTC_CAPABLE ? "deep sleep" : "light sleep"
#else
          "deep sleep"
#endif
    );
    enterSleep();
}
