///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid_switch_webconfig.cpp
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

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ctype.h>

#include <rfid_switch_webconfig.h>

// EPC (Electronic Product Code) Length
// -------------------------------------
// Typical Range: 96 bits to 496 bits.
// Standard Sizes: 96 bits and 128 bits are the most common industry standards.

// TID (Tag Identifier) Length
// -------------------------------------
// Typical Range: 32 bits to 160 bits.
// Standard Sizes: Most modern chips use a 64-bit or 96-bit factory-locked unique ID.

Preferences prefs;

bool hasStoredConfig = false;
String configuredNameFilter;
String configuredBleAddress;
String configuredEpc;
String configuredTid;
String configuredPassword;
String configuredToken;
void (*configPortalLoopCallback)() = nullptr;

static const size_t MAX_EPC_LEN = 496 / 8 * 2; // Maximum length for configured EPC.
static const size_t MAX_TID_LEN = 160 / 8 * 2; // Maximum length for configured TID.
static const size_t MAX_WORD_HEX_LEN = 8;      // Password/token are one 32-bit word in hex.

/** Maximum length for configured device name. */
static const size_t MAX_NAME_FILTER_LEN = 40;
static const size_t MAX_BLE_ADDRESS_LEN = 17;

// ============================================================================
// NVS helpers
// ============================================================================

void loadConfig()
{
    prefs.begin("shelly-ble", true);
    hasStoredConfig = prefs.getBool("configured", false);
    configuredBleAddress = prefs.getString("ble_address", "");
    configuredNameFilter = prefs.getString("name_filter", "");
    configuredEpc = prefs.getString("epc", "");
    configuredTid = prefs.getString("tid", "");
    configuredPassword = prefs.getString("password", "");
    configuredToken = prefs.getString("token", "");
    prefs.end();

    configuredBleAddress.trim();
    configuredNameFilter.trim();
    configuredEpc.trim();
    configuredTid.trim();
    configuredPassword.trim();
    configuredToken.trim();
}

void saveConfig(const String &bleAddress,
                const String &nameFilter,
                const String &epc,
                const String &tid,
                const String &password,
                const String &token)
{
    prefs.begin("shelly-ble", false);
    prefs.putBool("configured", true);
    prefs.putString("ble_address", bleAddress);
    prefs.putString("name_filter", nameFilter);
    prefs.putString("epc", epc);
    prefs.putString("tid", tid);
    prefs.putString("password", password);
    prefs.putString("token", token);
    prefs.end();
}

static bool isHexString(const String &value)
{
    for (size_t i = 0; i < value.length(); ++i)
    {
        if (!isxdigit(static_cast<unsigned char>(value[i])))
        {
            return false;
        }
    }
    return true;
}

static bool validateOptionalHex(const String &value, size_t maxLen, bool requireEvenLength)
{
    if (value.length() == 0)
    {
        return true;
    }
    if (value.length() > maxLen)
    {
        return false;
    }
    if (requireEvenLength && ((value.length() % 2) != 0))
    {
        return false;
    }
    return isHexString(value);
}

static bool validateOptionalFixedHex(const String &value, size_t fixedLen)
{
    if (value.length() == 0)
    {
        return true;
    }
    if (value.length() != fixedLen)
    {
        return false;
    }
    return isHexString(value);
}

