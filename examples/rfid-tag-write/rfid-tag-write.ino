///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid-tag-write.ino
//
// Example for RFID Tag Switch - Tag Writer
//
// Identify RFID tags and initialize their EPC/TID configuration, optional access password,
// and optional User Memory token from a JSON object received through the serial interface.
// User Memory is locked after initialization to match the runtime switch examples.
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
// 20260909 Initial
//
// ToDo:
// -
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <ArduinoJson.h>
#include <UNIT_UHF_RFID.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
#error "Unsupported board: define an RFID UART pin mapping for this target."
#endif

static const uint8_t RFID_BANK_RESERVED = 0x00;
static const uint8_t RFID_BANK_TID = 0x02;
static const uint8_t RFID_BANK_USER = 0x03;
static const uint8_t RFID_REGION = 3;
static const uint16_t RFID_TX_POWER = 2600;
static const uint32_t RFID_LOCK_FLAGS = 0x030C82;
static const size_t MAX_EPC_HEX_LENGTH = 124;
static const size_t MAX_TID_HEX_LENGTH = 40;
static const size_t ACCESS_PASSWORD_HEX_LENGTH = 8;
static const size_t SECRET_TOKEN_HEX_LENGTH = 8;
static const size_t MAX_JSON_LENGTH = 1024;
static const uint32_t JSON_TIMEOUT_MS = 15000;

struct TagConfiguration
{
  String epc;
  String tid;
  String currentAccessPassword;
  String accessPassword;
  String secretToken;
};

Unit_UHF_RFID uhf;
bool ignoreJsonLineEnd = false;

static String bufferToHexString(const uint8_t *data, size_t size)
{
  String result;
  result.reserve(size * 2);
  for (size_t index = 0; index < size; ++index)
  {
    if (data[index] < 0x10)
    {
      result += '0';
    }
    result += String(data[index], HEX);
  }
  result.toUpperCase();
  return result;
}

static int hexValue(char value)
{
  if (value >= '0' && value <= '9')
  {
    return value - '0';
  }
  if (value >= 'A' && value <= 'F')
  {
    return value - 'A' + 10;
  }
  if (value >= 'a' && value <= 'f')
  {
    return value - 'a' + 10;
  }
  return -1;
}

static bool isHexString(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    if (hexValue(value[index]) < 0)
    {
      return false;
    }
  }
  return true;
}

static bool validateHex(const String &value, size_t maximumLength, bool requireEvenLength)
{
  return value.length() > 0 && value.length() <= maximumLength &&
         (!requireEvenLength || (value.length() % 2 == 0)) && isHexString(value);
}

static bool validateOptionalFixedHex(const String &value, size_t requiredLength)
{
  return value.length() == 0 ||
         (value.length() == requiredLength && isHexString(value));
}

