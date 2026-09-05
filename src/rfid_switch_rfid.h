#pragma once

#include <Arduino.h>
#include <UNIT_UHF_RFID.h>

static const uint8_t RFID_SWITCH_BANK_TID = 0x02;
static const uint8_t RFID_SWITCH_BANK_USER = 0x03;

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
    uint8_t tidBuffer[12] = {0};
    uint8_t tokenBuffer[4] = {0};
};