static bool validateOptionalBleAddress(const String &value)
{
    if (value.length() == 0) {
        return true;
    }
    if (value.length() != MAX_BLE_ADDRESS_LEN) {
        return false;
    }
    for (size_t index = 0; index < value.length(); ++index) {
        if ((index + 1) % 3 == 0) {
            if (value[index] != ':') {
                return false;
            }
        } else if (!isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Web configuration portal
// ============================================================================

static WebServer server(80);
static bool sConfigureNameFilter = true;

static void shutdownWifi()
{
    const wifi_mode_t mode = WiFi.getMode();

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        WiFi.softAPdisconnect(true);
    }
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
    {
        WiFi.disconnect(true, true);
    }

    WiFi.mode(WIFI_OFF);
}

// Minimal CSS / HTML shared across pages.
static const char HTML_STYLE[] PROGMEM =
    "body{font-family:Arial,sans-serif;max-width:480px;margin:40px auto;"
    "padding:20px;background:#f4f4f4;}"
    "h1{color:#d63031;}"
    ".card{background:#fff;padding:20px;border-radius:8px;"
    "box-shadow:0 2px 6px rgba(0,0,0,.15);}"
    "label{display:block;margin-top:14px;font-weight:bold;}"
    "input,select{width:100%;padding:8px;margin-top:4px;box-sizing:border-box;"
    "border:1px solid #ccc;border-radius:4px;font-size:1em;}"
    ".btn{display:block;margin-top:22px;width:100%;padding:12px;background:#d63031;"
    "color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer;}"
    ".btn:hover{background:#c0392b;}"
    ".note{color:#636e72;font-size:.85em;margin-top:6px;}";

static String htmlEscape(String s)
{
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&#39;");
    return s;
}

static void sendPage(int code, const String &body)
{
    String page;
    page.reserve(800 + body.length());
    page += "<!DOCTYPE html><html><head>"
            "<meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>RFID Tag Switch Config</title>"
            "<style>";
    page += FPSTR(HTML_STYLE);
    page += "</style></head><body><div class='card'>"
            "<h1>&#x1F194; RFID Tag Switch</h1>";
    page += body;
    page += "</div></body></html>";
    server.send(code, "text/html", page);
}

static void handleRoot()
{
    String body;
    body.reserve(1400);
    body += "<form action='/save' method='POST'>";
    body += "<label>RFID EPC (Electronic Product Code)</label>";
    body += "<input type='text' name='epc'"
            " placeholder='E2801170200020A4B3C5D6E7'"
            " value='" +
            htmlEscape(configuredEpc) + "'"
                                        " inputmode='latin'"
                                        " pattern='[0-9A-Fa-f]*'"
                                        " title='Hex only, even length'"
                                        " spellcheck='false'"
                                        " maxlength='" +
            String(MAX_EPC_LEN) + "'>";

    body += "<label>RFID TID (Tag Identifier)</label>";
    body += "<input type='text' name='tid'"
            " placeholder='E2003412'"
            " value='" +
            htmlEscape(configuredTid) + "'"
                                        " inputmode='latin'"
                                        " pattern='[0-9A-Fa-f]*'"
                                        " title='Hex only, even length'"
                                        " spellcheck='false'"
                                        " maxlength='" +
            String(MAX_TID_LEN) + "'>";
    body += "<p class='note'>EPC and TID are both required for acceptance.</p>";

    body += "<label>RFID User Memory Password</label>";
    body += "<input type='text' id='password' name='password'"
            " placeholder='A1B2C3D4'"
            " value='" +
            htmlEscape(configuredPassword) + "'"
                                             " inputmode='latin'"
                                             " pattern='([0-9A-Fa-f]{8})?'"
                                             " title='Leave empty or use exactly 8 hex chars'"
                                             " spellcheck='false'"
                                             " autocomplete='off'"
                                             " minlength='8'"
                                             " maxlength='8'>";
    body += "<p class='note'>Leave empty to skip, or enter exactly 8 hex characters.</p>";

    body += "<label>RFID User Memory Data (Token)</label>";
    body += "<input type='text' id='token' name='token'"
            " placeholder='11223344'"
            " value='" +
            htmlEscape(configuredToken) + "'"
                                          " inputmode='latin'"
                                          " pattern='([0-9A-Fa-f]{8})?'"
                                          " title='Leave empty or use exactly 8 hex chars'"
                                          " spellcheck='false'"
                                          " autocomplete='off'"
                                          " minlength='8'"
                                          " maxlength='8'>";
    body += "<p class='note'>Leave empty to skip, or enter exactly 8 hex characters.</p>";

    if (sConfigureNameFilter)
    {
        body += "<label>Shelly BLE Address (optional)</label>";
        body += "<input type='text' name='ble_address'"
                " placeholder='AA:BB:CC:DD:EE:FF'"
                " value='" +
                htmlEscape(configuredBleAddress) + "'"
                                                  " pattern='([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}'"
                                                  " title='Use six hexadecimal byte pairs separated by colons'"
                                                  " spellcheck='false' maxlength='17'>";
        body += "<p class='note'>Set an address or a device name filter.</p>";

        body += "<label>Shelly Device Name (exact, case-sensitive, optional)</label>";
        body += "<input type='text' name='name_filter'"
                " placeholder='ShellyPlus1-ABCDEF'"
                " value='" +
                htmlEscape(configuredNameFilter) + "'"
                                                   " maxlength='" +
                String(MAX_NAME_FILTER_LEN) + "'>";
        body += "<p class='note'>Leave empty to connect to the strongest nearby Shelly.</p>";
    }
    body += "<button class='btn' type='submit'>Save &amp; Restart</button>";
    body += "</form>";
    body += "<script>"
            "(function(){"
            "function attach(id,label){"
            "var el=document.getElementById(id);"
            "if(!el)return;"
            "el.addEventListener('input',function(){"
            "var v=el.value.trim();"
            "if(v.length===0){el.setCustomValidity('');return;}"
            "if(!/^[0-9A-Fa-f]+$/.test(v)){el.setCustomValidity(label+': use hex only (0-9, A-F).');return;}"
            "if(v.length!==8){el.setCustomValidity(label+': enter exactly 8 hex characters.');return;}"
            "el.setCustomValidity('');"
            "});"
            "}"
            "attach('password','Password');"
            "attach('token','Token');"
            "})();"
            "</script>";
    sendPage(200, body);
}

static void handleSave()
{
    String newEpc = server.hasArg("epc") ? server.arg("epc") : "";
    String newTid = server.hasArg("tid") ? server.arg("tid") : "";
    String newPassword = server.hasArg("password") ? server.arg("password") : "";
    String newToken = server.hasArg("token") ? server.arg("token") : "";
    String newBleAddress = configuredBleAddress;
    String newFilter = configuredNameFilter;
    if (sConfigureNameFilter)
    {
        newBleAddress = server.hasArg("ble_address") ? server.arg("ble_address") : "";
        newFilter = server.hasArg("name_filter") ? server.arg("name_filter") : "";
    }

    newEpc.trim();
    newTid.trim();
    newPassword.trim();
    newToken.trim();
    newBleAddress.trim();
    newFilter.trim();

    if (sConfigureNameFilter && newBleAddress.length() == 0 && newFilter.length() == 0)
    {
        String body = "<p style='color:red'>Set a Shelly BLE address or device name filter.</p>"
                      "<a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    if (!validateOptionalBleAddress(newBleAddress))
    {
        String body = "<p style='color:red'>Invalid BLE address. Use AA:BB:CC:DD:EE:FF.</p>"
                      "<a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    if (!validateOptionalHex(newEpc, MAX_EPC_LEN, true))
    {
        String body = "<p style='color:red'>Invalid EPC. Use hex only, even length, max " +
                      String(MAX_EPC_LEN) +
                      " chars.</p><a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    if (!validateOptionalHex(newTid, MAX_TID_LEN, true))
    {
        String body = "<p style='color:red'>Invalid TID. Use hex only, even length, max " +
                      String(MAX_TID_LEN) +
                      " chars.</p><a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    if (!validateOptionalFixedHex(newPassword, MAX_WORD_HEX_LEN))
    {
        String body = "<p style='color:red'>Invalid password. Use exactly " +
                      String(MAX_WORD_HEX_LEN) +
                      " hex chars or leave empty.</p><a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    if (!validateOptionalFixedHex(newToken, MAX_WORD_HEX_LEN))
    {
        String body = "<p style='color:red'>Invalid token. Use exactly " +
                      String(MAX_WORD_HEX_LEN) +
                      " hex chars or leave empty.</p><a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    if (sConfigureNameFilter && (newFilter.length() > MAX_NAME_FILTER_LEN))
    {
        String body = "<p style='color:red'>Name is too long.</p>"
                      "<a href='/'>&#x2190; Back</a>";
        sendPage(400, body);
        return;
    }

    saveConfig(newBleAddress, newFilter, newEpc, newTid, newPassword, newToken);

    configuredBleAddress = newBleAddress;
    configuredNameFilter = newFilter;
    configuredEpc = newEpc;
    configuredTid = newTid;
    configuredPassword = newPassword;
    configuredToken = newToken;

    String body;
    body += "<p style='color:green'>&#x2714; Configuration saved!</p>";
    if (sConfigureNameFilter && newFilter.length() > 0)
    {
        body += "<p>Name filter: <b>" + htmlEscape(newFilter) + "</b></p>";
    }
    else if (sConfigureNameFilter)
    {
        body += "<p>Name filter: <b>(none)</b></p>";
    }
    body += "<p>The device will restart in a moment ...</p>";
    sendPage(200, body);

    delay(1500);
    ESP.restart();
}

/** Redirect everything else back to the root form (captive-portal style). */
static void handleNotFound()
{
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Redirecting ...");
}

void runConfigPortal(bool configureNameFilter)
{
    sConfigureNameFilter = configureNameFilter;

    Serial.println("--- Configuration mode ---");
    Serial.printf("AP SSID: %s\n", CONFIG_AP_SSID);
    Serial.printf("AP IP:   %s\n", CONFIG_AP_IP.toString().c_str());
    Serial.println("Open http://192.168.4.1 and set configuration.");

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(CONFIG_AP_IP, CONFIG_AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    uint32_t startMs = millis();
    while (millis() - startMs < (CONFIG_PORTAL_TIMEOUT_S * 1000UL))
    {
        server.handleClient();
        if (configPortalLoopCallback != nullptr)
        {
            configPortalLoopCallback();
        }
        delay(2);
    }

    Serial.println("Config timeout reached. Restarting ...");

    server.stop();
    shutdownWifi();

    ESP.restart();
}

// ============================================================================
// BLE scan/connect helpers
// ============================================================================

// static const char* activeNameFilter() {
//     return configuredNameFilter.length() > 0 ? configuredNameFilter.c_str() : nullptr;
// }