static bool hexToBytes(const String &value, uint8_t *output, size_t outputSize)
{
  if (value.length() != outputSize * 2)
  {
    return false;
  }

  for (size_t index = 0; index < outputSize; ++index)
  {
    const int high = hexValue(value[index * 2]);
    const int low = hexValue(value[index * 2 + 1]);
    if (high < 0 || low < 0)
    {
      return false;
    }
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

static bool hexToWord(const String &value, uint32_t &word)
{
  uint8_t bytes[4] = {0};
  if (!hexToBytes(value, bytes, sizeof(bytes)))
  {
    return false;
  }

  word = (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         bytes[3];
  return true;
}

static bool isTidPrefix(const String &tid, const String &prefix)
{
  return prefix.length() <= tid.length() &&
         tid.substring(0, prefix.length()).equalsIgnoreCase(prefix);
}

static bool readJsonString(JsonDocument &document, const char *name, String &value,
                           bool required)
{
  JsonVariant field = document[name];
  if (field.isNull())
  {
    value = "";
    return !required;
  }
  if (!field.is<const char *>())
  {
    return false;
  }

  value = field.as<const char *>();
  value.trim();
  return true;
}

static bool parseConfiguration(const String &json, TagConfiguration &configuration)
{
  JsonDocument document;
  DeserializationError error = deserializeJson(document, json);
  if (error)
  {
    log_e("JSON parse failed: %s", error.c_str());
    return false;
  }
  if (!readJsonString(document, "epc", configuration.epc, true) ||
      !readJsonString(document, "tid", configuration.tid, true) ||
      !readJsonString(document, "current_access_password",
                      configuration.currentAccessPassword, false) ||
      !readJsonString(document, "access_password", configuration.accessPassword, false) ||
      !readJsonString(document, "secret_token", configuration.secretToken, false))
  {
    log_e("JSON fields must be strings");
    return false;
  }

  if (!validateHex(configuration.epc, MAX_EPC_HEX_LENGTH, true))
  {
    log_e("Invalid EPC");
    return false;
  }
  if (!validateHex(configuration.tid, MAX_TID_HEX_LENGTH, true))
  {
    log_e("Invalid TID");
    return false;
  }
  if (!validateOptionalFixedHex(configuration.currentAccessPassword,
                                ACCESS_PASSWORD_HEX_LENGTH))
  {
    log_e("Invalid current_access_password; use exactly 8 hex characters");
    return false;
  }
  if (!validateOptionalFixedHex(configuration.accessPassword,
                                ACCESS_PASSWORD_HEX_LENGTH))
  {
    log_e("Invalid access_password; use exactly 8 hex characters");
    return false;
  }
  if (!validateOptionalFixedHex(configuration.secretToken, SECRET_TOKEN_HEX_LENGTH))
  {
    log_e("Invalid secret_token; use exactly 8 hex characters");
    return false;
  }
  return true;
}

static bool readJsonFrame(String &json)
{
  const uint32_t deadline = millis() + JSON_TIMEOUT_MS;
  bool started = false;
  bool inString = false;
  bool escaped = false;
  int braceDepth = 0;
  json = "";
  json.reserve(MAX_JSON_LENGTH);

  while (static_cast<int32_t>(millis() - deadline) < 0)
  {
    while (Serial.available() > 0)
    {
      const char character = static_cast<char>(Serial.read());
      if (!started)
      {
        if (isspace(static_cast<unsigned char>(character)))
        {
          continue;
        }
        if (character != '{')
        {
          log_e("JSON input must start with an object");
          return false;
        }
        started = true;
        braceDepth = 1;
        json += character;
        continue;
      }

      if (json.length() >= MAX_JSON_LENGTH)
      {
        log_e("JSON input is too long");
        return false;
      }
      json += character;

      if (inString)
      {
        if (escaped)
        {
          escaped = false;
        }
        else if (character == '\\')
        {
          escaped = true;
        }
        else if (character == '"')
        {
          inString = false;
        }
        continue;
      }

      if (character == '"')
      {
        inString = true;
      }
      else if (character == '{')
      {
        ++braceDepth;
      }
      else if (character == '}')
      {
        --braceDepth;
        if (braceDepth == 0)
        {
          return true;
        }
      }
    }
    delay(1);
  }

  log_e("Timed out waiting for complete JSON input");
  return false;
}

static void discardUntilLineEnd()
{
  while (Serial.available() > 0)
  {
    if (Serial.read() == '\n')
    {
      break;
    }
  }
}

static bool findConfiguredTag(const TagConfiguration &configuration)
{
  const uint8_t count = uhf.pollingOnce();
  for (uint8_t index = 0; index < count; ++index)
  {
    CARD &card = uhf.cards[index];
    if (!card.epc_str.equalsIgnoreCase(configuration.epc))
    {
      continue;
    }
    if (!uhf.select(card.epc))
    {
      log_e("Could not select configured EPC");
      continue;
    }

    uint8_t tidBuffer[12] = {0};
    if (!uhf.readCard(tidBuffer, sizeof(tidBuffer), RFID_BANK_TID, 0, 0))
    {
      log_e("Could not read configured tag TID");
      continue;
    }
    const String tid = bufferToHexString(tidBuffer, sizeof(tidBuffer));
    if (!isTidPrefix(tid, configuration.tid))
    {
      log_i("Configured EPC found, but TID does not match");
      continue;
    }
    return true;
  }

  log_i("Configured tag not found");
  return false;
}

static bool writeAccessPassword(uint32_t currentPassword, uint32_t newPassword)
{
  uint8_t passwordBytes[4] = {
      static_cast<uint8_t>(newPassword >> 24),
      static_cast<uint8_t>(newPassword >> 16),
      static_cast<uint8_t>(newPassword >> 8),
      static_cast<uint8_t>(newPassword)};

  return uhf.writeCard(passwordBytes, sizeof(passwordBytes), RFID_BANK_RESERVED, 2,
                       currentPassword);
}

static bool writeConfiguration(const TagConfiguration &configuration)
{
  uint32_t currentPassword = 0;
  uint32_t effectivePassword = 0;
  if (configuration.currentAccessPassword.length() > 0 &&
      !hexToWord(configuration.currentAccessPassword, currentPassword))
  {
    log_e("Could not decode current access password");
    return false;
  }
  effectivePassword = currentPassword;

  if (!findConfiguredTag(configuration))
  {
    return false;
  }

  if (configuration.secretToken.length() > 0)
  {
    uint8_t tokenBytes[4] = {0};
    if (!hexToBytes(configuration.secretToken, tokenBytes, sizeof(tokenBytes)))
    {
      log_e("Could not decode secret token");
      return false;
    }
    if (!uhf.writeCard(tokenBytes, sizeof(tokenBytes), RFID_BANK_USER, 0,
                       currentPassword))
    {
      log_e("Writing secret token failed");
      return false;
    }
    log_i("Secret token written");
  }

  if (configuration.accessPassword.length() > 0)
  {
    if (!hexToWord(configuration.accessPassword, effectivePassword))
    {
      log_e("Could not decode new access password");
      return false;
    }
    if (!writeAccessPassword(currentPassword, effectivePassword))
    {
      log_e("Writing access password failed");
      return false;
    }
    log_i("Access password written");
  }

  if (configuration.secretToken.length() > 0)
  {
    if (!uhf.lockCard(RFID_LOCK_FLAGS, effectivePassword))
    {
      log_e("Locking User Memory failed");
      return false;
    }
    log_i("User Memory locked");

    uint8_t expectedToken[4] = {0};
    uint8_t actualToken[4] = {0};
    if (!hexToBytes(configuration.secretToken, expectedToken, sizeof(expectedToken)) ||
        !uhf.readCard(actualToken, sizeof(actualToken), RFID_BANK_USER, 0,
                      effectivePassword))
    {
      log_e("Reading back secret token failed");
      return false;
    }
    if (memcmp(expectedToken, actualToken, sizeof(expectedToken)) != 0)
    {
      log_e("Secret token verification failed");
      return false;
    }
    log_i("Secret token verified");
  }

  return true;
}

static void printDetectedTags()
{
  const uint8_t count = uhf.pollingOnce();
  Serial.printf("Scan result: %u\r\n", count);
  for (uint8_t index = 0; index < count; ++index)
  {
    CARD &card = uhf.cards[index];
    String epc = card.epc_str;
    epc.toUpperCase();
    Serial.printf("EPC: %s, RSSI: %d dBm\r\n", epc.c_str(),
                  static_cast<int8_t>(card.rssi));
    if (!uhf.select(card.epc))
    {
      log_e("Could not select EPC %s", epc.c_str());
      Serial.printf("TID: read failed\r\n");
      continue;
    }

    uint8_t tidBuffer[12] = {0};
    if (!uhf.readCard(tidBuffer, sizeof(tidBuffer), RFID_BANK_TID, 0, 0))
    {
      log_e("Could not read TID for EPC %s", epc.c_str());
      Serial.printf("TID: read failed\r\n");
      continue;
    }
    const String tid = bufferToHexString(tidBuffer, sizeof(tidBuffer));
    Serial.printf("TID: %s\r\n", tid.c_str());
  }
}

static void handleWriteRequest()
{
  Serial.printf("Send a JSON object, ending after the closing brace.\r\n");
  Serial.printf("Required: epc, tid. Optional: current_access_password, access_password, secret_token.\r\n");

  String json;
  TagConfiguration configuration;
  if (!readJsonFrame(json))
  {
    discardUntilLineEnd();
    Serial.printf("JSON write failed. Returning to scanning.\r\n");
    return;
  }
  if (!parseConfiguration(json, configuration))
  {
    Serial.printf("JSON write failed. Returning to scanning.\r\n");
    return;
  }

  Serial.printf("Writing tag EPC %s, TID prefix %s.\r\n", configuration.epc.c_str(),
                configuration.tid.c_str());
  if (writeConfiguration(configuration))
  {
    Serial.printf("Tag initialization completed.\r\n");
  }
  else
  {
    Serial.printf("Tag initialization failed.\r\n");
  }
}

static void handleSerialInput()
{
  static bool lineHasContent = false;
  while (Serial.available() > 0)
  {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r')
    {
      continue;
    }
    if (character == '\n')
    {
      if (ignoreJsonLineEnd)
      {
        ignoreJsonLineEnd = false;
      }
      else if (!lineHasContent)
      {
        handleWriteRequest();
        ignoreJsonLineEnd = true;
      }
      lineHasContent = false;
      return;
    }
    if (!isspace(static_cast<unsigned char>(character)))
    {
      lineHasContent = true;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(2000);

  log_i("Starting RFID tag writer");
  uhf.begin(&Serial2, 115200, RFID_RX_PIN, RFID_TX_PIN, false);
  while (uhf.getVersion() == "ERROR")
  {
    log_e("RFID reader initialization failed; retrying");
    delay(1000);
  }

  if (!uhf.setOperatingRegion(RFID_REGION))
  {
    log_e("Setting RFID operating region failed");
  }
  if (!uhf.setTxPower(RFID_TX_POWER))
  {
    log_e("Setting RFID TX power failed");
  }

  log_i("RFID tag writer ready");
  Serial.printf("Press Enter to initialize a tag from JSON.\r\n");
}

void loop()
{
  handleSerialInput();
  printDetectedTags();
  delay(100);
}
