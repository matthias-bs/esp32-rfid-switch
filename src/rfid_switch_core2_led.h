/*
 * Lightweight M5Stack Core2 power LED control.
 *
 * This API controls the Core2 power-management LED directly over I2C without
 * including M5Unified. It detects the installed AXP192 or AXP2101 PMIC and
 * uses the corresponding LED control register.
 */

#pragma once

#include <Arduino.h>

namespace rfid_switch_core2_led
{
#if defined(ARDUINO_M5STACK_CORE2)
bool begin();
void set(bool enabled);
void blinkFailure();
void startConfigurationBlink();
void blinkConfiguration();
#else
inline bool begin()
{
    return false;
}

inline void set(bool)
{
}

inline void blinkFailure()
{
}

inline void startConfigurationBlink()
{
}

inline void blinkConfiguration()
{
}
#endif
}
