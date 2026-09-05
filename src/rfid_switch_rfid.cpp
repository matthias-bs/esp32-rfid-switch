#include "rfid_switch_rfid.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool RfidSwitchReader::begin(HardwareSerial *serial, uint8_t rxPin, uint8_t txPin,
                             uint8_t region, uint16_t txPower)
{
    reader.begin(serial, 115200, rxPin, txPin, false);

    bool readerAvailable = false;
    for (uint8_t attempt = 0; attempt < 5; ++attempt) {
        if (reader.getVersion() != "ERROR") {
            readerAvailable = true;
            break;
        }
        delay(100);
    }
    if (!readerAvailable) {
        return false;
    }

    if (!reader.setOperatingRegion(region)) {
        return false;
    }

    return reader.setTxPower(txPower);
}

bool RfidSwitchReader::hasValidTag(const RfidSwitchTagConfig &config)
{
    if (config.epc.length() == 0 || config.tid.length() == 0) {
        return false;
    }

    const uint8_t count = reader.pollingOnce();
    for (uint8_t index = 0; index < count; ++index) {
        CARD &card = reader.cards[index];
        if (!hexEquals(card.epc_str, config.epc)) {
            continue;
        }
        if (!reader.select(card.epc)) {
            continue;
        }

        String tid;
        if (!readTid(tid) || !hexStartsWith(tid, config.tid)) {
            continue;
        }

        if (config.token.length() > 0 && !readToken(config)) {
            continue;
        }
        return true;
    }

    return false;
}

bool RfidSwitchReader::readTid(String &tid)
{
    memset(tidBuffer, 0, sizeof(tidBuffer));
    if (!reader.readCard(tidBuffer, sizeof(tidBuffer), RFID_SWITCH_BANK_TID, 0, 0)) {
        return false;
    }

    tid.reserve(sizeof(tidBuffer) * 2);
    for (uint8_t value : tidBuffer) {
        if (value < 0x10) {
            tid += '0';
        }
        tid += String(value, HEX);
    }
    return true;
}

bool RfidSwitchReader::readToken(const RfidSwitchTagConfig &config)
{
    if (!hexToBytes(config.token, tokenBuffer, sizeof(tokenBuffer))) {
        return false;
    }

    uint8_t actualToken[sizeof(tokenBuffer)] = {0};
    if (!reader.readCard(actualToken, sizeof(actualToken), RFID_SWITCH_BANK_USER, 0,
                         parsePassword(config.accessPassword))) {
        return false;
    }

    return memcmp(actualToken, tokenBuffer, sizeof(tokenBuffer)) == 0;
}

bool RfidSwitchReader::hexToBytes(const String &value, uint8_t *output, size_t outputSize)
{
    if (value.length() != outputSize * 2) {
        return false;
    }

    for (size_t index = 0; index < outputSize; ++index) {
        char high = value[index * 2];
        char low = value[index * 2 + 1];
        if (!isxdigit(static_cast<unsigned char>(high)) ||
            !isxdigit(static_cast<unsigned char>(low))) {
            return false;
        }
        output[index] = static_cast<uint8_t>(strtoul(value.substring(index * 2, index * 2 + 2).c_str(), nullptr, 16));
    }
    return true;
}

bool RfidSwitchReader::hexEquals(const String &left, const String &right)
{
    return left.equalsIgnoreCase(right);
}

bool RfidSwitchReader::hexStartsWith(const String &value, const String &prefix)
{
    return prefix.length() <= value.length() &&
           value.substring(0, prefix.length()).equalsIgnoreCase(prefix);
}

uint32_t RfidSwitchReader::parsePassword(const String &value)
{
    if (value.length() == 0) {
        return 0;
    }
    return strtoul(value.c_str(), nullptr, 16);
}