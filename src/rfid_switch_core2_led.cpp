/*
 * Lightweight M5Stack Core2 power LED control implementation.
 *
 * The standard implementation is M5Unified's M5.Power.setLed(). This file
 * provides the smaller direct PMIC I2C alternative for lightweight sketches
 * where including M5Unified causes unacceptable firmware size pressure.
 */

#include "rfid_switch_core2_led.h"

#if defined(ARDUINO_M5STACK_CORE2)

#include <Wire.h>

namespace
{
constexpr uint8_t AXP192_ADDRESS = 0x34;
constexpr uint8_t AXP192_GPIO1_CONTROL_REGISTER = 0x92;
constexpr uint8_t AXP192_PWM1_X_REGISTER = 0x98;
constexpr uint8_t AXP192_PWM1_Y1_REGISTER = 0x99;
constexpr uint8_t AXP192_POWER_LED_REGISTER = 0x9a;
constexpr int CORE2_I2C_SDA_PIN = 21;
constexpr int CORE2_I2C_SCL_PIN = 22;
constexpr uint8_t AXP192_ID_REGISTER = 0x03;
constexpr uint8_t AXP2101_LED_REGISTER = 0x69;

enum class PmicType
{
    unknown,
    axp192,
    axp2101
};

bool i2cReady = false;
PmicType pmicType = PmicType::unknown;
uint32_t configurationBlinkTimestamp = 0;
bool configurationLedOn = false;
bool configurationCallbackReported = false;

bool readRegister(uint8_t address, uint8_t &value)
{
    Wire1.beginTransmission(AXP192_ADDRESS);
    Wire1.write(address);
    if (Wire1.endTransmission() != 0 ||
        Wire1.requestFrom(AXP192_ADDRESS, static_cast<uint8_t>(1)) != 1)
    {
        return false;
    }
    value = Wire1.read();
    return true;
}

bool writeRegister(uint8_t address, uint8_t value)
{
    Wire1.beginTransmission(AXP192_ADDRESS);
    Wire1.write(address);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}
}

namespace rfid_switch_core2_led
{
bool begin()
{
    if (i2cReady)
    {
        return true;
    }

    Wire1.begin(CORE2_I2C_SDA_PIN, CORE2_I2C_SCL_PIN);
    Wire1.setClock(100000);

    uint8_t chipId = 0;
    uint8_t registerValue = 0;
    const bool idRead = readRegister(AXP192_ID_REGISTER, chipId);
    const bool recognizedId = chipId == 0x03 || chipId == 0x4a;
    bool ledRead = false;
    bool pwmXWritten = true;
    bool pwmYWritten = true;
    bool ledWritten = false;
    bool gpioWritten = true;

    if (chipId == 0x03)
    {
        pmicType = PmicType::axp192;
        ledRead = readRegister(AXP192_POWER_LED_REGISTER, registerValue);
        pwmXWritten = writeRegister(AXP192_PWM1_X_REGISTER, 0x00);
        pwmYWritten = writeRegister(AXP192_PWM1_Y1_REGISTER, 0xff);
        ledWritten = writeRegister(AXP192_POWER_LED_REGISTER, 0xff);
        gpioWritten = writeRegister(AXP192_GPIO1_CONTROL_REGISTER, 0x02);
    }
    else if (chipId == 0x4a)
    {
        pmicType = PmicType::axp2101;
        ledRead = readRegister(AXP2101_LED_REGISTER, registerValue);
        ledWritten = writeRegister(AXP2101_LED_REGISTER, 0x05);
    }

    i2cReady = idRead && recognizedId && ledRead && pwmXWritten &&
               pwmYWritten && ledWritten && gpioWritten;
    log_i("[CORE2 LED] PMIC id=%s value=0x%02x, init read=%s, writes=%s/%s/%s/%s, ready=%s",
          idRead ? "ok" : "failed",
          chipId,
          ledRead ? "ok" : "failed",
          pwmXWritten ? "ok" : "failed",
          pwmYWritten ? "ok" : "failed",
          ledWritten ? "ok" : "failed",
          gpioWritten ? "ok" : "failed",
          i2cReady ? "yes" : "no");
    return i2cReady;
}

void set(bool enabled)
{
    if (!begin())
    {
        return;
    }

    if (pmicType == PmicType::axp2101)
    {
        writeRegister(AXP2101_LED_REGISTER, enabled ? 0x35 : 0x05);
    }
    else
    {
        writeRegister(AXP192_POWER_LED_REGISTER, enabled ? 0 : 255);
    }
}

void blinkFailure()
{
    for (uint8_t blink = 0; blink < 3; ++blink)
    {
        set(true);
        delay(200);
        set(false);
        delay(200);
    }
}

void startConfigurationBlink()
{
    configurationBlinkTimestamp = millis() - 500;
    configurationLedOn = false;
    configurationCallbackReported = false;
    log_i("[CORE2 LED] Configuration blink started");
    set(false);
}

void blinkConfiguration()
{
    if (!configurationCallbackReported)
    {
        configurationCallbackReported = true;
        log_i("[CORE2 LED] Configuration blink callback active");
    }

    const uint32_t now = millis();
    if (now - configurationBlinkTimestamp >= 500)
    {
        configurationBlinkTimestamp = now;
        configurationLedOn = !configurationLedOn;
        set(configurationLedOn);
    }
}
}

#endif
